#ifndef __UI_ICONS_H
#define __UI_ICONS_H

#include "lvgl.h"

/* 天气/气象图标（全部用基础图形拼装，随数据动态变化） */

/* 大天气图标（48x48+，主卡片用） */
void UI_WeatherIcon(lv_obj_t *parent, int code, int x, int y);
/* 小天气图标（24x24，预报栏用） */
void UI_WeatherIconSmall(lv_obj_t *parent, int code, int x, int y);
/* 湿度水滴：湿度越高水滴越大越多 */
void UI_HumidityIcon(lv_obj_t *parent, float hum, int x, int y);
/* 温度计（大，24x48） */
void UI_ThermoIcon(lv_obj_t *parent, float temp, int x, int y);
/* 温度计（小，28x28，体感卡片用） */
void UI_ThermoIconSmall(lv_obj_t *parent, float temp, int x, int y);
/* 风三条流线 */
void UI_WindIcon(lv_obj_t *parent, int x, int y);
/* 风向旗帜 */
void UI_WindDirIcon(lv_obj_t *parent, int x, int y);

/* 温度 → 颜色（-10℃ 蓝 → 15℃ 绿 → 40℃ 红），返回 RGB888 */
uint32_t UI_TempToColor(float temp);

#endif
