#include "board_backlight.h"

#include "esp_err.h"
#include "driver/ledc.h"

#include "app_config.h"

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CH      LEDC_CHANNEL_0
#define BL_LEDC_MODE    LEDC_LOW_SPEED_MODE
#define BL_PWM_FREQ_HZ  20000           /* 20kHz：高于人耳可听范围，避免背光啸叫 */
#define BL_DUTY_MAX     ((1 << 10) - 1) /* 10 位分辨率 */

void Backlight_Init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = BL_LEDC_MODE,
        .timer_num = BL_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = BL_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&t));

    ledc_channel_config_t c = {
        .gpio_num = PIN_LCD_BL,
        .speed_mode = BL_LEDC_MODE,
        .channel = BL_LEDC_CH,
        .timer_sel = BL_LEDC_TIMER,
        .duty = BL_DUTY_MAX,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

void Backlight_Set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)BL_DUTY_MAX * percent / 100;
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CH, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CH);
}
