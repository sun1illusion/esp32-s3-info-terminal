#include "ui_weather.h"

#include <stdio.h>
#include "esp_log.h"
#include "font_zh.h"
#include "ui_icons.h"
#include "ui_home.h"
#include "net_weather.h"
#include "app_state.h"
#include "app_diag.h"

#define FC_DAYS_UI  FC_DAYS

static lv_obj_t *s_scr_weather = NULL;

/* 天气页 UI 引用（数据到达后由 timer 更新） */
static lv_obj_t *s_icon_box   = NULL;   /* 大天气图标容器 */
static lv_obj_t *s_temp_label = NULL;
static lv_obj_t *s_text_label = NULL;
static lv_obj_t *s_drop_box   = NULL;   /* 湿度水滴容器 */
static lv_obj_t *s_hum_label  = NULL;
static lv_obj_t *s_wind_label = NULL;
static lv_obj_t *s_feels_box  = NULL;   /* 体感小温度计容器 */
static lv_obj_t *s_feels_lbl  = NULL;
static lv_obj_t *s_dir_label  = NULL;
static lv_obj_t *s_fc_icon[FC_DAYS_UI] = {NULL};
static lv_obj_t *s_fc_temp[FC_DAYS_UI] = {NULL};

/* 返回主界面 */
static void back_to_home_cb(lv_event_t *e)
{
    lv_scr_load(Home_GetScreen());
}

