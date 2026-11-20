#include "net_time.h"

#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_netif.h"

static const char *TAG = "net_time";

/* 2001-09-09 之后的时间视为"已对上"：芯片上电默认从 1970-01-01 起算，
 * SNTP 成功后会直接跳到当前时间，这个阈值只用来区分两者。 */
#define TIME_VALID_MIN  1000000000L

static bool s_sntp_started = false;

static void sntp_sync_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm tm;
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "SNTP 对时成功: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static void start_sntp(void)
{
    if (s_sntp_started) return;
    s_sntp_started = true;

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP 已启动（ntp.aliyun.com）");
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    start_sntp();
}

void Time_Init(void)
{
    /* 东八区。CST-8 是 POSIX 写法，符号是反的：CST-8 表示 UTC+8 */
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL);

    /* 若调用时 WiFi 已经连上（重连场景），IP 事件不会再触发，这里补一刀 */
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            start_sntp();
        }
    }
}

bool Time_GetHM(int *hour, int *min)
{
    time_t now = time(NULL);
    if (now < (time_t)TIME_VALID_MIN) return false;

    struct tm tm;
    localtime_r(&now, &tm);
    if (hour) *hour = tm.tm_hour;
    if (min)  *min  = tm.tm_min;
    return true;
}

bool Time_IsSynced(void)
{
    return time(NULL) >= (time_t)TIME_VALID_MIN;
}
