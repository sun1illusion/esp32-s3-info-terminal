#include "board_oled.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state.h"
#include "net_weather.h"
#include "net_time.h"

static const char *TAG = "board_oled";

/* 0.91" SSD1306：128x32，4 个 page */
#define OLED_W      128
#define OLED_H      32
#define OLED_PAGES  (OLED_H / 8)

/* 右上角时间小字。觉得碍眼就改 0，重新编译即彻底不画。 */
#define OLED_SHOW_CLOCK 1

#define I2C_SCL_GPIO 42   /* 与原理图/接线一致：SCL=42, SDA=41 */
#define I2C_SDA_GPIO 41
#define OLED_ADDR1   0x3C
#define OLED_ADDR2   0x3D

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static bool s_present = false;

static uint8_t s_fb[OLED_W * OLED_PAGES];   /* 512 字节帧缓存 */

/* ================= 底层通信 ================= */

static esp_err_t oled_cmds(const uint8_t *cmds, size_t n)
{
    if (!s_dev || n > 30) return ESP_ERR_INVALID_STATE;

    uint8_t buf[32];
    buf[0] = 0x00;                  /* 控制字节：后续都是命令 */
    memcpy(buf + 1, cmds, n);
    return i2c_master_transmit(s_dev, buf, n + 1, 100);
}

static esp_err_t oled_data(const uint8_t *data, size_t n)
{
    if (!s_dev) return ESP_ERR_INVALID_STATE;

    /* SSD1306 的 I2C 数据包格式：1 字节控制字 0x40 + 最多 128 字节显存数据。
     * 以前这里是一次性 memcpy 进 129 字节的栈缓冲，
     * 而 n = sizeof(s_fb) = 512，直接溢出 384 字节踩爆栈
     * （表现为 Double exception / Backtrace CORRUPTED）。
     * 必须按 OLED_W 分块发。 */
    uint8_t buf[OLED_W + 1];
    buf[0] = 0x40;

    esp_err_t err = ESP_OK;
    while (n > 0) {
        size_t chunk = (n > OLED_W) ? OLED_W : n;
        memcpy(buf + 1, data, chunk);
        err = i2c_master_transmit(s_dev, buf, chunk + 1, 200);
        if (err != ESP_OK) return err;
        data += chunk;
        n    -= chunk;
    }
    return err;
}

static void oled_flush(void)
{
    static const uint8_t win[] = {
        0x21, 0x00, OLED_W - 1,     /* 列范围 0..127 */
        0x22, 0x00, OLED_PAGES - 1, /* 页范围 0..3 */
    };
    oled_cmds(win, sizeof(win));
    oled_data(s_fb, sizeof(s_fb));
}

/* ================= 绘图原语 ================= */

static void px(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    uint16_t i = (uint16_t)(y >> 3) * OLED_W + x;
    uint8_t m = 1 << (y & 7);
    if (on) s_fb[i] |= m; else s_fb[i] &= ~m;
}

static void fill_rect(int x, int y, int w, int h, bool on)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            px(x + i, y + j, on);
}

static void rect_outline(int x, int y, int w, int h)
{
    for (int i = 0; i < w; i++) { px(x + i, y, true); px(x + i, y + h - 1, true); }
    for (int j = 0; j < h; j++) { px(x, y + j, true); px(x + w - 1, y + j, true); }
}

static void fill_circle(int cx, int cy, int r)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            if (x * x + y * y <= r * r) px(cx + x, cy + y, true);
        }
    }
}

static void circle_outline(int cx, int cy, int r)
{
    for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d <= r * r && d >= (r - 1) * (r - 1)) px(cx + x, cy + y, true);
        }
    }
}

static void hline(int x, int y, int w)   { fill_rect(x, y, w, 1, true); }
static void vline(int x, int y, int h)   { fill_rect(x, y, 1, h, true); }

/* ================= 数字 ================= */

/* 3x5 小数字（14 段风格：每行 3 bit） */
static const uint8_t s_num3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7},  /* 0 */
    {0x2, 0x6, 0x2, 0x2, 0x7},  /* 1 */
    {0x7, 0x1, 0x7, 0x4, 0x7},  /* 2 */
    {0x7, 0x1, 0x7, 0x1, 0x7},  /* 3 */
    {0x5, 0x5, 0x7, 0x1, 0x1},  /* 4 */
    {0x7, 0x4, 0x7, 0x1, 0x7},  /* 5 */
    {0x7, 0x4, 0x7, 0x5, 0x7},  /* 6 */
    {0x7, 0x1, 0x1, 0x1, 0x1},  /* 7 */
    {0x7, 0x5, 0x7, 0x5, 0x7},  /* 8 */
    {0x7, 0x5, 0x7, 0x1, 0x7},  /* 9 */
};

