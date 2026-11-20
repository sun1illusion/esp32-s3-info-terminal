#include "ui_settings.h"

#include <stdio.h>
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_app_desc.h"
#include "driver/gpio.h"

#include "app_config.h"
#include "app_state.h"
#include "app_io.h"
#include "app_diag.h"
#include "board_backlight.h"
#include "net_wifi.h"
#include "ui_home.h"
#include "ui_wifi.h"
#include "font_zh.h"

static lv_obj_t *s_scr_settings = NULL;
static lv_obj_t *s_scr_serial   = NULL;
static lv_obj_t *s_scr_sysinfo  = NULL;
static lv_obj_t *s_scr_iomgr    = NULL;

static lv_obj_t *s_bright_val_lbl = NULL;
static lv_obj_t *s_vol_val_lbl    = NULL;
static lv_obj_t *s_wifi_lbl       = NULL;
static lv_obj_t *s_io_sw[APP_IO_COUNT] = {NULL};
static int       s_io_shown[APP_IO_COUNT];   /* 屏幕上当前显示的电平，用于避免重复刷样式 */

/* ---------------- 返回 ---------------- */

static void back_to_home_cb(lv_event_t *e)     { lv_scr_load(Home_GetScreen()); }
static void back_to_settings_cb(lv_event_t *e) { lv_scr_load(s_scr_settings); }

/* ---------------- 滑动条 ---------------- */

static void bright_cb(lv_event_t *e)
{
    int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    g_brightness = v;
    Backlight_Set(v);                     /* 实时改变背光 PWM */
    if (s_bright_val_lbl) {
        char b[8];
        snprintf(b, sizeof(b), "%d%%", v);
        lv_label_set_text(s_bright_val_lbl, b);
    }
}

static void vol_cb(lv_event_t *e)
{
    int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
    g_volume = v;                         /* 喇叭未接，先存值备用 */
    if (s_vol_val_lbl) {
        char b[8];
        snprintf(b, sizeof(b), "%d%%", v);
        lv_label_set_text(s_vol_val_lbl, b);
    }
}

/* 滑动条卡片 */
static void create_slider_card(lv_obj_t *parent, int y, const char *title, int init,
                               lv_event_cb_t cb, lv_obj_t **val_lbl_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 224, 54);
    lv_obj_set_pos(card, 8, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tl = lv_label_create(card);
    lv_label_set_text(tl, title);
    lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(tl, 12, 5);

    lv_obj_t *vl = lv_label_create(card);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", init);
    lv_label_set_text(vl, b);
    lv_obj_set_style_text_color(vl, lv_color_hex(0x93C5FD), LV_PART_MAIN);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, -12, 5);
    if (val_lbl_out) *val_lbl_out = vl;

    lv_obj_t *sl = lv_slider_create(card);
    lv_obj_set_size(sl, 200, 8);
    lv_obj_set_pos(sl, 12, 30);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, init, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x1E40AF), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 2, LV_PART_KNOB);
    lv_obj_set_style_border_width(sl, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(sl, cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* ---------------- 功能按钮 / 二级页框架 ---------------- */

static void create_func_btn(lv_obj_t *parent, int x, int y, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 54, 70);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);

    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_center(l);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
}

/* 二级页通用框架：背景 + 圆形返回键 + 标题 */
static lv_obj_t *create_sub_screen(const char *title)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0B1E3D), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, &myfont_16, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *back = lv_btn_create(scr);
    lv_obj_set_size(back, 30, 30);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_to_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_center(bl);

    lv_obj_t *tl = lv_label_create(scr);
    lv_label_set_text(tl, title);
    lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(tl, 44, 12);

    return scr;
}

/* ---------------- 三个二级页 ---------------- */

