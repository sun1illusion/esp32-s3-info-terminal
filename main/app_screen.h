#ifndef __APP_SCREEN_H
#define __APP_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

/* 大屏省电：连续 idle_ms 无触摸就关闭背光，触摸后立即恢复亮屏。
 *
 * 为什么只关背光、不进 light sleep：
 *   - 背光是整机最大功耗项（100~150mA），关掉就能砍掉一半左右
 *   - light sleep 会让 WiFi 断连，看板的天气/手机控制会失效，
 *     唤醒后还要重连 3~5 秒，对"常在线"的设备不划算
 *   - 关背光后 LVGL 仍在轮询触摸，所以"摸一下就亮"，没有唤醒延迟
 *
 * 小屏 OLED 不参与：SSD1306 全亮仅 10~20mA，且产品定位就是常亮天气显示。
 *
 * 占用：1 个 lv_timer + 3 个变量，约 150 字节。 */
void ScreenPower_Init(uint32_t idle_ms);

bool ScreenPower_IsOff(void);

#endif
