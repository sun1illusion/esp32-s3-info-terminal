#include "app_diag.h"
#include "app_config.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

/* 低于这两个阈值就告警：留出一次 TLS 握手的余量 */
#define DIAG_WARN_FREE_KB   20
#define DIAG_WARN_BLOCK_KB  16

static void diag_log(const char *stage)
{
    size_t free_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t low      = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    /* LVGL 池（lvgl_mux 是递归锁，即使调用方已持锁也安全） */
    lv_mem_monitor_t mon;
    lvgl_port_lock(0);
    lv_mem_monitor(&mon);
    lvgl_port_unlock();

    /* 峰值 = LVGL 池历史最大占用（lv_mem_monitor 的 max_used）。
     * 它才是判断池能不能缩小的依据：free 只是"此刻"的余量，
     * 而页面切换、动画、滚动都会把峰值推高，看 free 会低估风险。 */
    ESP_LOGI("diag", "[%-10s] RAM free=%uKB 最大块=%uKB 水位=%uKB | LVGL池 free=%uKB 峰值=%uKB/%uKB 碎片%u%% | 栈余=%uB",
             stage,
             (unsigned)(free_int / 1024),
             (unsigned)(largest / 1024),
             (unsigned)(low / 1024),
             (unsigned)(mon.free_size / 1024),
             (unsigned)(mon.max_used / 1024),
             (unsigned)(mon.total_size / 1024),
             (unsigned)mon.frag_pct,
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    if (free_int / 1024 < DIAG_WARN_FREE_KB || largest / 1024 < DIAG_WARN_BLOCK_KB) {
        ESP_LOGW("diag", "内存偏紧：free=%uKB 最大块=%uKB（低于阈值 %u/%uKB），新增功能前先瘦身",
                 (unsigned)(free_int / 1024), (unsigned)(largest / 1024),
                 DIAG_WARN_FREE_KB, DIAG_WARN_BLOCK_KB);
    }
}

void Diag_MemReport(const char *stage)
{
    diag_log(stage);
}

/* 长跑监控：每 period_ms 打一次，用来看有没有缓慢泄漏 */
static void diag_timer_cb(lv_timer_t *t)
{
    diag_log("周期");
}

void Diag_StartPeriodic(uint32_t period_ms)
{
    lvgl_port_lock(0);
    lv_timer_create(diag_timer_cb, period_ms, NULL);
    lvgl_port_unlock();
}

/* ================= 字库自检 =================
 *
 * 为什么要运行时查：LVGL 遇到字库里没有的字符不会报错，直接画成空白框，
 * 只有肉眼看到才发现，而那时候已经烧录验证过一轮了。
 * 这里遍历对象树上的每个 lv_label，逐字符问 lv_font_get_glyph_dsc，
 * 缺了就打印码点，一眼能定位该补哪个字。 */

/* 极简 UTF-8 解码：只处理合法的 1~4 字节序列，非法字节跳过 */
static const char *utf8_next(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c = p[0];

    if (c < 0x80) {
        *cp = c;
        return s + 1;
    }
    if ((c & 0xE0) == 0xC0) {
        *cp = ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        return s + 2;
    }
    if ((c & 0xF0) == 0xE0) {
        *cp = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        return s + 3;
    }
    if ((c & 0xF8) == 0xF0) {
        *cp = ((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12)
            | ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        return s + 4;
    }
    *cp = '?';
    return s + 1;
}

static void scan_label(lv_obj_t *obj, const char *page, int *missing)
{
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);

        if (txt && font) {
            const char *p = txt;
            while (*p) {
                uint32_t cp;
                p = utf8_next(p, &cp);

                if (cp < 0x20) continue;          /* 换行等控制符不参与渲染 */

                lv_font_glyph_dsc_t dsc;
                if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
                    ESP_LOGE("font", "【%s】缺字 U+%04X", page, (unsigned)cp);
                    (*missing)++;
                }
            }
        }
    }

    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n; i++) {
        scan_label(lv_obj_get_child(obj, i), page, missing);
    }
}

void Diag_CheckGlyphs(lv_obj_t *root, const char *page)
{
    if (!root) return;

    int missing = 0;
    scan_label(root, page, &missing);

    if (missing > 0) {
        ESP_LOGE("font", "【%s】共 %d 个字形缺失，执行 tools/regen_font.py 重新生成",
                 page, missing);
    } else {
        ESP_LOGI("font", "【%s】字库完整", page);
    }
}

/* ================= 帧率探针（验证用，APP_FPS_PROBE=1 才编译） =================
 *
 * 静态界面不会触发刷新，直接读计数永远是 0。所以这里铺一张全屏矩形做颜色循环，
 * 强制每一帧都整屏重绘 —— 测到的是刷屏上限，而不是某个小部件的局部刷新。
 * 测试期间屏幕会被彩色覆盖，属预期行为，测完把 APP_FPS_PROBE 置 0 即消失。 */
#if APP_FPS_PROBE

#include "board_lcd.h"

static uint32_t s_fps_last_cnt  = 0;
static uint32_t s_fps_last_tick = 0;