static int small_num(int x, int y, int value, int digits)
{
    if (value < 0) value = 0;
    int div = 1;
    for (int i = 1; i < digits; i++) div *= 10;

    for (int k = 0; k < digits; k++) {
        int d = (value / div) % 10;
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 3; c++) {
                if (s_num3x5[d][r] & (4 >> c)) px(x + c, y + r, true);
            }
        }
        x += 4;
        div /= 10;
    }
    return x;
}

/* 7 段大数字：宽 10、高 18、笔画 2 */
#define SEG_A 0x01
#define SEG_B 0x02
#define SEG_C 0x04
#define SEG_D 0x08
#define SEG_E 0x10
#define SEG_F 0x20
#define SEG_G 0x40

static const uint8_t s_seg_mask[10] = {
    63, 6, 91, 79, 102, 109, 125, 7, 127, 111
};

static int big_digit(int x, int y, int d)
{
    if (d < 0 || d > 9) return x + 12;
    uint8_t m = s_seg_mask[d];

    if (m & SEG_A) fill_rect(x + 2, y,      6, 2, true);
    if (m & SEG_B) fill_rect(x + 8, y + 2,  2, 7, true);
    if (m & SEG_C) fill_rect(x + 8, y + 9,  2, 7, true);
    if (m & SEG_D) fill_rect(x + 2, y + 16, 6, 2, true);
    if (m & SEG_E) fill_rect(x,     y + 9,  2, 7, true);
    if (m & SEG_F) fill_rect(x,     y + 2,  2, 7, true);
    if (m & SEG_G) fill_rect(x + 2, y + 8,  6, 2, true);

    return x + 11;
}

/* ================= 天气图标（32x32） ================= */

static void icon_sun(int cx, int cy, int r)
{
    fill_circle(cx, cy, r);
    for (int k = 0; k < 8; k++) {
        static const int8_t dx[8] = { 0, 1, 1, 1, 0, -1, -1, -1};
        static const int8_t dy[8] = {-1, -1, 0, 1, 1,  1,  0, -1};
        for (int s = r + 2; s <= r + 4; s++) px(cx + dx[k] * s, cy + dy[k] * s, true);
    }
}

static void icon_cloud(int x, int y, int w)
{
    /* 三个圆 + 底座，形成蓬松云 */
    fill_circle(x + w / 4,     y + 8, w / 5);
    fill_circle(x + w / 2,     y + 6, w / 4);
    fill_circle(x + w * 3 / 4, y + 9, w / 5);
    fill_rect(x + 2, y + 9, w - 4, w / 4, true);
}

static void icon_rain_drops(int x, int y)
{
    for (int i = 0; i < 3; i++) {
        int dx = x + 6 + i * 7;
        for (int j = 0; j < 5; j++) {
            px(dx, y + j, true);
            if (j >= 2) px(dx - 1, y + j, true);
        }
    }
}

static void icon_snow(int x, int y)
{
    for (int i = 0; i < 3; i++) {
        fill_circle(x + 8 + i * 7, y + 3, 1);
    }
}

static void icon_bolt(int x, int y)
{
    /* 简单闪电：两段斜线 */
    for (int i = 0; i < 5; i++) { px(x + 10 - i / 2, y + i, true); px(x + 9 - i / 2, y + i, true); }
    for (int i = 0; i < 5; i++) { px(x + 6 + i / 2, y + 5 + i, true); px(x + 5 + i / 2, y + 5 + i, true); }
}

static void draw_weather_icon(int code)
{
    if (code < 0) {                     /* 还没数据 */
        rect_outline(2, 2, 28, 28);
        return;
    }
    if (code == 0 || code == 1) {
        icon_sun(16, 15, 7);
    } else if (code == 2) {
        icon_sun(11, 10, 5);
        icon_cloud(4, 14, 26);
    } else if (code == 3) {
        icon_cloud(3, 12, 27);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        icon_cloud(3, 6, 27);
        icon_rain_drops(4, 20);
    } else if (code >= 71 && code <= 77) {
        icon_cloud(3, 6, 27);
        icon_snow(4, 22);
    } else if (code >= 95) {
        icon_cloud(3, 4, 27);
        icon_bolt(8, 18);
    } else {                            /* 雾/霾 */
        icon_cloud(4, 4, 24);
        hline(3, 22, 26);
        hline(6, 26, 20);
    }
}

