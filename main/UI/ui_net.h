#ifndef __UI_NET_H
#define __UI_NET_H

#include "lvgl.h"

/* 手机控制页：显示设备 IP 与使用说明（网页由 net_web 提供） */
void      NetUI_Init(void);
lv_obj_t *NetUI_GetScreen(void);

#endif
