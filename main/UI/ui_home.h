#ifndef __UI_HOME_H
#define __UI_HOME_H

#include "lvgl.h"

/* 主界面（智能面板：天气/新闻/消息/设置 四张卡片） */
void      Home_Init(void);
lv_obj_t *Home_GetScreen(void);

#endif
