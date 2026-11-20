#ifndef __APP_DIAG_H
#define __APP_DIAG_H

#include <stdint.h>

/* 内存体检：打印内部 SRAM 的空闲/最大连续块/历史最低水位，
 * 以及 LVGL 内存池的占用与碎片率。
 * low（历史最低）能反映 TLS 握手等峰值时刻的真实压力。 */
void Diag_MemReport(const char *stage);

/* 长跑监控：每 period_ms 打一次（建议 10 分钟），低于阈值会告警 */
void Diag_StartPeriodic(uint32_t period_ms);

/* GPIO 完好性自检（针对 ESD 二极管/驱动管是否被击穿）。
 * 必须在该引脚被外设驱动接管之前调用（本例：Oled_Init 之前）。
 * 通过"输出低/高后读回"的自洽性判断引脚是否物理损坏。 */
void Diag_GpioSelfTest(int gpio);

/* 帧率探针：铺全屏动画强制整屏重绘，每秒打印 fps。
 * 仅在 APP_FPS_PROBE=1 时有实现，测试期间会遮挡界面。 */
void Diag_StartFpsProbe(void);

#include "lvgl.h"

/* 扫描一棵 LVGL 对象树里的所有 lv_label，检查每个字符在所用字体里是否存在。
 * 缺字时 LVGL 不会报错，只会渲染成空白框，肉眼发现太晚；
 * 在每个页面的 Init 末尾调用一次，启动日志就会直接报出码点。
 * 必须在持 lvgl_port_lock 的上下文里调用。 */
void Diag_CheckGlyphs(lv_obj_t *root, const char *page);

#endif
