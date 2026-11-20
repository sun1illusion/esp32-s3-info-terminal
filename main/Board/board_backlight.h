#ifndef __BOARD_BACKLIGHT_H
#define __BOARD_BACKLIGHT_H

/* 屏幕背光 PWM 控制（LEDC，20kHz）
 * 硬件前提：屏幕 LED 引脚必须接 GPIO1，否则只能软件调数、不影响实际亮度 */
void Backlight_Init(void);
void Backlight_Set(int percent);   /* 0~100 */

#endif
