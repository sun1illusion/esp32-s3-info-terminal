#include "app_sysinfo.h"

#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sysinfo";

/* 固件镜像大小只需查一次：esp_image_verify 会把整个镜像读一遍做校验，
 * 约几十毫秒，放在页面首次打开时做一次即可，不能每次刷新都调。 */
static uint32_t s_flash_total_kb = 0;
static uint32_t s_flash_used_kb  = 0;

static void flash_probe_once(void)
{
    if (s_flash_total_kb) return;

    const esp_partition_t *p = esp_ota_get_running_partition();
    if (!p) {
        ESP_LOGW(TAG, "取运行分区失败");
        return;
    }

    s_flash_total_kb = p->size / 1024;

    /* esp_image_verify 要的是 metadata 结构体，镜像长度在 meta.image_len 里。
     * 结构体约 200 字节，用 static 避免压在调用者栈上。 */
    static esp_image_metadata_t s_meta;
    esp_partition_pos_t pos = { .offset = p->address, .size = p->size };
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &s_meta) == ESP_OK) {
        s_flash_used_kb = s_meta.image_len / 1024;
    } else {
        /* 校验失败也把读到的一部分信息留下，至少不显示 0 */
        ESP_LOGW(TAG, "镜像校验失败，Flash 占用按 0 处理");
    }

    ESP_LOGI(TAG, "Flash 固件 %uKB / 分区 %uKB", (unsigned)s_flash_used_kb, (unsigned)s_flash_total_kb);
}

void SysInfo_Get(sysinfo_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    flash_probe_once();

    size_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t free_ = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    out->ram_total_kb = total / 1024;
    out->ram_used_kb  = (total - free_) / 1024;
    out->ram_min_kb   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024;

    out->flash_total_kb = s_flash_total_kb;
    out->flash_used_kb  = s_flash_used_kb;

    out->uptime_s   = (uint32_t)(esp_timer_get_time() / 1000000);
    out->task_count = (uint32_t)uxTaskGetNumberOfTasks();
}

int SysInfo_GetTopTasks(sysinfo_task_t *rows, int max, uint8_t *cpu_load)
{
    if (cpu_load) *cpu_load = 0;
    if (!rows || max <= 0) return 0;

    /* 静态缓冲：TaskStatus_t 约 40 字节，24 个近 1KB，放栈上不安全 */
    static TaskStatus_t st[24];
    uint32_t total_run = 0;

    UBaseType_t n = uxTaskGetSystemState(st, 24, &total_run);
    if (n == 0) return 0;

    /* 先把每个任务的占比算出来（百分比以墙上时间为基数，
     * 双核时所有任务加起来约 200%，所以单任务上限 100%） */
    static uint8_t pct[24];
    uint32_t idle_sum = 0;
    int idle_cnt = 0;

    for (int i = 0; i < (int)n; i++) {
        if (total_run > 0) {
            pct[i] = (uint8_t)((uint64_t)st[i].ulRunTimeCounter * 100 / total_run);
        } else {
            pct[i] = 0;
        }

        const char *nm = st[i].pcTaskName ? st[i].pcTaskName : "";
        if (strncmp(nm, "IDLE", 4) == 0) {
            idle_sum += pct[i];
            idle_cnt++;
        }
    }

    /* CPU 负载 = 100 - 平均 IDLE 占比。按实际 IDLE 任务数取平均，
     * 这样单核/双核都能得到 0~100 的正确结果。 */
    if (cpu_load && idle_cnt > 0) {
        uint32_t avg_idle = idle_sum / (uint32_t)idle_cnt;
        *cpu_load = (avg_idle >= 100) ? 0 : (uint8_t)(100 - avg_idle);
    }

    /* 选择排序：只挑出非 IDLE 任务里 CPU 最高的前 max 个 */
    int picked = 0;
    uint8_t used[24] = {0};

    while (picked < max) {
        int best = -1;
        for (int j = 0; j < (int)n; j++) {
            if (used[j]) continue;
            if (strncmp(st[j].pcTaskName ? st[j].pcTaskName : "", "IDLE", 4) == 0) continue;
            if (best < 0 || pct[j] > pct[best]) best = j;
        }
        if (best < 0) break;
        used[best] = 1;

        sysinfo_task_t *r = &rows[picked];
        strncpy(r->name, st[best].pcTaskName ? st[best].pcTaskName : "?", sizeof(r->name) - 1);
        r->name[sizeof(r->name) - 1] = '\0';
        r->prio       = (uint8_t)st[best].uxCurrentPriority;
        r->cpu_pct    = pct[best];
        r->stack_free = (uint16_t)st[best].usStackHighWaterMark;
        picked++;
    }

    return picked;
}
