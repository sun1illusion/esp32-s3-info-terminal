#include "ui_wifi.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "font_zh.h"
#include "ui_home.h"
#include "ui_settings.h"
#include "net_wifi.h"
#include "net_web.h"

static const char *TAG = "ui_wifi";

#define AP_MAX   5      /* 屏幕上一屏能放下 5 个热点 */

static lv_obj_t *s_scr      = NULL;
static lv_obj_t *s_ssid_lbl = NULL;
static lv_obj_t *s_ip_lbl   = NULL;
static lv_obj_t *s_state    = NULL;
static lv_obj_t *s_scan_btn = NULL;
static lv_obj_t *s_rows[AP_MAX]      = {NULL};
static lv_obj_t *s_row_lbl[AP_MAX]   = {NULL};

static wifi_ap_t      s_aps[AP_MAX];
static int            s_ap_count = 0;
static volatile bool  s_scan_busy = false;
static volatile bool  s_scan_done = false;

/* 密码弹窗 */
static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_ta    = NULL;
static char      s_target_ssid[33] = {0};

/* ---------------- 扫描任务（阻塞 2~3s，不能放 LVGL 任务里） ---------------- */
static void scan_task(void *arg)
{
    s_scan_busy = true;
    s_scan_done = false;
    s_ap_count = WiFi_Scan(s_aps, AP_MAX);
    s_scan_busy = false;
    s_scan_done = true;
    vTaskDelete(NULL);
}

static void start_scan(void)
{
    if (s_scan_busy) return;

    if (s_scan_btn) lv_label_set_text(lv_obj_get_child(s_scan_btn, 0), "扫描中...");
    if (s_scan_btn) lv_obj_add_state(s_scan_btn, LV_STATE_DISABLED);

    BaseType_t ok = xTaskCreate(scan_task, "wifi_scan", 4096, NULL, 3, NULL);
    if (ok != pdPASS) {
        if (s_scan_btn) {
            lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
            lv_label_set_text(lv_obj_get_child(s_scan_btn, 0), "扫描失败，重试");
        }
        s_scan_busy = false;
        ESP_LOGE(TAG, "扫描任务创建失败");
    }
}

static void scan_btn_cb(lv_event_t *e)
{
    start_scan();
}

/* ---------------- 密码输入 ---------------- */

static void close_modal(void)
{
    if (s_modal) {
        lv_obj_delete(s_modal);
        s_modal = NULL;
        s_ta = NULL;
    }
}

static void modal_cancel_cb(lv_event_t *e)
{
    close_modal();
}

static void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CANCEL) {
        close_modal();
        return;
    }
    if (code != LV_EVENT_READY) return;

    const char *pass = s_ta ? lv_textarea_get_text(s_ta) : "";
    ESP_LOGI(TAG, "连接 %s（密码长度 %d）", s_target_ssid, (int)strlen(pass));

    WiFi_Switch(s_target_ssid, pass);
    close_modal();

    if (s_state) {
        lv_label_set_text(s_state, "连接中...");
        lv_obj_set_style_text_color(s_state, lv_color_hex(0xF59E0B), LV_PART_MAIN);
    }
}

static void row_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_ap_count) return;

    snprintf(s_target_ssid, sizeof(s_target_ssid), "%s", s_aps[idx].ssid);

    /* 弹窗：提示 + 输入框 + 键盘 */
    s_modal = lv_obj_create(s_scr);
    lv_obj_set_size(s_modal, 240, 320);
    lv_obj_set_pos(s_modal, 0, 0);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x0B1E3D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_modal, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_modal, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_modal);
    lv_label_set_text_fmt(t, "密码: %s", s_target_ssid);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(t, 10, 8);

    s_ta = lv_textarea_create(s_modal);
    lv_obj_set_size(s_ta, 220, 38);
    lv_obj_set_pos(s_ta, 10, 32);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, 63);
    lv_textarea_set_placeholder_text(s_ta, "输入密码");
    lv_obj_set_style_text_font(s_ta, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ta, lv_color_hex(0x1F2937), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

    lv_obj_t *cancel = lv_btn_create(s_modal);
    lv_obj_set_size(cancel, 70, 30);
    lv_obj_set_pos(cancel, 165, 76);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(0x374151), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cancel, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(cancel, modal_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "取消");
    lv_obj_center(cl);

    lv_obj_t *kb = lv_keyboard_create(s_modal);
    lv_obj_set_size(kb, 240, 170);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
}

