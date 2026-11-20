#include "board_lcd.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "esp_lvgl_port.h"

#include "app_config.h"

static const char *TAG = "board_lcd";

/* 刷新完成计数：LVGL 每次把所有脏区画完并 flush 结束会发 REFR_READY。
 * 为什么不用 SPI 的 on_color_trans_done：那个回调已被 esp_lvgl_port
 * 注册用于同步 flush，重复注册会把它的覆盖掉，直接导致花屏。 */
static volatile uint32_t s_refr_cnt = 0;

static void on_refr_ready(lv_event_t *e)
{
    (void)e;
    s_refr_cnt++;
}

uint32_t LCD_GetRefrCount(void)
{
    return s_refr_cnt;
}

void LCD_Init(void)
{
    ESP_LOGI(TAG, "初始化 SPI2 总线");
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "初始化 ILI9341 面板");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = PIN_LCD_CS,
        .dc_gpio_num = PIN_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_SPI_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,   /* 官方示例即 BGR，设成 RGB 会红蓝互换 */
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    ESP_LOGI(TAG, "初始化 LVGL 移植层");
    /* LVGL 任务绑到 CPU1，WiFi/协议栈主要在 CPU0，避免抢 CPU */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = 5;
    lvgl_cfg.task_affinity = 1;
    lvgl_cfg.task_max_sleep_ms = 10;
    lvgl_cfg.timer_period_ms = 5;
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel,
        .buffer_size = LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = true },
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    if (disp) {
        lv_display_add_event_cb(disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    }
}
