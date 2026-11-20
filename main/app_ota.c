#include "app_ota.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"

static const char *TAG = "app_ota";

/* 新固件"报到"延时：留够时间让 WiFi/UI/外设都初始化完。
 * 如果这段时间内崩溃或看门狗复位，就永远走不到确认，bootloader 会自动回滚。 */
#define OTA_CONFIRM_DELAY_MS  (30 * 1000)

static esp_ota_handle_t       s_handle  = 0;
static const esp_partition_t *s_target  = NULL;
static size_t                 s_total   = 0;
static size_t                 s_written = 0;
static bool                   s_updating = false;

const char *Ota_GetRunningVersion(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d ? d->version : "?";
}

const char *Ota_GetRunningPartition(void)
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

bool Ota_IsUpdating(void)
{
    return s_updating;
}

int Ota_GetProgress(void)
{
    if (s_total == 0) return 0;
    int p = (int)(s_written * 100 / s_total);
    return p > 100 ? 100 : p;
}

esp_err_t Ota_Begin(size_t image_size)
{
    if (s_updating) {
        ESP_LOGW(TAG, "已有升级在进行中");
        return ESP_ERR_INVALID_STATE;
    }
    if (image_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 自动挑"另一个"分区：当前跑 ota_0 就返回 ota_1，反之亦然 */
    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target) {
        ESP_LOGE(TAG, "找不到可用的 OTA 分区（分区表里没有 ota_0/ota_1？）");
        return ESP_ERR_NOT_FOUND;
    }

    if (image_size > s_target->size) {
        ESP_LOGE(TAG, "固件 %u 字节 > 分区 %s 容量 %u 字节",
                 (unsigned)image_size, s_target->label, (unsigned)s_target->size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 传入确切大小，让底层一次性擦除整个镜像区间，写入更快 */
    esp_err_t err = esp_ota_begin(s_target, image_size, &s_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin 失败: %s", esp_err_to_name(err));
        s_handle = 0;
        return err;
    }

    s_total    = image_size;
    s_written  = 0;
    s_updating = true;

    ESP_LOGI(TAG, "开始升级 → %s（%u 字节）", s_target->label, (unsigned)image_size);
    return ESP_OK;
}

esp_err_t Ota_Write(const void *data, size_t len)
{
    if (!s_updating) return ESP_ERR_INVALID_STATE;

    esp_err_t err = esp_ota_write(s_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "写入失败: %s", esp_err_to_name(err));
        Ota_Abort();
        return err;
    }

    s_written += len;

    /* 每 10% 打一条，避免刷屏又能看出进度 */
    static int last_pct = -1;
    int pct = Ota_GetProgress();
    if (pct / 10 != last_pct / 10) {
        last_pct = pct;
        ESP_LOGI(TAG, "进度 %d%%（%u/%u）", pct, (unsigned)s_written, (unsigned)s_total);
    }
    return ESP_OK;
}

esp_err_t Ota_End(void)
{
    if (!s_updating) return ESP_ERR_INVALID_STATE;

    /* 这里会校验整包 SHA256，失败说明数据不完整或损坏 */
    esp_err_t err = esp_ota_end(s_handle);
    s_handle   = 0;
    s_updating = false;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end 失败（镜像校验不通过）: %s", esp_err_to_name(err));
        return err;
    }

    if (s_written != s_total) {
        ESP_LOGE(TAG, "写入字节数不符：%u / %u", (unsigned)s_written, (unsigned)s_total);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_ota_set_boot_partition(s_target);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "设置启动分区失败: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "升级完成，下次启动将进入 %s", s_target->label);
    return ESP_OK;
}

void Ota_Abort(void)
{
    if (s_handle) {
        esp_ota_abort(s_handle);
        s_handle = 0;
    }
    s_updating = false;
    s_written  = 0;
    s_total    = 0;
    ESP_LOGW(TAG, "升级已中止");
}

/* ---------------- 新固件确认 ---------------- */

static void confirm_timer_cb(void *arg)
{
    (void)arg;
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "新固件已确认，回滚标记取消（当前分区永久生效）");
    } else if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGD(TAG, "当前固件不在待确认状态，无需处理");
    } else {
        ESP_LOGW(TAG, "mark_valid 失败: %s", esp_err_to_name(err));
    }
}

void Ota_Init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "取运行分区失败");
        return;
    }

    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
        return;
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "运行分区 %s，版本 %s", running->label, Ota_GetRunningVersion());
        return;
    }

    ESP_LOGW(TAG, "检测到待确认的新固件（%s），%d 秒后若仍存活则确认",
             running->label, OTA_CONFIRM_DELAY_MS / 1000);

    const esp_timer_create_args_t args = {
        .callback = confirm_timer_cb,
        .name     = "ota_confirm",
    };
    esp_timer_handle_t t = NULL;
    if (esp_timer_create(&args, &t) == ESP_OK) {
        esp_timer_start_once(t, (uint64_t)OTA_CONFIRM_DELAY_MS * 1000);
    } else {
        /* 定时器都建不起来就别硬撑，直接确认，避免无谓回滚 */
        ESP_LOGW(TAG, "定时器创建失败，立即确认");
        confirm_timer_cb(NULL);
    }
}
