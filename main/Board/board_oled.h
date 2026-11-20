#ifndef __BOARD_OLED_H
#define __BOARD_OLED_H

#include <stdbool.h>

/* 0.91 寸 SSD1306（128x32，I2C 0x3C/0x3D）
 * 用途：常亮显示天气图标 + 气温 + 湿度/风力
 * 特点：独立帧缓存 + 自绘，不经过 LVGL，不消耗对象池 */

void Oled_Init(void);
bool Oled_IsPresent(void);

#endif
