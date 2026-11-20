#include "app_screen.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "board_backlight.h"
#include "board_touch.h"
#include "app_state.h"

static const char *TAG = "app_screen";

static uint32_t s_idle_ms = 30000;
static bool     s_off     = false;
static uint32_t s_seen_ms = 0;      /* 上一轮看到的时间戳，用来判断"有没有新触摸" */

static void idle_check_cb(lv_timer_t *t)
{
    (void)t;

    uint32_t last = Touch_GetLastPressMs();

    if (s_off) {
        /* 时间戳变化 = 用户碰了屏。
         * 用"变化"而不是"当前是否按下"，是因为短按可能刚好落在两次轮询之间。 */
        if (last != s_seen_ms) {
            /* 亮度被设成 0 时给个兜底值，否则"亮了但看不见"会以为死机 */
            Backlight_Set(g_brightness > 0 ? g_brightness : 50);
            s_off = false;
            ESP_LOGI(TAG, "触摸唤醒，恢复亮度");
        }
    } else if (lv_tick_elaps(last) > s_idle_ms) {
        Backlight_Set(0);
        s_off = true;
        ESP_LOGI(TAG, "%u 秒无操作，关闭背光", (unsigned)(s_idle_ms / 1000));
    }

    s_seen_ms = last;
}

void ScreenPower_Init(uint32_t idle_ms)
{
    if (idle_ms < 5000) idle_ms = 5000;   /* 太短会误息屏 */
    s_idle_ms = idle_ms;
    s_seen_ms = Touch_GetLastPressMs();

    lvgl_port_lock(0);
    lv_timer_create(idle_check_cb, 1000, NULL);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "息屏管理就绪：%u 秒无触摸关闭背光，触摸即唤醒",
             (unsigned)(idle_ms / 1000));
}

bool ScreenPower_IsOff(void)
{
    return s_off;
}
