#include "ui_home.h"

#include <stdio.h>
#include <string.h>
#include "font_zh.h"
#include "app_diag.h"
#include "ui_weather.h"
#include "ui_settings.h"
#include "ui_net.h"
#include "ui_sysmon.h"
#include "net_weather.h"

static lv_obj_t *s_scr_home = NULL;
static lv_obj_t *s_status   = NULL;   /* 底部状态栏文字 */

/* 卡片点击：按名字分发 */
static void card_click_cb(lv_event_t *e)
{
    const char *name = (const char *)lv_event_get_user_data(e);

    if (s_status) lv_label_set_text_fmt(s_status, "Open: %s", name);

    if (strcmp(name, "天气") == 0) {
        lv_scr_load(WeatherUI_GetScreen());
        if (s_status) lv_label_set_text(s_status, "Fetching weather...");
        Weather_TaskStart();
    } else if (strcmp(name, "手机") == 0) {
        lv_scr_load(NetUI_GetScreen());
    } else if (strcmp(name, "系统") == 0) {
        lv_scr_load(SysMon_GetScreen());
    } else if (strcmp(name, "设置") == 0) {
        lv_scr_load(Settings_GetScreen());
    }
}

/* 一张功能卡片：左侧色条 + 名称 */
static lv_obj_t *create_card(lv_obj_t *parent, const char *name, uint32_t color, int y)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 224, 52);
    lv_obj_set_pos(card, 8, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bar = lv_obj_create(card);
    lv_obj_set_size(bar, 4, 52);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, name);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_add_event_cb(card, card_click_cb, LV_EVENT_CLICKED, (void *)name);
    return card;
}

void Home_Init(void)
{
    /* 主界面直接用默认屏幕（screen 0） */
    s_scr_home = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr_home, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_scr_home, &myfont_16, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_scr_home, LV_SCROLLBAR_MODE_OFF);

    /* 顶部标题栏 */
    lv_obj_t *header = lv_obj_create(s_scr_home);
    lv_obj_set_size(header, 240, 40);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x0F172A), LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "智能面板");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_center(title);

    /* 四张功能卡片 */
    create_card(s_scr_home, "天气", 0x3B82F6, 48);
    create_card(s_scr_home, "手机", 0x10B981, 106);
    create_card(s_scr_home, "系统", 0x06B6D4, 164);
    create_card(s_scr_home, "设置", 0x8B5CF6, 222);

    /* 底部状态栏 */
    s_status = lv_label_create(s_scr_home);
    lv_label_set_text(s_status, "ESP32-S3 + LVGL 9.6");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -8);

    Diag_CheckGlyphs(s_scr_home, "主页");
}

lv_obj_t *Home_GetScreen(void)
{
    return s_scr_home;
}
