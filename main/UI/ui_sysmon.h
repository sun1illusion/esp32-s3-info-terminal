#ifndef __UI_SYSMON_H
#define __UI_SYSMON_H

#include "lvgl.h"

/* 「系统运行状态」页：RAM/Flash 环形图 + 运行时长 + 任务 CPU/栈余。
 * 数据来自 app_sysinfo，每秒刷新一次。 */
void SysMon_Init(void);
lv_obj_t *SysMon_GetScreen(void);

#endif
