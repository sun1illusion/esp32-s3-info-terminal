#include "net_wifi.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "app_config.h"
#include "app_state.h"

static const char *TAG = "net_wifi";

#define NVS_NS      "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "pass"
#define SCAN_MAX    20

static char s_ssid[33] = {0};
static char s_pass[65] = {0};

/* ---------------- 凭据存取 ---------------- */

static void creds_load(void)
{
    nvs_handle_t h;
    size_t len;

    /* 默认值先填上（首次上电 / NVS 被擦） */
    strncpy(s_ssid, WIFI_SSID, sizeof(s_ssid) - 1);
    strncpy(s_pass, WIFI_PASS, sizeof(s_pass) - 1);

    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "无已保存凭据，使用默认: %s", s_ssid);
        return;
    }

    len = sizeof(s_ssid);
    if (nvs_get_str(h, NVS_KEY_SSID, s_ssid, &len) != ESP_OK) {
        strncpy(s_ssid, WIFI_SSID, sizeof(s_ssid) - 1);
    }
    len = sizeof(s_pass);
    if (nvs_get_str(h, NVS_KEY_PASS, s_pass, &len) != ESP_OK) {
        strncpy(s_pass, WIFI_PASS, sizeof(s_pass) - 1);
    }
    nvs_close(h);
    ESP_LOGI(TAG, "已加载凭据: %s", s_ssid);
}

static esp_err_t creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, pass);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ---------------- 事件 ---------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        esp_wifi_connect();          /* 断线重连 */
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_wifi_connected = true;
        ESP_LOGI(TAG, "WiFi 已连接: %s", s_ssid);
    }
}

/* ---------------- 应用配置 ---------------- */

static void apply_sta_config(void)
{
    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, s_ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, s_pass, sizeof(wc.sta.password) - 1);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

void WiFi_Init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    creds_load();
    esp_wifi_set_mode(WIFI_MODE_STA);
    apply_sta_config();
    esp_wifi_start();
}

bool WiFi_IsConnected(void)
{
    return g_wifi_connected;
}

const char *WiFi_GetSSID(void)
{
    return s_ssid;
}

esp_err_t WiFi_Switch(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32 || (pass && strlen(pass) > 64)) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "切换网络: %s", ssid);

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = 0;
    strncpy(s_pass, pass ? pass : "", sizeof(s_pass) - 1);
    s_pass[sizeof(s_pass) - 1] = 0;

    esp_err_t err = creds_save(s_ssid, s_pass);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "凭据保存失败: %s", esp_err_to_name(err));
    }

    /* 断开旧连接 → 换配置 → 重连（事件回调里会持续重试） */
    g_wifi_connected = false;
    esp_wifi_disconnect();
    apply_sta_config();
    esp_wifi_connect();

    return ESP_OK;
}

/* ---------------- 扫描 ---------------- */

int WiFi_Scan(wifi_ap_t *out, int max)
{
    if (!out || max <= 0) return 0;

    /* 扫描期间需要 STA 已启动 */
    wifi_scan_config_t sc = {0};
    sc.show_hidden = false;
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;

    esp_err_t err = esp_wifi_scan_start(&sc, true);   /* true = 阻塞等待完成 */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "扫描失败: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;
    if (num > SCAN_MAX) num = SCAN_MAX;

    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) return 0;

    uint16_t got = num;
    if (esp_wifi_scan_get_ap_records(&got, recs) != ESP_OK) {
        free(recs);
        return 0;
    }

    /* 去重同名 + 按信号强度插入排序（强 → 弱） */
    int n = 0;
    for (int i = 0; i < got; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == 0) continue;                       /* 隐藏热点跳过 */

        int dup = -1;
        for (int k = 0; k < n; k++) {
            if (strcmp(out[k].ssid, ssid) == 0) { dup = k; break; }
        }
        if (dup >= 0) {
            if (recs[i].rssi > out[dup].rssi) out[dup].rssi = recs[i].rssi;
            continue;
        }
        if (n >= max) continue;

        /* 插入到合适位置 */
        int pos = n;
        while (pos > 0 && out[pos - 1].rssi < recs[i].rssi) {
            out[pos] = out[pos - 1];
            pos--;
        }
        strncpy(out[pos].ssid, ssid, sizeof(out[pos].ssid) - 1);
        out[pos].ssid[sizeof(out[pos].ssid) - 1] = 0;
        out[pos].rssi = recs[i].rssi;
        n++;
    }

    free(recs);
    ESP_LOGI(TAG, "扫描完成，%d 个热点", n);
    return n;
}
