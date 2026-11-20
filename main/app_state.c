#include "app_state.h"
#include "app_config.h"

/* 全局变量定义（头文件里 extern 声明） */
volatile bool g_wifi_connected = false;

int   g_weather_code = -1;
float g_temp_f = 0;
float g_hum_f = 0;
float g_feels_f = 0;
float g_wind_f = 0;
int   g_wind_dir = 0;
volatile bool g_weather_ready = false;

int  g_daily_code[FC_DAYS];
int  g_daily_max[FC_DAYS];
int  g_daily_min[FC_DAYS];
char g_daily_date[FC_DAYS][8];

int g_brightness = DEFAULT_BRIGHTNESS;
int g_volume = DEFAULT_VOLUME;
