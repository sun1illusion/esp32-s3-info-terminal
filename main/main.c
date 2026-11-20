#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"

#include "app_config.h"
#include "app_state.h"
#include "app_diag.h"
#include "app_io.h"
#include "app_screen.h"
#include "app_ota.h"
#include "board_lcd.h"
#include "board_touch.h"
#include "board_backlight.h"
#include "board_oled.h"
#include "board_audio.h"
#include "net_wifi.h"
#include "net_time.h"
#include "net_weather.h"
#include "net_web.h"
#include "ui_home.h"
#include "ui_weather.h"
#include "ui_settings.h"
#include "ui_net.h"
#include "ui_sysmon.h"

static const char *TAG = "app";

void app_main(void)
{
    nvs_flash_init();

    /* --- 硬件排查（仅 APP_GPIO_SELFTEST=1 时编译进来）
     * 必须在 Oled_Init 之前跑：一旦 I2C 驱动接管引脚，就不能再随手改配置了 */
#if APP_GPIO_SELFTEST
    Diag_GpioSelfTest(PIN_SELFTEST_SDA);
    Diag_GpioSelfTest(PIN_SELFTEST_SCL);
#endif

    /* --- 硬件层 --- */
    LCD_Init();                 /* SPI2 总线 + ILI9341 + LVGL 移植层 */
    Touch_Init();               /* XPT2046 + 注册为 LVGL 输入设备 */
    Backlight_Init();
    Backlight_Set(g_brightness);
    AppIO_Init();               /* 可远程控制的 8 个 GPIO */
    Oled_Init();                /* 0.91" 小屏：常亮天气图标 + 气温 */

#if APP_AUDIO_TEST_TONE
    /* 喇叭链路验证：A4-C5-E5 三声递增音。
     * 听到即证明 I2S 时序、MAX98357A 的 SD 使能、功放供电全部正常。
     * 静音且无报错，优先查 SD 脚（必须为高/悬空）和 VIN（需 5V）。 */
    if (Audio_Init() == ESP_OK) {
        Audio_SetVolume(70);
        Audio_PlayTone(440, 250);
        vTaskDelay(pdMS_TO_TICKS(80));
        Audio_PlayTone(554, 250);
        vTaskDelay(pdMS_TO_TICKS(80));
        Audio_PlayTone(659, 400);
    }
#endif

    /* --- 界面层 ---
     * LVGL 不是线程安全的，LVGL 任务已在跑，建界面必须持锁，
     * 否则并发操作 lv_malloc 的堆会踩坏内存（表现为随机 StoreProhibited）。 */
    Diag_MemReport("建界面前");
    lvgl_port_lock(0);
    Home_Init();
    WeatherUI_Init();
    Settings_Init();
    NetUI_Init();
    SysMon_Init();              /* 系统运行状态页（RAM/Flash 环 + 任务列表） */
    lvgl_port_unlock();
    Diag_MemReport("建界面后");

    /* --- 网络层 --- */
    WiFi_Init();
    Time_Init();                /* SNTP 对时：挂到 got_ip 事件上，连上才真正启动 */
    Web_Init();                 /* 手机网页控制（HTTP 80 端口） */
    Weather_TaskStart();        /* 开机自动取一次天气（任务内部会等 WiFi 连上） */

    /* OTA 状态检查：若是刚升级上来的新固件（PENDING_VERIFY），
     * 30 秒后自动确认；期间崩溃则 bootloader 下次启动回滚到旧分区。 */
    Ota_Init();

    /* 每 10 分钟打一次内存：水位是历史最低值，采样稀疏不会漏掉峰值，
     * 只是发现得晚一些。日常看实时数据用「系统监控」页即可。 */
    Diag_StartPeriodic(10 * 60 * 1000);

    /* 30 秒无触摸关背光，触摸即亮。小屏 OLED 不受影响（常亮） */
    ScreenPower_Init(30 * 1000);

#if APP_FPS_PROBE
    Diag_StartFpsProbe();            /* 帧率对比用，测完把 APP_FPS_PROBE 置 0 */
#endif

    ESP_LOGI(TAG, "看板就绪");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
