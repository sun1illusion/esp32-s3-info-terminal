#ifndef __BOARD_LCD_H
#define __BOARD_LCD_H

#include <stdint.h>

/* ILI9341 屏幕 + LVGL 移植层初始化（内部会初始化 SPI2 总线）
 * 注意：必须先调用 LCD_Init()，再调用 Touch_Init()（两者共用 SPI2） */
void LCD_Init(void);

/* 累计刷新完成次数（LV_EVENT_REFR_READY 计数）。
 * 注意：SPI 的 on_color_trans_done 回调被 esp_lvgl_port 用于同步 flush，
 * 不能再注册，所以这里从 LVGL 事件侧统计。 */
uint32_t LCD_GetRefrCount(void);

#endif
