#ifndef __NET_WEATHER_H
#define __NET_WEATHER_H

/* 天气服务：HTTP 拉取 Open-Meteo → 解析 JSON → 写入 g_weather_* 全局
 * 结果由 UI 层的 timer 读取并刷新界面 */

/* 启动一次天气获取（内部创建任务，等待 WiFi 就绪后拉取，失败自动放弃） */
void Weather_TaskStart(void);

/* 数据语义转换（UI 层复用） */
const char *Weather_CodeToText(int code);   /* WMO 天气码 → 中文 */
int         Weather_WindLevel(float kmh);   /* 风速 → 蒲福风级 0~12 */
const char *Weather_WindDirText(int deg);   /* 风向角 → 中文方位 */

#endif
