#include "ui_sysmon.h"

#include <stdio.h>
#include <string.h>

#include "font_zh.h"
#include "app_sysinfo.h"
#include "app_diag.h"
#include "ui_home.h"

LV_FONT_DECLARE(lv_font_montserrat_28);

#define COL_BG        0x111827
#define COL_HEADER    0x0F172A
#define COL_TRACK     0x1F2937
#define COL_RAM       0x3B82F6
#define COL_FLASH     0x10B981
#define COL_TEXT      0xF3F4F6
#define COL_DIM       0x9CA3AF
#define COL_BAR       0x38BDF8
#define COL_WARN      0xF87171

/* 任务区竖直排布：6 行 × 20px，从 y=194 到 y=313，
 * 必须严格小于 320，否则最后一行的字会被屏幕底边切掉。
 * 列宽分配（myfont_16 半角约 8px/字符）：
 *   名字 14~86（72px，够 9 字符，如 esp_timer）
 *   条形 90~160（70px）
 *   百分比 164~196 / 栈余 198~226 */
#define TASK_Y0       194
#define TASK_ROW_H    20
#define BAR_X         90
#define BAR_W         70
#define BAR_H         8

static lv_obj_t *s_scr        = NULL;
static lv_obj_t *s_arc_ram    = NULL;
static lv_obj_t *s_arc_flash  = NULL;
static lv_obj_t *s_lbl_ram    = NULL;
static lv_obj_t *s_lbl_flash  = NULL;
static lv_obj_t *s_lbl_uptime = NULL;
static lv_obj_t *s_lbl_tasks  = NULL;

static lv_obj_t *s_task_name[SYSINFO_TASK_MAX] = {0};
static lv_obj_t *s_task_bar[SYSINFO_TASK_MAX]  = {0};
static lv_obj_t *s_task_pct[SYSINFO_TASK_MAX]  = {0};
static lv_obj_t *s_task_stk[SYSINFO_TASK_MAX]  = {0};

static void back_to_home_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load(Home_GetScreen());
}

/* 环形图：270° 表盘，圆角端点，去掉旋钮和可点击（纯显示） */
static lv_obj_t *make_arc(lv_obj_t *parent, int x, int y, uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, 92, 92);
    lv_obj_set_pos(arc, x, y);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);

    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);

    lv_obj_set_style_arc_width(arc, 9, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);

    return arc;
}

/* 环中心的百分比数字 */
static lv_obj_t *make_pct_label(lv_obj_t *parent, lv_obj_t *arc, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, "0%");
    lv_obj_align_to(lbl, arc, LV_ALIGN_CENTER, 0, -7);
    return lbl;
}

/* 环下方的小标签 */
static void make_tag(lv_obj_t *parent, lv_obj_t *arc, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, &myfont_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
    lv_label_set_text(lbl, text);
    lv_obj_align_to(lbl, arc, LV_ALIGN_CENTER, 0, 17);
}

