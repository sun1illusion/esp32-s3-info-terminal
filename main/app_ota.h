#ifndef __APP_OTA_H
#define __APP_OTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* OTA 升级（A/B 双分区 + 失败自动回滚）
 *
 * 原理：
 *   1. 新固件写进"另一个"分区（ota_0 与 ota_1 互为备份），运行中的分区不受影响
 *   2. esp_ota_end() 会校验整包 SHA256，不通过就不切换
 *   3. 写完后 set_boot_partition() + 重启，bootloader 从新分区启动
 *   4. 新固件若带 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 编译，首次启动处于
 *      ESP_OTA_IMG_PENDING_VERIFY，必须调 Ota_Init() 里的确认逻辑"报到"，
 *      否则下次重启 bootloader 会自动回退到旧分区
 */

/* 启动时调用一次：若当前是待验证的新固件，延时 30 秒后确认健康、取消回滚 */
void Ota_Init(void);

/* 升级流程：Begin → 多次 Write → End（成功后由调用方重启） */
esp_err_t Ota_Begin(size_t image_size);
esp_err_t Ota_Write(const void *data, size_t len);
esp_err_t Ota_End(void);
void      Ota_Abort(void);

bool Ota_IsUpdating(void);
int  Ota_GetProgress(void);              /* 0~100 */

const char *Ota_GetRunningVersion(void); /* 当前固件版本（来自 esp_app_desc） */
const char *Ota_GetRunningPartition(void);

#endif
