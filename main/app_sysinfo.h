#ifndef __APP_SYSINFO_H
#define __APP_SYSINFO_H

#include <stdint.h>

/* 系统运行状态采集：供「系统监控」页显示。
 * 数据来源：heap_caps（RAM）、分区表 + image header（Flash）、
 *          esp_timer（运行时长）、uxTaskGetSystemState（任务列表）。 */

typedef struct {
    uint32_t ram_total_kb;
    uint32_t ram_used_kb;
    uint32_t ram_min_kb;      /* 历史最低空闲水位 */
    uint32_t flash_total_kb;  /* app 分区容量 */
    uint32_t flash_used_kb;   /* 固件实际占用 */
    uint32_t uptime_s;
    uint32_t task_count;
} sysinfo_t;

#define SYSINFO_TASK_MAX 6

typedef struct {
    char     name[14];
    uint8_t  prio;
    uint8_t  cpu_pct;      /* 需 CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS */
    uint16_t stack_free;   /* 字节，历史最小剩余（ESP-IDF 中单位即字节） */
} sysinfo_task_t;

void SysInfo_Get(sysinfo_t *out);

/* 按 CPU 占用从高到低返回最多 max 个**业务任务**（IDLE 已过滤）。
 * *cpu_load 返回所有核心的平均负载（0~100）——即 100 减去 IDLE 占比，
 * 这才是「系统有多忙」，不要直接拿 IDLE 的占比当负载看。
 * 返回实际条数。 */
int SysInfo_GetTopTasks(sysinfo_task_t *rows, int max, uint8_t *cpu_load);

#endif
