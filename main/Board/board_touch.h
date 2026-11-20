#ifndef __BOARD_TOUCH_H
#define __BOARD_TOUCH_H

#include <stdint.h>

/* XPT2046 电阻触摸：初始化 + 注册为 LVGL 输入设备
 * 依赖 LCD_Init() 已经初始化好 SPI2 总线 */
void Touch_Init(void);

/* 最后一次按下的时刻（lv_tick 毫秒）。息屏逻辑用它算空闲时长。
 * 返回值在 LVGL 任务里更新，32 位对齐读写在 Xtensa 上是原子的，可直接读。 */
uint32_t Touch_GetLastPressMs(void);

#endif
