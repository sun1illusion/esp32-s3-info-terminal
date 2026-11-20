#ifndef __UI_COMMON_H
#define __UI_COMMON_H

#include "lvgl.h"

/* 基础图形拼装（用 LVGL 原生对象画圆/条/渐变/射线/泪滴，
 * 全部轴对齐 + transform 旋转，避免依赖图片资源） */

lv_obj_t *UI_Circle(lv_obj_t *parent, int x, int y, int d, uint32_t color);
lv_obj_t *UI_Bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius);
lv_obj_t *UI_GradCircle(lv_obj_t *parent, int x, int y, int d, uint32_t c_top, uint32_t c_bot);
void      UI_Ray(lv_obj_t *parent, int cx, int cy, int inner_r, int len, int w,
                 uint32_t color, int angle);
void      UI_Drop(lv_obj_t *parent, int tx, int ty, int r, uint32_t color);

#endif
