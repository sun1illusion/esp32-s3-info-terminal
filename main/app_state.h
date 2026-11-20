#ifndef __APP_STATE_H
#define __APP_STATE_H

#include <stdbool.h>

/* 全局共享状态：各模块写、各模块读（类似 STM32 里的 AD_Value[] 全局） */

/* --- 网络 --- */
extern volatile bool g_wifi_connected;

/* --- 当前天气 --- */
extern int   g_weather_code;
extern float g_temp_f;
extern float g_hum_f;
extern float g_feels_f;
extern float g_wind_f;
extern int   g_wind_dir;
extern volatile bool g_weather_ready;   /* 新数据就绪标志（UI 消费后清零） */

/* --- 未来预报 --- */
#define FC_DAYS 4
extern int  g_daily_code[FC_DAYS];
extern int  g_daily_max[FC_DAYS];
extern int  g_daily_min[FC_DAYS];
extern char g_daily_date[FC_DAYS][8];

/* --- 设置项 --- */
extern int g_brightness;   /* 0~100 */
extern int g_volume;       /* 0~100（喇叭未接，预留） */

#endif