/* 次级数据小卡片：图标容器 + 数值 label */
static lv_obj_t *create_metric_card(lv_obj_t *parent, int x, int y,
                                    lv_obj_t **icon_box_out, lv_obj_t **val_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 108, 38);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);   /* 不清 padding 会把图标挤到卡片外被裁 */
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ib = lv_obj_create(card);
    lv_obj_set_size(ib, 28, 28);
    lv_obj_set_pos(ib, 4, 5);
    lv_obj_set_style_bg_opa(ib, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(ib, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ib, 0, LV_PART_MAIN);
    lv_obj_clear_flag(ib, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ib, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    if (icon_box_out) *icon_box_out = ib;

    lv_obj_t *val = lv_label_create(card);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(val, LV_ALIGN_LEFT_MID, 38, 0);
    if (val_out) *val_out = val;

    return card;
}

/* 定时刷新数据（LVGL timer 上下文） */
static void weather_refresh_timer(lv_timer_t *t)
{
    Weather_TaskStart();
}

/* 数据就绪后刷新界面（LVGL timer 上下文，安全） */
static void weather_ui_timer(lv_timer_t *t)
{
    if (!g_weather_ready) return;
    g_weather_ready = false;

    char buf[32];
    ESP_LOGI("ui_weather", "刷新界面: %.1f°C 湿度%.0f%% 风%.1f", g_temp_f, g_hum_f, g_wind_f);

    /* 温度 + 描述（浮点必须用 snprintf，LVGL 的 fmt 不支持 %f） */
    if (s_temp_label) {
        snprintf(buf, sizeof(buf), "%.1f°C", g_temp_f);
        lv_label_set_text(s_temp_label, buf);
    }
    if (s_text_label) lv_label_set_text(s_text_label, Weather_CodeToText(g_weather_code));

    /* 主天气图标 */
    if (s_icon_box) {
        lv_obj_clean(s_icon_box);
        UI_WeatherIcon(s_icon_box, g_weather_code, 0, 0);
    }

    /* 湿度：水滴随湿度变化 */
    if (s_drop_box) {
        lv_obj_clean(s_drop_box);
        UI_HumidityIcon(s_drop_box, g_hum_f, 0, 0);
    }
    if (s_hum_label) {
        snprintf(buf, sizeof(buf), "%.0f%%", g_hum_f);
        lv_label_set_text(s_hum_label, buf);
    }

    /* 风速等级（蒲福） */
    if (s_wind_label) lv_label_set_text_fmt(s_wind_label, "Lv%d", Weather_WindLevel(g_wind_f));

    /* 体感：小温度计变色 */
    if (s_feels_box) {
        lv_obj_clean(s_feels_box);
        UI_ThermoIconSmall(s_feels_box, g_feels_f, 0, 0);
    }
    if (s_feels_lbl) {
        snprintf(buf, sizeof(buf), "%.1f°", g_feels_f);
        lv_label_set_text(s_feels_lbl, buf);
    }

    /* 风向 */
    if (s_dir_label) lv_label_set_text(s_dir_label, Weather_WindDirText(g_wind_dir));

    /* 未来预报 */
    for (int i = 0; i < FC_DAYS_UI; i++) {
        if (s_fc_icon[i]) {
            lv_obj_clean(s_fc_icon[i]);
            UI_WeatherIconSmall(s_fc_icon[i], g_daily_code[i], 0, 0);
        }
        if (s_fc_temp[i]) {
            lv_label_set_text_fmt(s_fc_temp[i], "%d/%d°", g_daily_max[i], g_daily_min[i]);
        }
    }
}

void WeatherUI_Init(void)
{
    s_scr_weather = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_weather, lv_color_hex(0x111827), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_scr_weather, &myfont_16, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_weather, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_scr_weather, LV_SCROLLBAR_MODE_OFF);

    /* 左上角圆形返回键 */
    lv_obj_t *back = lv_btn_create(s_scr_weather);
    lv_obj_set_size(back, 30, 30);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_to_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_center(bl);

    /* 城市名 */
    lv_obj_t *city = lv_label_create(s_scr_weather);
    lv_label_set_text(city, "杭州");
    lv_obj_set_style_text_color(city, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_align(city, LV_ALIGN_TOP_MID, 0, 8);

    /* 主卡片：大图标 + 大温度 + 描述 */
    lv_obj_t *main_card = lv_obj_create(s_scr_weather);
    lv_obj_set_size(main_card, 224, 112);
    lv_obj_set_pos(main_card, 8, 40);
    lv_obj_set_style_bg_color(main_card, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_radius(main_card, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(main_card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(main_card, LV_OBJ_FLAG_SCROLLABLE);

    s_icon_box = lv_obj_create(main_card);
    lv_obj_set_size(s_icon_box, 80, 80);
    lv_obj_set_pos(s_icon_box, 8, 16);
    lv_obj_set_style_bg_opa(s_icon_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_icon_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_icon_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_icon_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_icon_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_temp_label = lv_label_create(main_card);
    lv_label_set_text(s_temp_label, "--°C");
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(s_temp_label, 98, 30);

    s_text_label = lv_label_create(main_card);
    lv_label_set_text(s_text_label, "--");
    lv_obj_set_style_text_color(s_text_label, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
    lv_obj_set_pos(s_text_label, 100, 76);

    /* 次级数据 2x2 */
    create_metric_card(s_scr_weather, 8,   160, &s_drop_box,  &s_hum_label);    /* 湿度 */
    create_metric_card(s_scr_weather, 124, 160, NULL,        &s_wind_label);   /* 风速等级 */
    create_metric_card(s_scr_weather, 8,   204, &s_feels_box, &s_feels_lbl);   /* 体感 */

    lv_obj_t *winddir_box = NULL;
    create_metric_card(s_scr_weather, 124, 204, &winddir_box, &s_dir_label);   /* 风向 */

    /* 风速卡片的风线图标（静态） */
    lv_obj_t *wind_box = lv_obj_create(s_scr_weather);
    lv_obj_set_size(wind_box, 28, 28);
    lv_obj_set_pos(wind_box, 128, 165);
    lv_obj_set_style_bg_opa(wind_box, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(wind_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wind_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(wind_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wind_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    UI_WindIcon(wind_box, 0, 0);

    /* 风向卡片的旗帜图标（静态） */
    UI_WindDirIcon(winddir_box, 0, 0);

    /* 湿度水滴初始形态 */
    UI_HumidityIcon(s_drop_box, 50, 0, 0);

    /* 未来 4 天预报 */
    for (int i = 0; i < FC_DAYS_UI; i++) {
        lv_obj_t *box = lv_obj_create(s_scr_weather);
        lv_obj_set_size(box, 50, 70);
        lv_obj_set_pos(box, 8 + i * 58, 250);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1F2937), LV_PART_MAIN);
        lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *dl = lv_label_create(box);
        lv_label_set_text(dl, g_daily_date[i]);
        lv_obj_set_style_text_color(dl, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
        lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 2);

        s_fc_icon[i] = lv_obj_create(box);
        lv_obj_set_size(s_fc_icon[i], 28, 28);
        lv_obj_set_style_bg_opa(s_fc_icon[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(s_fc_icon[i], 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_fc_icon[i], 0, LV_PART_MAIN);
        lv_obj_clear_flag(s_fc_icon[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_fc_icon[i], LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_align(s_fc_icon[i], LV_ALIGN_CENTER, 0, -6);

        s_fc_temp[i] = lv_label_create(box);
        lv_label_set_text(s_fc_temp[i], "--/--");
        lv_obj_set_style_text_color(s_fc_temp[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_align(s_fc_temp[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    lv_timer_create(weather_ui_timer, 200, NULL);
    /* 每 10 分钟自动刷新一次数据（即使不进天气页） */
    lv_timer_create(weather_refresh_timer, 10 * 60 * 1000, NULL);

    Diag_CheckGlyphs(s_scr_weather, "天气");
}

lv_obj_t *WeatherUI_GetScreen(void)
{
    return s_scr_weather;
}