/* ================= 组帧 ================= */

/* 3x5 数字按整数倍放大（scale=2 时字高 10px）。
 * 返回最后一个字形的右边界（不含尾部间隔），方便接冒号。 */
static int small_num_scaled(int x, int y, int value, int digits, int scale)
{
    if (value < 0) value = 0;
    int div = 1;
    for (int i = 1; i < digits; i++) div *= 10;

    for (int k = 0; k < digits; k++) {
        int d = (value / div) % 10;
        for (int r = 0; r < 5; r++) {
            for (int c = 0; c < 3; c++) {
                if (s_num3x5[d][r] & (4 >> c)) {
                    fill_rect(x + c * scale, y + r * scale, scale, scale, true);
                }
            }
        }
        x += 4 * scale;
        div /= 10;
    }
    return x - scale;
}

/* 右上角 HH:MM。
 * 2 倍放大后总宽 34px、字高 10px，右对齐到 x=123，起点 90 —— 
 * 温度区在 x ≤ 78、底部风力线在 y ≥ 24，两边都不碰。
 * 还没对上时间就什么都不画，右上角保持空白。 */
static void draw_clock(void)
{
    int hh, mm;
    if (!Time_GetHM(&hh, &mm)) return;

    const int sc = 2;
    const int dw = 3 * sc;                      /* 单数字宽 6 */
    const int gap = sc;                         /* 数字间隔 2 */
    const int total = dw * 4 + gap * 4 + sc;    /* 4 数字 + 4 间隔 + 冒号 2px = 34 */

    int x = OLED_W - 4 - total;                 /* 右对齐，右侧留 4px */
    const int y = 4;

    x = small_num_scaled(x, y, hh, 2, sc) + gap;
    fill_rect(x, y + 2, sc, sc, true);          /* 冒号上点 */
    fill_rect(x, y + 6, sc, sc, true);          /* 冒号下点 */
    x += sc + gap;
    small_num_scaled(x, y, mm, 2, sc);
}

static void build_frame(void)
{
    memset(s_fb, 0, sizeof(s_fb));

    draw_weather_icon(g_weather_code);
#if OLED_SHOW_CLOCK
    draw_clock();
#endif

    if (g_weather_code < 0) {
        /* 无数据：显示两条横线 */
        fill_rect(40, 10, 16, 2, true);
        fill_rect(40, 18, 16, 2, true);
        fill_rect(40, 26, 30, 2, true);
        return;
    }

    /* --- 气温（7 段大数字） --- */
    float t = g_temp_f;
    bool neg = t < -0.05f;
    if (neg) t = -t;

    int t10 = (int)lroundf(t * 10.0f);
    int whole = t10 / 10;
    int dec   = t10 % 10;
    if (whole > 99) whole = 99;

    int x = 36;
    if (neg) { fill_rect(x, 10, 5, 2, true); x += 7; }
    if (whole >= 10) x = big_digit(x, 1, whole / 10);
    x = big_digit(x, 1, whole % 10);
    fill_rect(x, 17, 2, 2, true);       /* 小数点 */
    x += 4;
    x = big_digit(x, 1, dec);
    circle_outline(x + 2, 4, 2);        /* 摄氏度符号 */

    /* --- 底部：湿度水滴 + 风力 --- */
    int by = 24;

    /* 水滴 */
    fill_circle(38, by + 3, 3);
    px(38, by - 2, true); px(37, by - 1, true); px(39, by - 1, true);

    int hv = (int)(g_hum_f + 0.5f);
    if (hv > 99) hv = 99;
    x = small_num(45, by, hv, 2);
    /* 百分号 */
    px(x + 1, by, true); px(x + 2, by + 1, true); px(x + 1, by + 4, true); px(x + 3, by + 3, true);

    /* 风力：三条线 + 等级数字 */
    int wx = 78;
    hline(wx, by, 10);
    hline(wx + 2, by + 3, 12);
    hline(wx, by + 6, 8);
    small_num(wx + 16, by + 1, Weather_WindLevel(g_wind_f), 2);
}

