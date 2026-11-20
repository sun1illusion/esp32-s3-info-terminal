#ifndef __NET_WIFI_H
#define __NET_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* 热点信息（扫描结果） */
typedef struct {
    char   ssid[33];
    int8_t rssi;
} wifi_ap_t;

/* 连接 WiFi：凭据从 NVS 读取，首次上电用 app_config.h 里的默认值补上 */
void WiFi_Init(void);

bool        WiFi_IsConnected(void);
const char *WiFi_GetSSID(void);   /* 当前配置的 SSID（不一定已连上） */

/* 切换网络：写入 NVS（掉电不丢）并立即重连 */
esp_err_t WiFi_Switch(const char *ssid, const char *pass);

/* 阻塞式扫描（约 2~3 秒，调用方需放在独立任务里）：
 * 去重同名 SSID、按信号强度从强到弱，返回写入 out 的条数 */
int WiFi_Scan(wifi_ap_t *out, int max);

#endif