/* 串口状态：分节显示，固定行距，互不重叠 */
static void serial_section(lv_obj_t *scr, int y, const char *title, uint32_t color,
                           const char *d1, const char *d2)
{
    lv_obj_t *t = lv_label_create(scr);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_pos(t, 10, y);

    lv_obj_t *l1 = lv_label_create(scr);
    lv_label_set_text(l1, d1);
    lv_obj_set_style_text_color(l1, lv_color_hex(0xD1D5DB), LV_PART_MAIN);
    lv_obj_set_pos(l1, 10, y + 20);

    if (d2) {
        lv_obj_t *l2 = lv_label_create(scr);
        lv_label_set_text(l2, d2);
        lv_obj_set_style_text_color(l2, lv_color_hex(0xD1D5DB), LV_PART_MAIN);
        lv_obj_set_pos(l2, 10, y + 38);
    }
}

static void create_serial_ui(void)
{
    s_scr_serial = create_sub_screen("串口状态");

    serial_section(s_scr_serial, 44, "调试串口 UART0", 0xFBBF24,
                   "115200 8N1  TX43 RX44", NULL);

    char title[48];
    snprintf(title, sizeof(title), "屏幕 SPI2 %dMHz", (int)LCD_SPI_CLK_MHZ);
    serial_section(s_scr_serial, 92, title, 0x60A5FA,
                   "SCK21 MOSI40 MISO35", "CS39 DC38 RST37");

    serial_section(s_scr_serial, 158, "触摸 XPT2046 1MHz", 0x22C55E,
                   "CS36 IRQ47", NULL);

    serial_section(s_scr_serial, 206, "OLED I2C 400kHz", 0xC084FC,
                   "SCL42 SDA41", NULL);

    s_wifi_lbl = lv_label_create(s_scr_serial);
    lv_obj_set_style_text_color(s_wifi_lbl, lv_color_hex(0x22C55E), LV_PART_MAIN);
    lv_obj_set_pos(s_wifi_lbl, 10, 264);
    lv_label_set_text(s_wifi_lbl, "WiFi 未连接");
}

/* 系统信息：启动时读一次硬件信息 */
static void create_sysinfo_ui(void)
{
    s_scr_sysinfo = create_sub_screen("系统信息");

    esp_chip_info_t info;
    esp_chip_info(&info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    uint32_t free_heap = esp_get_free_heap_size();

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    const esp_app_desc_t *app = esp_app_get_description();

    char buf[400];
    snprintf(buf, sizeof(buf),
        "芯片: ESP32-S3\n"
        "核心: %d 核\n"
        "Flash: %u MB\n"
        "PSRAM: 8 MB 未启用\n"
        "可用内存: %u KB\n"
        "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
        "固件: %s\n"
        "编译: %s %s",
        (int)info.cores,
        (unsigned)(flash_size / (1024u * 1024u)),
        (unsigned)(free_heap / 1024u),
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        app->version, app->date, app->time);

    lv_obj_t *l = lv_label_create(s_scr_sysinfo);
    lv_obj_set_width(l, 224);
    lv_label_set_text(l, buf);
    lv_obj_set_style_text_color(l, lv_color_hex(0xD1D5DB), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(l, 2, LV_PART_MAIN);
    lv_obj_set_pos(l, 10, 42);
}

/* IO 口开关：点一下翻转电平（状态真身放在 app_io，手机网页也读写同一份） */
static void io_toggle_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= APP_IO_COUNT) return;

    AppIO_Toggle(idx);
    s_io_shown[idx] = AppIO_Get(idx);
    lv_obj_set_style_bg_color(s_io_sw[idx],
        lv_color_hex(s_io_shown[idx] ? 0x22C55E : 0x374151), LV_PART_MAIN);
}

/* 手机上改了 GPIO，屏幕这边 300ms 内跟着变色 */
static void io_sync_timer(lv_timer_t *t)
{
    for (int i = 0; i < APP_IO_COUNT; i++) {
        if (!s_io_sw[i]) continue;
        int lv = AppIO_Get(i);
        if (s_io_shown[i] == lv) continue;      /* 没变化就不动，省刷新开销 */
        s_io_shown[i] = lv;
        lv_obj_set_style_bg_color(s_io_sw[i],
            lv_color_hex(lv ? 0x22C55E : 0x374151), LV_PART_MAIN);
    }
}

