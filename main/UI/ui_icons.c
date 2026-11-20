#include "ui_icons.h"
#include "ui_common.h"

/* 温度 → 颜色（-10℃ 蓝 → 15℃ 绿 → 40℃ 红 渐变），返回 RGB888 */
uint32_t UI_TempToColor(float temp)
{
    float t = (temp + 10.0f) / 50.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;

    uint8_t r, g, b;
    if (t < 0.5f) {
        r = 0;
        g = (uint8_t)(t * 2 * 255);
        b = (uint8_t)((1 - t * 2) * 255);
    } else {
        r = (uint8_t)((t - 0.5f) * 2 * 255);
        g = (uint8_t)((1 - (t - 0.5f) * 2) * 255);
        b = 0;
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* 湿度图标：湿度越高水滴越大越多（28x28 容器内，坐标已适配不超界） */
void UI_HumidityIcon(lv_obj_t *parent, float hum, int x, int y)
{
    if (hum < 30) {
        UI_Drop(parent, x + 14, y + 3, 7, 0x60A5FA);
    } else if (hum < 60) {
        UI_Drop(parent, x + 12, y + 3, 8, 0x3B82F6);
        UI_Drop(parent, x + 24, y + 17, 4, 0x60A5FA);
    } else if (hum < 80) {
        UI_Drop(parent, x + 11, y + 3, 9, 0x2563EB);
        UI_Drop(parent, x + 23, y + 13, 5, 0x3B82F6);
    } else {
        UI_Drop(parent, x + 11, y + 3, 9, 0x2563EB);
        UI_Drop(parent, x + 23, y + 11, 5, 0x3B82F6);
        UI_Drop(parent, x + 25, y + 21, 3, 0x60A5FA);
    }
}

/* 温度计图标（描边玻璃管 + 彩色球泡），在 (x,y) 画 24x48 区域 */
void UI_ThermoIcon(lv_obj_t *parent, float temp, int x, int y)
{
    uint32_t rgb = UI_TempToColor(temp);

    lv_obj_t *tube = UI_Bar(parent, x + 8, y + 2, 8, 26, 0x1F2937, 4);
    lv_obj_set_style_border_width(tube, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(tube, lv_color_hex(0x9CA3AF), LV_PART_MAIN);

    float t = (temp + 10.0f) / 50.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int fill_h = (int)(6 + t * 18);
    UI_Bar(parent, x + 10, y + 26 - fill_h, 4, fill_h, rgb, 2);

    UI_Circle(parent, x + 2, y + 26, 20, rgb);
    UI_Circle(parent, x + 7, y + 31, 5, 0xFFFFFF);
}

/* 小温度计（28x28 内，体感卡片用） */
void UI_ThermoIconSmall(lv_obj_t *parent, float temp, int x, int y)
{
    uint32_t rgb = UI_TempToColor(temp);

    lv_obj_t *tube = UI_Bar(parent, x + 11, y + 0, 6, 11, 0x374151, 3);
    lv_obj_set_style_border_width(tube, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(tube, lv_color_hex(0x9CA3AF), LV_PART_MAIN);

    float t = (temp + 10.0f) / 50.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    int fill_h = (int)(4 + t * 7);
    UI_Bar(parent, x + 12, y + 11 - fill_h, 4, fill_h, rgb, 2);

    UI_Circle(parent, x + 8, y + 10, 12, rgb);
    UI_Circle(parent, x + 12, y + 14, 3, 0xFFFFFF);
}

/* 风图标：三条渐短流线 */
void UI_WindIcon(lv_obj_t *parent, int x, int y)
{
    UI_Bar(parent, x + 2, y + 6, 18, 3, 0x93C5FD, 2);
    UI_Bar(parent, x + 6, y + 13, 22, 3, 0x60A5FA, 2);
    UI_Bar(parent, x + 2, y + 20, 12, 3, 0x93C5FD, 2);
}

/* 风向旗帜（28x28 内）：旗杆 + 三角旗面 */
void UI_WindDirIcon(lv_obj_t *parent, int x, int y)
{
    UI_Bar(parent, x + 5, y + 0, 3, 24, 0x9CA3AF, 1);

    int s = 13;
    lv_obj_t *flag = lv_obj_create(parent);
    lv_obj_set_size(flag, s, s);
    lv_obj_set_pos(flag, x + 8, y + 1);
    lv_obj_set_style_radius(flag, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(flag, lv_color_hex(0x60A5FA), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(flag, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(flag, 0, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(flag, s / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(flag, s / 2, LV_PART_MAIN);
    lv_obj_set_style_transform_rotation(flag, 450, LV_PART_MAIN);
    lv_obj_clear_flag(flag, LV_OBJ_FLAG_SCROLLABLE);
}

/* 大天气图标（48x48 区域，渐变拟物 + 放射光线） */
void UI_WeatherIcon(lv_obj_t *parent, int code, int x, int y)
{
    if (code == 0 || code == 1) {
        /* 晴：橙黄渐变太阳 + 8 根放射射线 */
        UI_GradCircle(parent, x + 12, y + 12, 24, 0xFFF176, 0xF5A623);
        for (int i = 0; i < 8; i++) {
            UI_Ray(parent, x + 24, y + 24, 14, 6, 3, 0xFBBF24, i * 450);
        }
    } else if (code == 2) {
        /* 多云：小太阳 + 白→淡蓝渐变云 */
        UI_GradCircle(parent, x + 4, y + 6, 16, 0xFFF176, 0xF5A623);
        UI_GradCircle(parent, x + 14, y + 18, 24, 0xFFFFFF, 0xBBD8F5);
        UI_GradCircle(parent, x + 26, y + 12, 18, 0xFFFFFF, 0xBBD8F5);
        UI_Bar(parent, x + 12, y + 28, 32, 12, 0xBBD8F5, 6);
    } else if (code == 3) {
        /* 阴：灰渐变云 */
        UI_GradCircle(parent, x + 6, y + 14, 22, 0xD1D5DB, 0x9CA3AF);
        UI_GradCircle(parent, x + 22, y + 8, 20, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 4, y + 22, 38, 14, 0x9CA3AF, 7);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        /* 雨：云 + 泪滴雨滴 */
        UI_GradCircle(parent, x + 6, y + 8, 22, 0xD1D5DB, 0x9CA3AF);
        UI_GradCircle(parent, x + 22, y + 2, 20, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 4, y + 16, 38, 12, 0x9CA3AF, 6);
        UI_Drop(parent, x + 8, y + 28, 4, 0x4FC3F7);
        UI_Drop(parent, x + 20, y + 30, 4, 0x4FC3F7);
        UI_Drop(parent, x + 32, y + 28, 4, 0x4FC3F7);
    } else if (code >= 71 && code <= 77) {
        /* 雪：云 + 白点 */
        UI_GradCircle(parent, x + 6, y + 8, 22, 0xD1D5DB, 0x9CA3AF);
        UI_GradCircle(parent, x + 22, y + 2, 20, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 4, y + 16, 38, 12, 0x9CA3AF, 6);
        UI_Circle(parent, x + 10, y + 30, 6, 0xFFFFFF);
        UI_Circle(parent, x + 22, y + 32, 6, 0xFFFFFF);
        UI_Circle(parent, x + 32, y + 30, 6, 0xFFFFFF);
    } else if (code >= 95) {
        /* 雷雨：深云 + 黄色闪电 */
        UI_GradCircle(parent, x + 6, y + 8, 22, 0x6B7280, 0x4B5563);
        UI_GradCircle(parent, x + 22, y + 2, 20, 0x6B7280, 0x4B5563);
        UI_Bar(parent, x + 4, y + 16, 38, 12, 0x4B5563, 6);
        lv_obj_t *z = UI_Bar(parent, x + 24, y + 26, 6, 18, 0xFBBF24, 2);
        lv_obj_set_style_transform_pivot_x(z, 3, LV_PART_MAIN);
        lv_obj_set_style_transform_pivot_y(z, 9, LV_PART_MAIN);
        lv_obj_set_style_transform_rotation(z, 200, LV_PART_MAIN);
    } else {
        /* 雾：浅灰云 + 横线 */
        UI_GradCircle(parent, x + 12, y + 6, 20, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 4, y + 24, 40, 5, 0xD1D5DB, 3);
        UI_Bar(parent, x + 8, y + 34, 30, 5, 0xD1D5DB, 3);
    }
}

/* 小天气图标（预报栏用，24x24 区域） */
void UI_WeatherIconSmall(lv_obj_t *parent, int code, int x, int y)
{
    if (code == 0 || code == 1) {
        UI_GradCircle(parent, x + 8, y + 8, 12, 0xFFF176, 0xF5A623);
    } else if (code == 2) {
        UI_GradCircle(parent, x + 2, y + 4, 8, 0xFFF176, 0xF5A623);
        UI_GradCircle(parent, x + 8, y + 10, 12, 0xFFFFFF, 0xBBD8F5);
        UI_Bar(parent, x + 6, y + 16, 14, 6, 0xBBD8F5, 3);
    } else if (code == 3) {
        UI_GradCircle(parent, x + 4, y + 8, 12, 0xD1D5DB, 0x9CA3AF);
        UI_GradCircle(parent, x + 12, y + 4, 10, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 2, y + 12, 20, 7, 0x9CA3AF, 4);
    } else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
        UI_GradCircle(parent, x + 4, y + 6, 12, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 2, y + 12, 18, 6, 0x9CA3AF, 3);
        UI_Drop(parent, x + 4, y + 18, 3, 0x4FC3F7);
        UI_Drop(parent, x + 12, y + 18, 3, 0x4FC3F7);
    } else if (code >= 71 && code <= 77) {
        UI_GradCircle(parent, x + 4, y + 6, 12, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 2, y + 12, 18, 6, 0x9CA3AF, 3);
        UI_Circle(parent, x + 6, y + 18, 5, 0xFFFFFF);
        UI_Circle(parent, x + 14, y + 18, 5, 0xFFFFFF);
    } else if (code >= 95) {
        UI_GradCircle(parent, x + 4, y + 6, 12, 0x6B7280, 0x4B5563);
        UI_Bar(parent, x + 2, y + 12, 18, 6, 0x4B5563, 3);
        UI_Bar(parent, x + 12, y + 14, 4, 10, 0xFBBF24, 2);
    } else {
        UI_GradCircle(parent, x + 6, y + 4, 10, 0xD1D5DB, 0x9CA3AF);
        UI_Bar(parent, x + 2, y + 14, 20, 4, 0xD1D5DB, 2);
        UI_Bar(parent, x + 4, y + 20, 14, 4, 0xD1D5DB, 2);
    }
}
