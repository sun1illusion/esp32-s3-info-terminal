#include "net_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"

#include "app_config.h"
#include "app_io.h"
#include "app_ota.h"
#include "net_wifi.h"

static const char *TAG = "net_web";

/* ============ 串口日志环形缓冲（供手机查看） ============ */
#define LOG_LINES   24
#define LOG_CHARS   96

static char s_log[LOG_LINES][LOG_CHARS];
static int  s_log_head = 0;     /* 下一个写入位置 */
static int  s_log_count = 0;
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;

static void log_push(const char *s)
{
    if (!s || !*s) return;

    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) n--;
    if (n == 0) return;
    if (n >= LOG_CHARS) n = LOG_CHARS - 1;

    portENTER_CRITICAL(&s_log_mux);
    memcpy(s_log[s_log_head], s, n);
    s_log[s_log_head][n] = 0;
    s_log_head = (s_log_head + 1) % LOG_LINES;
    if (s_log_count < LOG_LINES) s_log_count++;
    portEXIT_CRITICAL(&s_log_mux);
}

/* 劫持 ESP_LOG：一份照常打到串口，一份存环形缓冲 */
static int web_log_vprintf(const char *fmt, va_list ap)
{
    char buf[LOG_CHARS];
    va_list cp;
    va_copy(cp, ap);
    vsnprintf(buf, sizeof(buf), fmt, cp);
    va_end(cp);

    log_push(buf);
    return vprintf(fmt, ap);
}

/* ============ 鉴权 ============ */
/* WEB_TOKEN 为空则不校验；否则所有 api 接口必须带 ?k=xxx */
static bool auth_ok(httpd_req_t *req)
{
    if (WEB_TOKEN[0] == '\0') return true;

    char q[128], v[40];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) return false;
    if (httpd_query_key_value(q, "k", v, sizeof(v)) != ESP_OK) return false;
    return strcmp(v, WEB_TOKEN) == 0;
}

static esp_err_t deny(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"err\":\"unauthorized\"}");
}

