#ifndef __UI_WEATHER_H
#define __UI_WEATHER_H

#include "lvgl.h"

/* 天气页：主卡片（大图标 + 大温度）+ 4 个次级数据卡片 + 未来 4 天预报
 * 数据来自 net_weather 写入的全局变量，由内部 timer 刷新 */
void      WeatherUI_Init(void);
lv_obj_t *WeatherUI_GetScreen(void);

#endif
