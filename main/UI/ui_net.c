#include "ui_net.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "font_zh.h"
#include "ui_home.h"
#include "net_web.h"
#include "app_diag.h"

static const char *TAG = "ui_net";

static lv_obj_t *s_scr   = NULL;
static lv_obj_t *s_ip    = NULL;
static lv_obj_t *s_state = NULL;
static char      s_last_ip[16] = "";

static void back_cb(lv_event_t *e)
{
    lv_scr_load(Home_GetScreen());
}

/* 每秒刷新 IP（开机瞬间可能还没拿到地址） */
static void net_ui_timer(lv_timer_t *t)
{
    const char *ip = Web_GetIP();

    /* IP 变化时打一条日志，方便串口里直接看到访问地址 */
    if (strcmp(ip, s_last_ip) != 0) {
        snprintf(s_last_ip, sizeof(s_last_ip), "%s", ip);
        if (ip[0] != '0') ESP_LOGI(TAG, "手机访问地址: http://%s/", ip);
    }

    if (s_ip) lv_label_set_text(s_ip, ip);
    if (s_state) {
        bool ok = (ip[0] != '0');
        lv_label_set_text(s_state, ok ? "服务已就绪" : "等待网络...");
        lv_obj_set_style_text_color(s_state,
            lv_color_hex(ok ? 0x22C55E : 0xF59E0B), LV_PART_MAIN);
    }
}

void NetUI_Init(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(0x0B1E3D), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_scr, &myfont_16, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_scr, LV_SCROLLBAR_MODE_OFF);

    /* 返回 */
    lv_obj_t *back = lv_btn_create(s_scr);
    lv_obj_set_size(back, 30, 30);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_center(bl);

    lv_obj_t *tl = lv_label_create(s_scr);
    lv_label_set_text(tl, "手机控制");
    lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(tl, 44, 12);

    /* 卡片：IP */
    lv_obj_t *card = lv_obj_create(s_scr);
    lv_obj_set_size(card, 224, 104);
    lv_obj_set_pos(card, 8, 48);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(card);
    lv_label_set_text(cap, "在手机浏览器打开");
    lv_obj_set_style_text_color(cap, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_pos(cap, 14, 12);

    s_ip = lv_label_create(card);
    lv_label_set_text(s_ip, "0.0.0.0");
    lv_obj_set_style_text_font(s_ip, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ip, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(s_ip, 14, 36);

    s_state = lv_label_create(card);
    lv_label_set_text(s_state, "等待网络...");
    lv_obj_set_style_text_color(s_state, lv_color_hex(0xF59E0B), LV_PART_MAIN);
    lv_obj_set_pos(s_state, 14, 76);

    /* 说明 */
    lv_obj_t *hint = lv_label_create(s_scr);
    lv_obj_set_width(hint, 224);
    lv_label_set_text(hint,
        "手机需连同一个 WiFi\n\n"
        "页面上可以：\n"
        "· 切换 8 个 GPIO 电平\n"
        "· 实时查看串口日志");
    lv_obj_set_style_text_color(hint, lv_color_hex(0xD1D5DB), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(hint, 3, LV_PART_MAIN);
    lv_obj_set_pos(hint, 12, 168);

    lv_timer_create(net_ui_timer, 1000, NULL);
    ESP_LOGI(TAG, "手机控制页就绪");

    Diag_CheckGlyphs(s_scr, "手机");
}

lv_obj_t *NetUI_GetScreen(void)
{
    return s_scr;
}