/* ============ 控制页面（单引号写属性/字符串，避免 C 转义） ============ */
static const char s_html[] =
"<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>大屏控制</title><style>"
"body{background:#0b1e3d;color:#e5e7eb;font-family:system-ui,-apple-system,sans-serif;margin:0;padding:14px}"
"h1{font-size:17px;margin:0 0 4px}"
".sub{color:#93c5fd;font-size:12px;margin-bottom:14px}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
"button{background:#374151;color:#e5e7eb;border:0;border-radius:10px;padding:16px 10px;font-size:15px}"
"button.on{background:#22c55e;color:#052e16;font-weight:600}"
".t{color:#93c5fd;font-size:12px;margin:18px 0 6px}"
"pre{background:#0f172a;border-radius:10px;padding:10px;height:240px;overflow:auto;"
"font-size:11px;line-height:1.5;white-space:pre-wrap;margin:0}"
"#sys{color:#64748b;font-size:11px;margin-top:10px}"
"#stat{color:#f87171;font-size:12px;margin-top:8px;min-height:16px}"
"#fw{width:100%;font-size:12px;color:#93c5fd;margin-bottom:8px}"
"#upp{height:6px;background:#1f2937;border-radius:3px;overflow:hidden;display:none;margin-top:8px}"
"#upp i{display:block;height:100%;background:#3b82f6;width:0;transition:width .2s}"
"</style></head><body>"
"<h1>ESP32-S3 大屏 · 远程控制</h1>"
"<div class='sub'>点按钮切换 GPIO 电平</div>"
"<div class='grid' id='g'></div>"
"<div class='t'>串口日志（最近 24 行）</div>"
"<pre id='log'>连接中...</pre>"
"<div id='stat'></div>"
"<div class='t'>网络 · <span id='wname'>--</span></div>"
"<button id='wbtn'>扫描热点（点一下选网络）</button>"
"<div class='grid' id='wlist'></div>"
"<div id='sys'></div>"
"<div class='t'>固件升级 · 当前版本 <span id='ver'>--</span></div>"
"<input type='file' id='fw' accept='.bin'>"
"<button id='upb' style='width:100%'>上传并升级</button>"
"<div id='upp'><i></i></div>"
"<script>"
"var K=new URLSearchParams(location.search).get('k')||localStorage.getItem('k')||'';"
"if(K)localStorage.setItem('k',K);"
"function U(p){return p+(p.indexOf('?')<0?'?':'&')+'k='+encodeURIComponent(K)}"
"var PINS=[8,9,10,11,12,13,14,18],st={};"
"function err(t){var e=document.getElementById('stat');if(e)e.textContent=t}"
"function kb(v){return v>=1024?(v/1024).toFixed(1)+'MB':v+'KB'}"
"function render(){var g=document.getElementById('g');g.innerHTML='';"
"PINS.forEach(function(p){var b=document.createElement('button');"
"b.textContent='GPIO'+p+(st[p]?'  ON':'  OFF');"
"if(st[p])b.className='on';"
"b.onclick=function(){b.disabled=true;err('');"
"fetch(U('/api/io/set?pin='+p+'&lvl='+(st[p]?0:1)))"
".then(function(r){if(r.status==401){err('没有权限：地址后加 ?k=你的token');return null}return r.json()})"
".then(function(j){if(j){st=j;render()}})"
".catch(function(){err('请求失败，请检查网络')})"
".then(function(){b.disabled=false})};"
"g.appendChild(b)})}"
"function load(){fetch(U('/api/io'))"
".then(function(r){if(r.status==401){err('没有权限：地址后加 ?k=你的token');return null}return r.json()})"
".then(function(j){if(j){st=j;render()}})"
".catch(function(){setTimeout(load,2000)})}"
"function log(){fetch(U('/api/log'))"
".then(function(r){return r.ok?r.text():null})"
".then(function(t){if(t===null)return;var e=document.getElementById('log');"
"e.textContent=t;e.scrollTop=e.scrollHeight})"
".catch(function(){})}"
"function sys(){fetch(U('/api/sys'))"
".then(function(r){return r.ok?r.json():null})"
".then(function(j){if(j){var v=document.getElementById('ver');"
"if(v&&j.ver)v.textContent=j.ver+' · '+j.part;"
"document.getElementById('sys').textContent="
"'运行 '+j.up+'s · RAM '+kb(j.free)+'/'+kb(j.rt)+'（最低 '+kb(j.low)+'，最大块 '+kb(j.blk)+'）"
"· Flash 余 '+kb(j.ff)+'/'+kb(j.ft)}})"
".catch(function(){})}"
"function wstat(){fetch(U('/api/wifi')).then(function(r){return r.ok?r.json():null})"
".then(function(j){if(j){document.getElementById('wname').textContent="
"j.ssid+' · '+(j.conn?'已连接':'未连接')}}).catch(function(){})}"
"function wscan(){var b=document.getElementById('wbtn');b.disabled=true;b.textContent='扫描中...';"
"fetch(U('/api/wifi/scan')).then(function(r){return r.ok?r.json():null})"
".then(function(j){b.disabled=false;b.textContent='扫描热点（点一下选网络）';"
"var d=document.getElementById('wlist');d.innerHTML='';if(!j||!j.aps.length){err('没扫到热点');return}"
"j.aps.forEach(function(a){var x=document.createElement('button');"
"x.textContent=a.s+'  '+a.r+'dBm';"
"x.onclick=function(){var p=prompt('输入 WiFi 密码: '+a.s);if(p===null)return;"
"x.disabled=true;err('');"
"fetch(U('/api/wifi/set?ssid='+encodeURIComponent(a.s)+'&pass='+encodeURIComponent(p)))"
".then(function(r){return r.ok?r.json():null})"
".then(function(o){if(o&&o.ok){err('已保存，正在重连（屏幕会显示新 IP）')}else{err('切换失败');x.disabled=false}})"
".catch(function(){err('切换请求失败');x.disabled=false})};"
"d.appendChild(x)})})"
".catch(function(){b.disabled=false;b.textContent='扫描热点（点一下选网络）';err('扫描失败')})}"
"document.getElementById('wbtn').onclick=wscan;"
"function upload(){"
"var f=document.getElementById('fw').files[0];"
"if(!f){err('请先选择 .bin 固件');return}"
"if(!confirm('确认升级到 '+f.name+' ？设备会自动重启，升级期间不要断电'))return;"
"var b=document.getElementById('upb');b.disabled=true;"
"var pb=document.getElementById('upp'),bar=pb.querySelector('i');"
"pb.style.display='block';bar.style.width='0%';err('');"
"var x=new XMLHttpRequest();"
"x.open('POST',U('/api/ota'));"
"x.setRequestHeader('Content-Type','application/octet-stream');"
"x.upload.onprogress=function(e){if(e.lengthComputable){"
"var p=Math.round(e.loaded*100/e.total);bar.style.width=p+'%';err('上传中 '+p+'%')}};"
"x.onload=function(){"
"if(x.status==200){bar.style.width='100%';err('升级成功，设备重启中，约 10 秒后刷新页面')}"
"else{err('升级失败：'+x.responseText);b.disabled=false}};"
"x.onerror=function(){err('上传中断，检查网络后重试');b.disabled=false};"
"x.send(f)}"
"document.getElementById('upb').onclick=upload;"
"load();log();sys();wstat();"
"setInterval(function(){if(!document.hidden){log();sys();wstat()}},1000);"
"</script></body></html>";

