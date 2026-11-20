#ifndef __UI_WIFI_H
#define __UI_WIFI_H

#include "lvgl.h"

/* WiFi 设置页：显示当前网络 + 扫描附近热点 + 屏幕键盘输入密码切换 */
void      WiFiUI_Init(void);
lv_obj_t *WiFiUI_GetScreen(void);

#endif
