#include "net_weather.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "app_config.h"
#include "app_state.h"
#include "app_diag.h"

static const char *TAG = "net_weather";

/* HTTP 响应缓冲（4KB 足够当前接口的返回体） */
static char s_http_resp[4096];
static int  s_http_len = 0;

/* ---------------- 数据语义转换 ---------------- */

const char *Weather_CodeToText(int code)
{
    if (code == 0) return "晴";
    if (code == 1) return "晴间多云";
    if (code == 2) return "多云";
    if (code == 3) return "阴";
    if (code == 45 || code == 48) return "雾";
    if (code >= 51 && code <= 57) return "毛毛雨";
    if (code >= 61 && code <= 67) return "雨";
    if (code >= 71 && code <= 77) return "雪";
    if (code >= 80 && code <= 82) return "阵雨";
    if (code >= 85 && code <= 86) return "阵雪";
    if (code >= 95) return "雷阵雨";
    return "天气";
}

int Weather_WindLevel(float kmh)
{
    if (kmh < 1) return 0;
    if (kmh < 6) return 1;
    if (kmh < 12) return 2;
    if (kmh < 20) return 3;
    if (kmh < 29) return 4;
    if (kmh < 39) return 5;
    if (kmh < 50) return 6;
    if (kmh < 62) return 7;
    if (kmh < 75) return 8;
    if (kmh < 89) return 9;
    if (kmh < 103) return 10;
    if (kmh < 118) return 11;
    return 12;
}

const char *Weather_WindDirText(int deg)
{
    if (deg < 22 || deg >= 337) return "北风";
    if (deg < 67) return "东北风";
    if (deg < 112) return "东风";
    if (deg < 157) return "东南风";
    if (deg < 202) return "南风";
    if (deg < 247) return "西南风";
    if (deg < 292) return "西风";
    return "西北风";
}

/* ---------------- HTTP 接收 ---------------- */

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (s_http_len + evt->data_len < (int)sizeof(s_http_resp) - 1) {
            memcpy(s_http_resp + s_http_len, evt->data, evt->data_len);
            s_http_len += evt->data_len;
            s_http_resp[s_http_len] = 0;
        }
    }
    return ESP_OK;
}

/* ---------------- JSON 解析 ---------------- */

static void weather_parse_json(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON 解析失败");
        return;
    }

    cJSON *cur = cJSON_GetObjectItem(root, "current");
    if (cur) {
        cJSON *v;
        v = cJSON_GetObjectItem(cur, "temperature_2m");
        if (cJSON_IsNumber(v)) g_temp_f = (float)v->valuedouble;
        v = cJSON_GetObjectItem(cur, "relative_humidity_2m");
        if (cJSON_IsNumber(v)) g_hum_f = (float)v->valuedouble;
        v = cJSON_GetObjectItem(cur, "apparent_temperature");
        if (cJSON_IsNumber(v)) g_feels_f = (float)v->valuedouble;
        v = cJSON_GetObjectItem(cur, "weather_code");
        if (cJSON_IsNumber(v)) g_weather_code = v->valueint;
        v = cJSON_GetObjectItem(cur, "wind_speed_10m");
        if (cJSON_IsNumber(v)) g_wind_f = (float)v->valuedouble;
        v = cJSON_GetObjectItem(cur, "wind_direction_10m");
        if (cJSON_IsNumber(v)) g_wind_dir = (int)v->valuedouble;
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *time_arr = cJSON_GetObjectItem(daily, "time");
        cJSON *code_arr = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *max_arr  = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *min_arr  = cJSON_GetObjectItem(daily, "temperature_2m_min");

        int n = cJSON_GetArraySize(time_arr);
        if (n > FC_DAYS) n = FC_DAYS;
        for (int i = 0; i < n; i++) {
            cJSON *t = cJSON_GetArrayItem(time_arr, i);
            if (cJSON_IsString(t)) {
                if (i == 0) strcpy(g_daily_date[i], "今天");
                else snprintf(g_daily_date[i], sizeof(g_daily_date[i]), "%s", t->valuestring + 5);
            }
            cJSON *c = cJSON_GetArrayItem(code_arr, i);
            if (cJSON_IsNumber(c)) g_daily_code[i] = c->valueint;
            cJSON *mx = cJSON_GetArrayItem(max_arr, i);
            if (cJSON_IsNumber(mx)) g_daily_max[i] = (int)mx->valuedouble;
            cJSON *mn = cJSON_GetArrayItem(min_arr, i);
            if (cJSON_IsNumber(mn)) g_daily_min[i] = (int)mn->valuedouble;
        }
    }

    cJSON_Delete(root);
    g_weather_ready = true;
    ESP_LOGI(TAG, "天气解析完成: %.1f°C %s", g_temp_f, Weather_CodeToText(g_weather_code));
}

/* ---------------- 取数任务 ---------------- */

static void weather_fetch_task(void *arg)
{
    /* 最多等 10s 让 WiFi 连上 */
    int retry = 0;
    while (!g_wifi_connected && retry < 50) {
        vTaskDelay(pdMS_TO_TICKS(200));
        retry++;
    }
    if (!g_wifi_connected) {
        ESP_LOGE(TAG, "WiFi 未连接，放弃本次获取");
        vTaskDelete(NULL);
        return;
    }

    s_http_len = 0;
    esp_http_client_config_t config = {
        .url = WEATHER_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || s_http_len == 0) {
        ESP_LOGE(TAG, "HTTP 失败: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    weather_parse_json(s_http_resp);
    Diag_MemReport("取数后");     /* 此时能看到 TLS/JSON 峰值后的最低水位 */
    vTaskDelete(NULL);
}

void Weather_TaskStart(void)
{
    /* 10KB 栈：TLS 握手 + cJSON 足够；检查返回值，避免静默失败 */
    BaseType_t ok = xTaskCreate(weather_fetch_task, "weather", 10240, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "创建天气任务失败（内存不足），free heap=%u",
                 (unsigned)esp_get_free_heap_size());
    } else {
        ESP_LOGI(TAG, "天气任务已启动，free heap=%u", (unsigned)esp_get_free_heap_size());
    }
}