/* ============ 处理器 ============ */

/* 把所有 GPIO 状态拼成 JSON：{"8":0,"9":1,...} */
static void io_json(char *buf, size_t sz)
{
    size_t n = 0;
    buf[n++] = '{';
    for (int i = 0; i < APP_IO_COUNT; i++) {
        n += snprintf(buf + n, sz - n, "%s\"%d\":%d",
                      (i == 0) ? "" : ",", app_io_pins[i], AppIO_Get(i));
        if (n >= sz) break;
    }
    buf[n++] = '}';
    buf[n] = 0;
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    /* 禁止缓存：改了页面/脚本后，手机刷新就能拿到新版本 */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    return httpd_resp_send(req, s_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_io(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    char buf[160];
    io_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_io_set(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    char q[128], val[40];
    int  pin = -1, lvl = 0;

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
        if (httpd_query_key_value(q, "pin", val, sizeof(val)) == ESP_OK) pin = atoi(val);
        if (httpd_query_key_value(q, "lvl", val, sizeof(val)) == ESP_OK) lvl = atoi(val);
    }

    /* 只允许白名单里的引脚，避免网页变成任意 GPIO 后门 */
    for (int i = 0; i < APP_IO_COUNT; i++) {
        if (app_io_pins[i] == pin) {
            AppIO_Set(i, lvl);
            ESP_LOGI(TAG, "网页设置 GPIO%d = %d", pin, lvl);
            break;
        }
    }

    char buf[160];
    io_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_log(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    int start, count;

    portENTER_CRITICAL(&s_log_mux);
    count = s_log_count;
    start = (s_log_head - count + LOG_LINES) % LOG_LINES;
    portEXIT_CRITICAL(&s_log_mux);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");

    /* 逐行分块发送：不用大缓冲，避免占 httpd 任务栈 */
    for (int i = 0; i < count; i++) {
        char line[LOG_CHARS];
        portENTER_CRITICAL(&s_log_mux);
        memcpy(line, s_log[(start + i) % LOG_LINES], LOG_CHARS);
        portEXIT_CRITICAL(&s_log_mux);

        httpd_resp_send_chunk(req, line, strlen(line));
        httpd_resp_send_chunk(req, "\n", 1);
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

/* Flash 总容量 / 剩余（单位 KB）：总容量减去所有已声明的分区 */
static void flash_kb(uint32_t *total_kb, uint32_t *free_kb)
{
    uint32_t sz = 0;
    if (esp_flash_get_size(NULL, &sz) != ESP_OK) sz = 0;

    uint32_t used = 0;
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *p = esp_partition_get(it);
        if (p) used += p->size;
        it = esp_partition_next(it);   /* 内部会释放上一个迭代器 */
    }

    *total_kb = sz / 1024;
    *free_kb  = (sz > used) ? (sz - used) / 1024 : 0;
}

/* ---------------- 固件升级 ---------------- */

static void ota_restart_cb(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "重启进入新固件");
    esp_restart();
}

/* 接收 raw 二进制（Content-Length 即固件大小），边收边写"另一个"分区。
 * 全程不碰正在运行的分区：写失败、校验失败都只是丢掉新分区，设备照常跑旧固件。 */
static esp_err_t h_ota(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    if (Ota_IsUpdating()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "updating");
        return ESP_FAIL;
    }

    size_t total = req->content_len;
    if (total < 64 * 1024 || total > 8 * 1024 * 1024) {
        ESP_LOGE(TAG, "固件大小异常: %u 字节", (unsigned)total);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
        return ESP_FAIL;
    }

    esp_err_t err = Ota_Begin(total);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        return ESP_FAIL;
    }

    /* 4KB 静态缓冲：正好一个 flash sector，也不占 httpd 任务栈 */
    static uint8_t s_buf[4096];
    size_t remain = total;
    int    timeouts = 0;

    while (remain > 0) {
        int want = (remain > sizeof(s_buf)) ? (int)sizeof(s_buf) : (int)remain;
        int got  = httpd_req_recv(req, (char *)s_buf, want);

        if (got == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 10) {
                ESP_LOGE(TAG, "连续超时，放弃升级");
                Ota_Abort();
                return ESP_FAIL;
            }
            continue;
        }
        if (got <= 0) {
            ESP_LOGE(TAG, "接收中断: %d（已收 %u/%u）", got,
                     (unsigned)(total - remain), (unsigned)total);
            Ota_Abort();
            return ESP_FAIL;
        }
        timeouts = 0;

        err = Ota_Write(s_buf, (size_t)got);
        if (err != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write fail");
            return ESP_FAIL;
        }
        remain -= (size_t)got;
    }

    err = Ota_End();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "verify fail");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ok");

    /* 先把响应发出去再重启，否则浏览器只会看到连接被重置 */
    static esp_timer_handle_t rst = NULL;
    if (!rst) {
        const esp_timer_create_args_t a = { .callback = ota_restart_cb, .name = "ota_rst" };
        if (esp_timer_create(&a, &rst) != ESP_OK) {
            esp_restart();
        }
    }
    esp_timer_start_once(rst, 1500 * 1000);

    return ESP_OK;
}