/* ---------------- 列表/状态刷新 ---------------- */

static void refresh_list(void)
{
    for (int i = 0; i < AP_MAX; i++) {
        if (!s_rows[i]) continue;
        if (i < s_ap_count) {
            char buf[48];
            snprintf(buf, sizeof(buf), "%s  %d dBm", s_aps[i].ssid, (int)s_aps[i].rssi);
            lv_label_set_text(s_row_lbl[i], buf);
            lv_obj_clear_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_scan_btn) {
        lv_obj_clear_state(s_scan_btn, LV_STATE_DISABLED);
        lv_label_set_text(lv_obj_get_child(s_scan_btn, 0), "扫描附近热点");
    }
}

static void wifi_ui_timer(lv_timer_t *t)
{
    /* 扫描完成 → 铺列表 */
    if (s_scan_done) {
        s_scan_done = false;
        refresh_list();
    }

    /* 状态文字 */
    if (s_ssid_lbl) lv_label_set_text(s_ssid_lbl, WiFi_GetSSID());
    if (s_ip_lbl)   lv_label_set_text_fmt(s_ip_lbl, "IP %s", Web_GetIP());

    if (s_state) {
        bool ok = WiFi_IsConnected();
        lv_label_set_text(s_state, ok ? "已连接" : "未连接，正在重试...");
        lv_obj_set_style_text_color(s_state,
            lv_color_hex(ok ? 0x22C55E : 0xEF4444), LV_PART_MAIN);
    }
}

/* ---------------- 页面 ---------------- */

static void back_cb(lv_event_t *e)
{
    lv_scr_load(Settings_GetScreen());
}

void WiFiUI_Init(void)
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
    lv_label_set_text(tl, "WiFi 网络");
    lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(tl, 44, 12);

    /* 当前网络卡片 */
    lv_obj_t *card = lv_obj_create(s_scr);
    lv_obj_set_size(card, 224, 62);
    lv_obj_set_pos(card, 8, 44);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_ssid_lbl = lv_label_create(card);
    lv_label_set_text(s_ssid_lbl, WiFi_GetSSID());
    lv_obj_set_style_text_color(s_ssid_lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(s_ssid_lbl, 12, 6);

    s_state = lv_label_create(card);
    lv_label_set_text(s_state, "--");
    lv_obj_set_style_text_color(s_state, lv_color_hex(0x9CA3AF), LV_PART_MAIN);
    lv_obj_align(s_state, LV_ALIGN_TOP_RIGHT, -12, 6);

    s_ip_lbl = lv_label_create(card);
    lv_label_set_text_fmt(s_ip_lbl, "IP %s", Web_GetIP());
    lv_obj_set_style_text_color(s_ip_lbl, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_set_pos(s_ip_lbl, 12, 34);

    /* 扫描按钮 */
    s_scan_btn = lv_btn_create(s_scr);
    lv_obj_set_size(s_scan_btn, 224, 32);
    lv_obj_set_pos(s_scan_btn, 8, 114);
    lv_obj_set_style_bg_color(s_scan_btn, lv_color_hex(0x2563EB), LV_PART_MAIN);
    lv_obj_set_style_radius(s_scan_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_scan_btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(s_scan_btn, scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *sbl = lv_label_create(s_scan_btn);
    lv_label_set_text(sbl, "扫描附近热点");
    lv_obj_center(sbl);

    /* 热点列表（5 行） */
    for (int i = 0; i < AP_MAX; i++) {
        lv_obj_t *row = lv_btn_create(s_scr);
        lv_obj_set_size(row, 224, 30);
        lv_obj_set_pos(row, 8, 154 + i * 32);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1F2937), LV_PART_MAIN);
        lv_obj_set_style_radius(row, 6, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, "");
        lv_obj_set_style_text_color(l, lv_color_hex(0xE5E7EB), LV_PART_MAIN);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 10, 0);

        s_rows[i] = row;
        s_row_lbl[i] = l;
    }

    lv_timer_create(wifi_ui_timer, 500, NULL);
    ESP_LOGI(TAG, "WiFi 页就绪，当前: %s", WiFi_GetSSID());
}

lv_obj_t *WiFiUI_GetScreen(void)
{
    return s_scr;
}