static void probe_color_anim(void *var, int32_t v)
{
    lv_obj_set_style_bg_color((lv_obj_t *)var, lv_color_hsv_to_rgb((uint16_t)v, 80, 70), 0);
}

static void fps_probe_cb(lv_timer_t *t)
{
    uint32_t cnt  = LCD_GetRefrCount();
    uint32_t tick = lv_tick_get();

    if (s_fps_last_tick != 0) {
        uint32_t dt = tick - s_fps_last_tick;
        if (dt > 0) {
            ESP_LOGI("fps", "%u fps（累计刷新 %u 次）",
                     (unsigned)((cnt - s_fps_last_cnt) * 1000u / dt),
                     (unsigned)cnt);
        }
    }
    s_fps_last_cnt  = cnt;
    s_fps_last_tick = tick;
}

void Diag_StartFpsProbe(void)
{
    lvgl_port_lock(0);

    lv_obj_t *probe = lv_obj_create(lv_layer_top());
    lv_obj_set_size(probe, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(probe, 0, 0);
    lv_obj_set_style_bg_opa(probe, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(probe, 0, 0);
    lv_obj_set_style_radius(probe, 0, 0);
    lv_obj_clear_flag(probe, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, probe);
    lv_anim_set_exec_cb(&a, probe_color_anim);
    lv_anim_set_values(&a, 0, 359);
    lv_anim_set_time(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    lv_timer_create(fps_probe_cb, 1000, NULL);

    lvgl_port_unlock();
    ESP_LOGW("fps", "帧率探针已启动（全屏动画，屏幕被彩色覆盖属正常）");
}

#endif /* APP_FPS_PROBE */

/* ================= GPIO 完好性自检 =================
 *
 * 原理：CMOS 引脚内部是"一对 ESD 二极管 + 一个 NMOS 下拉 + 一个 PMOS 上拉"。
 *       - 对外短路到 GND → 输出高会被硬拉低（out_hi 读回 0）
 *       - 对外短路到 VDD → 输出低会被硬拉高（out_lo 读回 1）
 *       - 键合线/驱动管烧断   → 输出与读回不一致，且释放后电平不再跟随外部
 * 四个阶段全过才算健康。
 *
 * 附加信息量：released（释放为高阻后读回）在接了 I2C 模块时应为 1，
 * 因为它被模块内部的 4.7k 上拉电阻拉到模块自身的 3.3V 轨。
 * 所以 released=1 同时证明了三件事：走线通、上拉电阻在、模块 LDO 有输出。
 */
static int selftest_read(int gpio)
{
    vTaskDelay(pdMS_TO_TICKS(5));      /* 等外部 RC 稳定（模块上拉 4.7k × 线容 ~100pF） */
    return gpio_get_level(gpio);
}

void Diag_GpioSelfTest(int gpio)
{
    gpio_config_t c = {0};
    c.pin_bit_mask = 1ULL << gpio;
    c.intr_type    = GPIO_INTR_DISABLE;

    /* 1) 输入 + 内部上拉：期望 1 */
    c.mode        = GPIO_MODE_INPUT;
    c.pull_up_en  = GPIO_PULLUP_ENABLE;
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&c);
    int in_up = selftest_read(gpio);

    /* 2) 输入 + 内部下拉：接了 4.7k 外部上拉的脚会被拉回高，此项仅供参考 */
    c.pull_up_en   = GPIO_PULLUP_DISABLE;
    c.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&c);
    int in_dn = selftest_read(gpio);

    /* 3) 推挽输出：先低后高，看能否真实建立电平
     *    必须用 INPUT_OUTPUT 而不是 OUTPUT：OUTPUT 模式会关掉输入通路，
     *    gpio_get_level() 读回的是陈旧值（恒 0），会把好引脚误判成坏的。 */
    c.pull_down_en = GPIO_PULLDOWN_DISABLE;
    c.mode         = GPIO_MODE_INPUT_OUTPUT;
    gpio_config(&c);

    gpio_set_level(gpio, 0);
    int out_lo = selftest_read(gpio);
    gpio_set_level(gpio, 1);
    int out_hi = selftest_read(gpio);

    /* 4) 释放为高阻，交回外部上拉 */
    c.mode = GPIO_MODE_INPUT;
    gpio_config(&c);
    int released = selftest_read(gpio);

    bool ok = (in_up == 1) && (out_lo == 0) && (out_hi == 1);

    ESP_LOGI("gpiotest",
             "GPIO%-2d 上拉读=%d 下拉读=%d 出低读=%d 出高读=%d 释放读=%d => %s",
             gpio, in_up, in_dn, out_lo, out_hi, released,
             ok ? "引脚正常" : "引脚异常");

    if (!ok) {
        ESP_LOGE("gpiotest",
                 "GPIO%d 输出与读回不自洽：出低=%d(应0) 出高=%d(应1)。"
                 "典型原因是 ESD 二极管击穿或对外硬短路",
                 gpio, out_lo, out_hi);
    } else if (released == 0) {
        ESP_LOGW("gpiotest",
                 "GPIO%d 释放后仍为低：引脚本身是好的，但外部没有上拉。"
                 "说明模块内部上拉/LDO 供电这条链路断了（模块可能已损或没供电）",
                 gpio);
    }
}