/* 设备健康度：手机端也能看到内存/Flash，便于长跑观察 */
static esp_err_t h_sys(httpd_req_t *req){
    if (!auth_ok(req)) return deny(req);

    uint32_t ft = 0, ff = 0;
    flash_kb(&ft, &ff);

    char buf[220];
    snprintf(buf, sizeof(buf),
             "{\"up\":%u,\"free\":%u,\"low\":%u,\"blk\":%u,\"rt\":%u,\"ft\":%u,\"ff\":%u,"
             "\"ver\":\"%s\",\"part\":\"%s\"}",
             (unsigned)(esp_timer_get_time() / 1000000),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)ft, (unsigned)ff,
             Ota_GetRunningVersion(), Ota_GetRunningPartition());

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

/* ---------- WiFi 配置接口 ---------- */

static esp_err_t h_wifi(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    char buf[160];
    snprintf(buf, sizeof(buf), "{\"ssid\":\"%s\",\"ip\":\"%s\",\"conn\":%d}",
             WiFi_GetSSID(), Web_GetIP(), WiFi_IsConnected() ? 1 : 0);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t h_wifi_scan(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    wifi_ap_t aps[8];
    int n = WiFi_Scan(aps, 8);      /* 阻塞 2~3s，仅本次请求卡住 */

    char buf[600];
    size_t p = 0;
    p += snprintf(buf + p, sizeof(buf) - p, "{\"aps\":[");
    for (int i = 0; i < n && p < sizeof(buf) - 64; i++) {
        p += snprintf(buf + p, sizeof(buf) - p, "%s{\"s\":\"%s\",\"r\":%d}",
                      (i == 0) ? "" : ",", aps[i].ssid, (int)aps[i].rssi);
    }
    p += snprintf(buf + p, sizeof(buf) - p, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

/* 先回复客户端再切换，否则断网瞬间响应发不出去 */
static char s_new_ssid[33];
static char s_new_pass[65];

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* URL 解码：%XX 与 '+'（JS 端用 encodeURIComponent，SSID 可能是中文/带空格） */
static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
            *o++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
            s += 3;
        } else if (*s == '+') {
            *o++ = ' ';
            s++;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

static void wifi_switch_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(800));
    WiFi_Switch(s_new_ssid, s_new_pass);
    vTaskDelete(NULL);
}

static esp_err_t h_wifi_set(httpd_req_t *req)
{
    if (!auth_ok(req)) return deny(req);

    char q[384], v[200];
    s_new_ssid[0] = 0;
    s_new_pass[0] = 0;

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return httpd_resp_sendstr(req, "{\"ok\":0}");
    }
    if (httpd_query_key_value(q, "ssid", v, sizeof(v)) == ESP_OK) {
        url_decode(v);
        strncpy(s_new_ssid, v, sizeof(s_new_ssid) - 1);
    }
    if (httpd_query_key_value(q, "pass", v, sizeof(v)) == ESP_OK) {
        url_decode(v);
        strncpy(s_new_pass, v, sizeof(s_new_pass) - 1);
    }
    if (!s_new_ssid[0]) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":0}");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"ok\":1}", HTTPD_RESP_USE_STRLEN);

    xTaskCreate(wifi_switch_task, "wifi_sw", 4096, NULL, 4, NULL);
    return ESP_OK;
}

