#include "ui_common.h"

/* 实心圆 */
lv_obj_t *UI_Circle(lv_obj_t *parent, int x, int y, int d, uint32_t color)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, d, d);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(c, 0, LV_PART_MAIN);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/* 圆角矩形 */
lv_obj_t *UI_Bar(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, radius, LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

/* 纵向渐变圆：c_top 亮 → c_bot 深 */
lv_obj_t *UI_GradCircle(lv_obj_t *parent, int x, int y, int d, uint32_t c_top, uint32_t c_bot)
{
    lv_obj_t *c = UI_Circle(parent, x, y, d, c_top);
    lv_obj_set_style_bg_grad_color(c, lv_color_hex(c_bot), LV_PART_MAIN);
    lv_obj_set_style_bg_grad_dir(c, LV_GRAD_DIR_VER, LV_PART_MAIN);
    return c;
}

/* 射线（太阳光线）：绕圆心 (cx,cy) 旋转，angle 单位 0.1° */
void UI_Ray(lv_obj_t *parent, int cx, int cy, int inner_r, int len, int w,
            uint32_t color, int angle)
{
    lv_obj_t *r = UI_Bar(parent, cx + inner_r, cy - w / 2, len, w, color, w / 2);
    lv_obj_set_style_transform_pivot_x(r, -inner_r, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(r, w / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(r, angle, LV_PART_MAIN);
}

/* 泪滴（上尖下圆）：圆 + 顶部旋转 45° 菱形尖角，尖角顶点在 (tx, ty-0.4r) */
void UI_Drop(lv_obj_t *parent, int tx, int ty, int r, uint32_t color)
{
    UI_Circle(parent, tx - r, ty, r * 2, color);

    int s = r;
    if (s < 3) s = 3;

    lv_obj_t *tip = lv_obj_create(parent);
    lv_obj_set_size(tip, s, s);
    lv_obj_set_pos(tip, tx - s / 2, ty + (r * 3) / 10 - s / 2);
    lv_obj_set_style_radius(tip, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tip, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(tip, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(tip, s / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(tip, s / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(tip, 450, LV_PART_MAIN);
    lv_obj_clear_flag(tip, LV_OBJ_FLAG_SCROLLABLE);
}
