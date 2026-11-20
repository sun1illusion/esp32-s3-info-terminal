#ifndef __NET_TIME_H
#define __NET_TIME_H

#include <stdbool.h>

/* SNTP 网络对时。
 * 时序：WiFi_Init() 之后调用 Time_Init()，它挂到 IP_EVENT_STA_GOT_IP 上，
 *       拿到 IP 才真正启动 SNTP —— 开机就启动的话首包必然失败，
 *       而 lwip 的重试间隔是 1 小时，等于一开机就得等到下个整点。
 * 未对时前 time() 返回 1970 年，Time_GetHM 返回 false，调用方据此决定画不画。 */
void Time_Init(void);

/* 取本地时:分（东八区）。返回 false 表示还没对上时间。 */
bool Time_GetHM(int *hour, int *min);

bool Time_IsSynced(void);

#endif