/* ============ 初始化 ============ */

void Web_Init(void)
{
    /* 劫持日志输出（必须在启动服务器前装好，才能记录后续日志） */
    esp_log_set_vprintf(web_log_vprintf);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 10;
    cfg.lru_purge_enable = true;   /* URI 用尽时回收最久未用连接 */
    cfg.stack_size = 6144;         /* WiFi 扫描会用到较多栈 */

    httpd_handle_t srv = NULL;
    esp_err_t err = httpd_start(&srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(err));
        return;
    }

    static const httpd_uri_t u_index  = { .uri = "/",            .method = HTTP_GET, .handler = h_index };
    static const httpd_uri_t u_io     = { .uri = "/api/io",      .method = HTTP_GET, .handler = h_io };
    static const httpd_uri_t u_io_set = { .uri = "/api/io/set",  .method = HTTP_GET, .handler = h_io_set };
    static const httpd_uri_t u_log    = { .uri = "/api/log",     .method = HTTP_GET, .handler = h_log };
    static const httpd_uri_t u_sys    = { .uri = "/api/sys",     .method = HTTP_GET, .handler = h_sys };
    static const httpd_uri_t u_wifi   = { .uri = "/api/wifi",      .method = HTTP_GET, .handler = h_wifi };
    static const httpd_uri_t u_wscan  = { .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = h_wifi_scan };
    static const httpd_uri_t u_wset   = { .uri = "/api/wifi/set",  .method = HTTP_GET, .handler = h_wifi_set };
    static const httpd_uri_t u_ota    = { .uri = "/api/ota",       .method = HTTP_POST, .handler = h_ota };

    httpd_register_uri_handler(srv, &u_index);
    httpd_register_uri_handler(srv, &u_io);
    httpd_register_uri_handler(srv, &u_io_set);
    httpd_register_uri_handler(srv, &u_log);
    httpd_register_uri_handler(srv, &u_sys);
    httpd_register_uri_handler(srv, &u_wifi);
    httpd_register_uri_handler(srv, &u_wscan);
    httpd_register_uri_handler(srv, &u_wset);
    httpd_register_uri_handler(srv, &u_ota);

    ESP_LOGI(TAG, "HTTP 服务就绪（鉴权: %s）", WEB_TOKEN[0] ? "已开启" : "关闭");
}

const char *Web_GetIP(void)
{
    static char ip[16] = "0.0.0.0";

    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!nif) return ip;

    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(nif, &info) == ESP_OK && info.ip.addr != 0) {
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    }
    return ip;
}