static void refresh_cb(lv_timer_t *t)
{
    (void)t;

    sysinfo_t si;
    SysInfo_Get(&si);

    uint32_t ram_pct = si.ram_total_kb ? (si.ram_used_kb * 100u / si.ram_total_kb) : 0;
    uint32_t fls_pct = si.flash_total_kb ? (si.flash_used_kb * 100u / si.flash_total_kb) : 0;
    if (ram_pct > 100) ram_pct = 100;
    if (fls_pct > 100) fls_pct = 100;

    lv_arc_set_value(s_arc_ram, (int32_t)ram_pct);
    lv_arc_set_value(s_arc_flash, (int32_t)fls_pct);
    lv_label_set_text_fmt(s_lbl_ram, "%u%%", (unsigned)ram_pct);
    lv_label_set_text_fmt(s_lbl_flash, "%u%%", (unsigned)fls_pct);

    /* 运行时长：超过一天显示 d hh:mm，否则 hh:mm:ss */
    char buf[40];
    uint32_t up = si.uptime_s;
    uint32_t d  = up / 86400;
    uint32_t h  = (up % 86400) / 3600;
    uint32_t m  = (up % 3600) / 60;
    uint32_t s  = up % 60;

    if (d > 0) {
        snprintf(buf, sizeof(buf), "运行 %ud %02u:%02u", (unsigned)d, (unsigned)h, (unsigned)m);
    } else {
        snprintf(buf, sizeof(buf), "运行 %02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)s);
    }
    lv_label_set_text(s_lbl_uptime, buf);

    sysinfo_task_t rows[SYSINFO_TASK_MAX];
    uint8_t load = 0;
    int n = SysInfo_GetTopTasks(rows, SYSINFO_TASK_MAX, &load);

    /* 显示"CPU 负载"而不是"任务数"：负载才是用户真正关心的，
     * 而且能直接抵消 IDLE 占比带来的误解 */
    snprintf(buf, sizeof(buf), "CPU 负载 %u%%", (unsigned)load);
    lv_label_set_text(s_lbl_tasks, buf);

    for (int i = 0; i < SYSINFO_TASK_MAX; i++) {
        if (!s_task_name[i]) continue;

        if (i >= n) {
            lv_obj_add_flag(s_task_name[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_task_bar[i],  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_task_pct[i],  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_task_stk[i],  LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        lv_obj_clear_flag(s_task_name[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_task_bar[i],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_task_pct[i],  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_task_stk[i],  LV_OBJ_FLAG_HIDDEN);

        lv_label_set_text(s_task_name[i], rows[i].name);

        /* 条形宽度按 CPU 占比取整，0% 时宽度为 0 自然不显示 */
        lv_obj_set_width(s_task_bar[i], (int32_t)((uint32_t)BAR_W * rows[i].cpu_pct / 100u));

        /* 占用过半转红，一眼能看出谁在抢 CPU */
        bool hot = rows[i].cpu_pct >= 50;
        lv_obj_set_style_bg_color(s_task_bar[i], lv_color_hex(hot ? COL_WARN : COL_BAR), 0);
        lv_obj_set_style_text_color(s_task_pct[i], lv_color_hex(hot ? COL_WARN : COL_TEXT), 0);

        snprintf(buf, sizeof(buf), "%u%%", (unsigned)rows[i].cpu_pct);
        lv_label_set_text(s_task_pct[i], buf);

        /* 栈余不足 1KB 时显示 <1K：IDLE 这类小栈任务余几百字节，
         * 取整成 0K 会让人误以为栈已耗尽 */
        if (rows[i].stack_free >= 1024) {
            snprintf(buf, sizeof(buf), "%uK", (unsigned)(rows[i].stack_free / 1024));
        } else {
            snprintf(buf, sizeof(buf), "<1K");
        }
        lv_label_set_text(s_task_stk[i], buf);
    }
}

void SysMon_Init(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_text_font(s_scr, &myfont_16, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_scr, LV_SCROLLBAR_MODE_OFF);

    /* 标题栏 */
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_size(header, 240, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(COL_HEADER), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "系统运行状态");
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_center(title);

    /* 返回键：和设置页的二级页保持同样的圆形样式 */
    lv_obj_t *back = lv_btn_create(header);
    lv_obj_set_size(back, 30, 30);
    lv_obj_set_pos(back, 6, 5);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A6E), 0);
    lv_obj_set_style_shadow_width(back, 0, 0);
    lv_obj_add_event_cb(back, back_to_home_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_center(bl);

    /* 两个环形图 */
    s_arc_ram = make_arc(s_scr, 14, 46, COL_RAM);
    make_tag(s_scr, s_arc_ram, "RAM");
    s_lbl_ram = make_pct_label(s_scr, s_arc_ram, COL_RAM);

    s_arc_flash = make_arc(s_scr, 134, 46, COL_FLASH);
    make_tag(s_scr, s_arc_flash, "Flash");
    s_lbl_flash = make_pct_label(s_scr, s_arc_flash, COL_FLASH);

    /* 运行时长 / 任务数 */
    s_lbl_uptime = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_uptime, 14, 148);
    lv_obj_set_style_text_color(s_lbl_uptime, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(s_lbl_uptime, "运行 --:--:--");

    s_lbl_tasks = lv_label_create(s_scr);
    lv_obj_set_pos(s_lbl_tasks, 146, 148);
    lv_obj_set_style_text_color(s_lbl_tasks, lv_color_hex(COL_TEXT), 0);
    lv_label_set_text(s_lbl_tasks, "CPU 负载 --%");

    /* 分隔线 */
    lv_obj_t *line = lv_obj_create(s_scr);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_pos(line, 14, 168);
    lv_obj_set_style_bg_color(line, lv_color_hex(COL_TRACK), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);

    /* 表头：三个 label 精确定位，比一个长字符串更容易和下面的列对齐 */
    const char *th_text[3] = { "任务", "CPU 占用", "栈余" };
    const int   th_x[3]    = { 14, BAR_X, 198 };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *th = lv_label_create(s_scr);
        lv_obj_set_pos(th, th_x[i], 172);
        lv_obj_set_style_text_color(th, lv_color_hex(COL_DIM), 0);
        lv_label_set_text(th, th_text[i]);
    }

    /* 任务行：名字 + 横向条形（底槽 + 进度）+ 百分比 + 栈余 */
    for (int i = 0; i < SYSINFO_TASK_MAX; i++) {
        int y = TASK_Y0 + i * TASK_ROW_H;

        s_task_name[i] = lv_label_create(s_scr);
        lv_obj_set_pos(s_task_name[i], 14, y);
        lv_obj_set_style_text_color(s_task_name[i], lv_color_hex(COL_TEXT), 0);
        lv_label_set_text(s_task_name[i], "");

        /* 底槽：先建，进度条后建，后者自然叠在上面 */
        lv_obj_t *bg = lv_obj_create(s_scr);
        lv_obj_set_pos(bg, BAR_X, y + 5);
        lv_obj_set_size(bg, BAR_W, BAR_H);
        lv_obj_set_style_bg_color(bg, lv_color_hex(COL_TRACK), 0);
        lv_obj_set_style_radius(bg, BAR_H / 2, 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        lv_obj_set_style_pad_all(bg, 0, 0);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        s_task_bar[i] = lv_obj_create(s_scr);
        lv_obj_set_pos(s_task_bar[i], BAR_X, y + 5);
        lv_obj_set_size(s_task_bar[i], 0, BAR_H);
        lv_obj_set_style_bg_color(s_task_bar[i], lv_color_hex(COL_BAR), 0);
        lv_obj_set_style_radius(s_task_bar[i], BAR_H / 2, 0);
        lv_obj_set_style_border_width(s_task_bar[i], 0, 0);
        lv_obj_set_style_pad_all(s_task_bar[i], 0, 0);
        lv_obj_clear_flag(s_task_bar[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        s_task_pct[i] = lv_label_create(s_scr);
        lv_obj_set_pos(s_task_pct[i], 164, y);
        lv_obj_set_style_text_color(s_task_pct[i], lv_color_hex(COL_TEXT), 0);
        lv_label_set_text(s_task_pct[i], "");

        s_task_stk[i] = lv_label_create(s_scr);
        lv_obj_set_pos(s_task_stk[i], 198, y);
        lv_obj_set_style_text_color(s_task_stk[i], lv_color_hex(COL_DIM), 0);
        lv_label_set_text(s_task_stk[i], "");
    }

    refresh_cb(NULL);

    /* 1 秒刷新一次：环形动画有过渡，太快会显得抖 */
    lv_timer_create(refresh_cb, 1000, NULL);

    Diag_CheckGlyphs(s_scr, "系统监控");
}

lv_obj_t *SysMon_GetScreen(void)
{
    return s_scr;
}