static void create_iomgr_ui(void)
{
    s_scr_iomgr = create_sub_screen("IO口管理");

    for (int i = 0; i < APP_IO_COUNT; i++) {
        int x = 8 + (i % 2) * 114;
        int y = 48 + (i / 2) * 56;

        lv_obj_t *btn = lv_btn_create(s_scr_iomgr);
        lv_obj_set_size(btn, 108, 48);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_bg_color(btn,
            lv_color_hex(AppIO_Get(i) ? 0x22C55E : 0x374151), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(btn, io_toggle_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_io_sw[i] = btn;
        s_io_shown[i] = AppIO_Get(i);

        char name[16];
        snprintf(name, sizeof(name), "GPIO%d", app_io_pins[i]);
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, name);
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_center(l);
    }

    lv_timer_create(io_sync_timer, 300, NULL);
}

/* ---------------- 页面跳转 ---------------- */

static void to_serial_cb(lv_event_t *e)
{
    /* 进页面前刷新 WiFi 状态，避免显示过期 */
    if (s_wifi_lbl) {
        bool ok = WiFi_IsConnected();
        lv_label_set_text(s_wifi_lbl, ok ? "WiFi: 已连接" : "WiFi: 未连接");
        lv_obj_set_style_text_color(s_wifi_lbl,
            lv_color_hex(ok ? 0x22C55E : 0xEF4444), LV_PART_MAIN);
    }
    lv_scr_load(s_scr_serial);
}

static void to_sysinfo_cb(lv_event_t *e) { lv_scr_load(s_scr_sysinfo); }
static void to_iomgr_cb(lv_event_t *e)   { lv_scr_load(s_scr_iomgr); }
static void to_wifi_cb(lv_event_t *e)    { lv_scr_load(WiFiUI_GetScreen()); }

/* ---------------- 设置主页 ---------------- */

void Settings_Init(void)
{
    s_scr_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_settings, lv_color_hex(0x0B1E3D), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_scr_settings, &myfont_16, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr_settings, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_scr_settings, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *back = lv_btn_create(s_scr_settings);
    lv_obj_set_size(back, 30, 30);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_set_style_radius(back, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x1E3A6E), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(back, back_to_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text(bl, "<");
    lv_obj_center(bl);

    lv_obj_t *tl = lv_label_create(s_scr_settings);
    lv_label_set_text(tl, "rtos系统设置");
    lv_obj_set_style_text_color(tl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_pos(tl, 44, 12);

    create_slider_card(s_scr_settings, 44,  "亮度", g_brightness, bright_cb, &s_bright_val_lbl);
    create_slider_card(s_scr_settings, 104, "音量", g_volume,     vol_cb,    &s_vol_val_lbl);

    create_func_btn(s_scr_settings, 8,   168, "串口\n状态", to_serial_cb);
    create_func_btn(s_scr_settings, 65,  168, "系统\n信息", to_sysinfo_cb);
    create_func_btn(s_scr_settings, 122, 168, "IO口\n管理", to_iomgr_cb);
    create_func_btn(s_scr_settings, 179, 168, "WiFi",      to_wifi_cb);

    /* 二级页一起建好，切页只是换 screen（更快、且只分配一次内存） */
    create_serial_ui();
    create_sysinfo_ui();
    create_iomgr_ui();
    WiFiUI_Init();

    /* 字库自检：缺字只会渲染成空白框，这里主动扫出来报码点 */
    Diag_CheckGlyphs(s_scr_settings, "设置");
    Diag_CheckGlyphs(s_scr_serial,   "串口状态");
    Diag_CheckGlyphs(s_scr_sysinfo,  "系统信息");
    Diag_CheckGlyphs(s_scr_iomgr,    "IO口管理");
}

lv_obj_t *Settings_GetScreen(void)
{
    return s_scr_settings;
}
