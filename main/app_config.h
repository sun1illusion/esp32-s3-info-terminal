#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ================= 引脚定义 ================= */
#define PIN_SCK         21
#define PIN_MOSI        40
#define PIN_MISO        35
#define PIN_LCD_CS      39
#define PIN_LCD_DC      38
#define PIN_LCD_RST     37
#define PIN_TOUCH_CS    36
#define PIN_TOUCH_IRQ   47

/* 背光 PWM（需把屏幕 LED 引脚从 3V3 改接到 GPIO1 才能真实调光）
 * 选 GPIO1 的理由：右侧排针、普通 IO、非 strapping、不占 USB(D-/D+=19/20)、非板载灯(48) */
#define PIN_LCD_BL      1

/* ================= 屏幕参数 ================= */
#define LCD_H_RES       240
#define LCD_V_RES       320

/* 屏幕 SPI 时钟：40MHz（帧率关键）。若花屏/白屏，依次退到 26MHz → 20MHz */
#define LCD_SPI_CLK_HZ  (40 * 1000 * 1000)
#define LCD_SPI_CLK_MHZ (LCD_SPI_CLK_HZ / 1000000)
#define LCD_BUF_LINES   40      /* LVGL 绘图缓冲行数（双缓冲共 240*40*2*2 = 37.5KB）
                                 * 从 60 行降下来可回收 18.75KB RAM，代价是 flush 块数 6→8 */

/* 触摸 ADC 有效区间（四点校准） */
#define CAL_X_MIN       25
#define CAL_X_MAX       215
#define CAL_Y_MIN       25
#define CAL_Y_MAX       295

/* ================= 网络 ================= */
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASS           "YOUR_WIFI_PASSWORD"

/* Open-Meteo 天气（当前 + 未来 4 天，杭州） */
#define WEATHER_URL     "https://api.open-meteo.com/v1/forecast?latitude=30.2741&longitude=120.1552" \
                        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code," \
                        "wind_speed_10m,wind_direction_10m&daily=weather_code,temperature_2m_max," \
                        "temperature_2m_min&timezone=Asia%2FShanghai&forecast_days=4"

/* ================= 全局默认值 ================= */
#define DEFAULT_BRIGHTNESS  80
#define DEFAULT_VOLUME      50

/* ================= 手机网页访问密钥 =================
 * 非空时：所有 api 接口必须带 ?k=<WEB_TOKEN>，否则返回 401。
 * 置为 "" 则不校验（仅建议在完全可信的局域网里这么用）。
 * 注意：这只是防误触/防同网段路人，不是强加密，别用重要密码。 */
#define WEB_TOKEN           "CHANGE_ME_TOKEN"

/* ================= 硬件排查开关 =================
 * 1 = 启动时对 OLED 的 SCL/SDA 做一次引脚完好性自检（会占用约 40ms，串口打印结果）
 * 0 = 关闭。排查完务必改回 0。
 * 判定看串口日志 [gpiotest]：
 *   "引脚正常" + 释放读=1  -> 引脚好、外部上拉好、模块 LDO 有输出（链路全通）
 *   "引脚正常" + 释放读=0  -> 引脚好但外部无上拉（模块没供电 / 上拉断 / 线没接）
 *   "引脚异常"             -> 该 GPIO 大概率已被击穿，需换脚
 */
#define APP_GPIO_SELFTEST   0
#define PIN_SELFTEST_SCL    42
#define PIN_SELFTEST_SDA    41

/* ================= 音频 =================
 * 0 = 不初始化 I2S（音乐功能暂停，释放约 6KB DMA 内存）
 * 1 = 开机播三声测试音（A4-C5-E5）验证喇叭链路
 * 注：Board/board_audio.c 保留为已验证的基础设施，不调用即零占用。 */
#define APP_AUDIO_TEST_TONE 0

/* ================= 帧率探针 =================
 * 1 = 启动时铺一张全屏矩形做颜色循环，强制整屏重绘，每秒打印 fps
 *     （屏幕会被彩色覆盖，属正常，用于对比不同 LCD_BUF_LINES 的帧率）
 * 0 = 关闭。测完务必改回 0。 */
#define APP_FPS_PROBE       0

#endif