/* ================= 刷新任务 ================= */

static void oled_task(void *arg)
{
    int last_code = -999;
    int last_t = -9999, last_h = -1, last_w = -1;
    int last_min = -1;

    while (1) {
        int code = g_weather_code;
        int t = (int)(g_temp_f * 10);
        int h = (int)(g_hum_f + 0.5f);
        int w = Weather_WindLevel(g_wind_f);

        /* 分钟变化也要重绘一次，否则时间会一直停在开机那一刻 */
        int minute = -1;
#if OLED_SHOW_CLOCK
        int hh, mm;
        if (Time_GetHM(&hh, &mm)) minute = hh * 60 + mm;
#endif

        if (code != last_code || t != last_t || h != last_h || w != last_w
            || minute != last_min) {
            last_code = code; last_t = t; last_h = h; last_w = w;
            last_min = minute;
            build_frame();
            oled_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ================= 初始化 ================= */

void Oled_Init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    if (i2c_new_master_bus(&bus_cfg, &s_bus) != ESP_OK) {
        ESP_LOGW(TAG, "I2C 总线初始化失败，跳过 OLED");
        return;
    }

    /* 探测 0x3C / 0x3D。
     * 引脚刚被重配置后总线电容还没被上拉充满，首次 probe 可能超时，
     * 所以每个地址重试 3 次 —— 不把"慢一拍"误判成"没设备"。 */
    int addr = 0;
    const uint16_t cand[] = { OLED_ADDR1, OLED_ADDR2 };
    for (int i = 0; i < 2 && addr == 0; i++) {
        for (int r = 0; r < 3; r++) {
            if (i2c_master_probe(s_bus, cand[i], 50) == ESP_OK) { addr = cand[i]; break; }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (addr == 0) {
        /* 兜底全扫描：既能诊断接线，也能把非标准地址的模块救回来。
         * 注意不能再像以前那样扫到了就 return，必须拿扫描结果继续初始化。 */
        char found[64];
        int  n = 0;
        found[0] = 0;
        for (int a = 0x08; a <= 0x77; a++) {
            if (i2c_master_probe(s_bus, (uint16_t)a, 20) == ESP_OK) {
                if (addr == 0) addr = a;        /* 用第一个应答者继续走初始化 */
                n += snprintf(found + n, sizeof(found) - n, "0x%02X ", a);
                if (n > (int)sizeof(found) - 8) break;
            }
        }
        if (n == 0) {
            ESP_LOGW(TAG, "I2C 总线无任何设备：检查 SCL=GPIO42 / SDA=GPIO41 接线与 OLED 供电");
            return;
        }
        ESP_LOGW(TAG, "I2C 扫描结果: %s（改用 0x%02X 初始化）", found, addr);
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev) != ESP_OK) {
        ESP_LOGW(TAG, "添加 OLED 设备失败");
        return;
    }

    static const uint8_t init_seq[] = {
        0xAE,               /* 关显示 */
        0xD5, 0x80,         /* 时钟分频 */
        0xA8, 0x1F,         /* 1/32 复用（128x32 专用） */
        0xD3, 0x00,
        0x40,               /* 起始行 0 */
        0x8D, 0x14,         /* 电荷泵开 */
        0x20, 0x00,         /* 水平寻址 */
        0xA1,               /* 列重映射 */
        0xC8,               /* 行反向扫描 */
        0xDA, 0x02,         /* COM 引脚配置（128x32） */
        0x81, 0x8F,         /* 对比度 */
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,               /* 跟随显存 */
        0xA6,               /* 正常显示 */
        0x2E,               /* 关滚动 */
        0xAF,               /* 开显示 */
    };
    if (oled_cmds(init_seq, sizeof(init_seq)) != ESP_OK) {
        ESP_LOGW(TAG, "OLED 初始化命令失败");
        return;
    }

    s_present = true;
    memset(s_fb, 0, sizeof(s_fb));
    oled_flush();

    ESP_LOGI(TAG, "OLED 就绪（0x%02X, 128x32）", addr);

    if (xTaskCreate(oled_task, "oled", 2560, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "OLED 任务创建失败");
    }
}

bool Oled_IsPresent(void)
{
    return s_present;
}
