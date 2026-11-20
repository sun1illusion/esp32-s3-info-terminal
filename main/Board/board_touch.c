#include "board_touch.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_xpt2046.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "app_config.h"

static const char *TAG = "board_touch";

static esp_lcd_touch_handle_t touch_handle = NULL;

/* 最后一次「按下」的时刻（lv_tick 毫秒）。
 * 息屏逻辑靠它判断空闲时长：轮询读取时顺手打时间戳，
 * 比另外注册中断或事件回调更省，也不会漏掉短按。 */
static volatile uint32_t s_last_press_ms = 0;

uint32_t Touch_GetLastPressMs(void)
{
    return s_last_press_ms;
}

/* ADC 原始值 → 屏幕像素（线性校准：电阻屏四周有死区，直接满量程映射会导致边缘到不了） */
static void calibrate_touch(int *x, int *y)
{
    int cx = (*x - CAL_X_MIN) * LCD_H_RES / (CAL_X_MAX - CAL_X_MIN);
    int cy = (*y - CAL_Y_MIN) * LCD_V_RES / (CAL_Y_MAX - CAL_Y_MIN);
    if (cx < 0) cx = 0;
    if (cx >= LCD_H_RES) cx = LCD_H_RES - 1;
    if (cy < 0) cy = 0;
    if (cy >= LCD_V_RES) cy = LCD_V_RES - 1;
    *x = cx;
    *y = cy;
}

/* LVGL 输入设备读取回调（由 LVGL 任务周期调用） */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x[1], y[1], strength[1];
    uint8_t cnt = 0;

    if (esp_lcd_touch_read_data(touch_handle) == ESP_OK) {
        bool pressed = esp_lcd_touch_get_coordinates(touch_handle, x, y, strength, &cnt, 1);
        if (pressed && cnt > 0) {
            int cal_x = x[0], cal_y = y[0];
            calibrate_touch(&cal_x, &cal_y);
            data->point.x = cal_x;
            data->point.y = cal_y;
            data->state = LV_INDEV_STATE_PRESSED;
            s_last_press_ms = lv_tick_get();   /* 供息屏逻辑判断空闲时长 */
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void Touch_Init(void)
{
    ESP_LOGI(TAG, "初始化 XPT2046");

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config = {
        .cs_gpio_num = PIN_TOUCH_CS,
        .dc_gpio_num = -1,
        .spi_mode = 0,
        .pclk_hz = 1 * 1000 * 1000,
        .trans_queue_depth = 5,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = LCD_H_RES,
        .y_max = LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = PIN_TOUCH_IRQ,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 0, .mirror_y = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &touch_cfg, &touch_handle));

    /* 注册为 LVGL 指针输入设备
     * 注意：LVGL 任务此刻已在跑，任何 LVGL API 调用都必须持锁，
     * 否则和 LVGL 任务并发操作 lv_malloc 的堆会把 TLSF 元数据写坏。 */
    lvgl_port_lock(0);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lvgl_port_unlock();

    /* 初始化时间戳：否则开机后 30 秒就会因为"从没摸过"而直接息屏 */
    s_last_press_ms = lv_tick_get();
}
