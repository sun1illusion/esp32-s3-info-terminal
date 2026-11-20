#ifndef __UI_SETTINGS_H
#define __UI_SETTINGS_H

#include "lvgl.h"

/* 设置模块：设置主页（亮度/音量/功能按钮）+ 三个二级页
 *   - 串口状态：UART/SPI/触摸/I2C/WiFi 参数
 *   - 系统信息：芯片/Flash/内存/MAC/固件版本
 *   - IO口管理：GPIO 逐个开关（点灯） */
void      Settings_Init(void);
lv_obj_t *Settings_GetScreen(void);

#endif
