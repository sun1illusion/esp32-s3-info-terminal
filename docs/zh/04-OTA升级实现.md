# OTA 升级实现

[English](../en/04-ota.md) · [返回 README](../../README.md)

---

## 一、分区表设计

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000      # 设置项 + Wi-Fi 凭据
otadata,  data, ota,     0xf000,   0x2000      # 记录从哪个分区启动
phy_init, data, phy,     0x11000,  0x1000      # 射频校准
ota_0,    app,  ota_0,   0x20000,  0x400000    # A 分区
ota_1,    app,  ota_1,   0x420000, 0x400000    # B 分区
```

| 设计点 | 理由 |
|---|---|
| **两个 4MB 分区** | 固件 1.58MB，每个分区余 2.4MB，以后加功能不用重新分区 |
| **`otadata` 8KB** | 必须正好占两个 4KB 扇区。写新记录时擦一个扇区，另一个还留着上一条记录，所以写到一半断电永远可恢复 |
| **64KB 对齐** | 应用分区起始地址必须按 64KB 对齐 |
| **去掉 `factory`** | 它要占 4MB，而 OTA 永远写不到它。救砖靠全片备份 |

> 改分区表会让**已经烧进去的东西全部失效**。改完第一次必须用 USB 完整 `idf.py flash` 一次。

## 二、升级流程

```
浏览器 ──POST /api/ota──► httpd 任务
                            │
                            ├─ Ota_Begin(content_length)
                            │     └─ esp_ota_get_next_update_partition()   → 挑"另一个"分区
                            │     └─ esp_ota_begin(分区, 大小, &handle)
                            │
                            ├─ 循环：httpd_req_recv() → Ota_Write()
                            │     └─ esp_ota_write()   每次 4KB
                            │
                            ├─ Ota_End()
                            │     └─ esp_ota_end()               校验 SHA-256
                            │     └─ esp_ota_set_boot_partition() 改写 otadata
                            │
                            └─ 回 "ok" → esp_timer 1.5 秒 → esp_restart()
```

**全程不碰正在运行的分区。** 任何失败——上传中断、校验不通过、断电——运行中的分区都完好，设备继续正常用。

重启前延时 1.5 秒，是为了让 HTTP 响应先发出去。立刻重启的话浏览器看到的是连接重置，而不是成功提示。

## 三、回滚机制

开启 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE = y` 之后：

```
新固件启动  →  状态 = ESP_OTA_IMG_PENDING_VERIFY
                │
                ├─ 应用在 30 秒内调 esp_ota_mark_app_valid_cancel_rollback()
                │       → 状态 = VALID，永久生效
                │
                └─ 确认前重启（崩溃 / 看门狗 / 人为复位）
                        → Bootloader 标记 ABORTED，回到上一个分区
```

`app_ota.c` 里用一个单次定时器实现确认：

```c
void Ota_Init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {

        /* 30 秒足够 Wi-Fi、界面和各外设初始化完。
         * 这期间如果崩了，定时器永远不会触发，
         * 下次复位时 Bootloader 自动回滚。 */
        esp_timer_start_once(timer, 30 * 1000000ULL);
    }
}

static void confirm_cb(void *arg)
{
    esp_ota_mark_app_valid_cancel_rollback();
}
```

这个确认条件是**故意宽松**的：只看固件活过了 30 秒，不检查 Wi-Fi 是否连上。连不上网通常是网络环境问题而不是固件问题——每换一个路由器就回滚一次，比不回滚更糟。

## 四、实测结果

| 测试 | 结果 |
|---|---|
| 上传 1.58MB（Wi-Fi） | **7~10 秒**（约 300KB/s），HTTP 200 |
| A 分区 → B 分区 | `Loaded app from partition at offset 0x420000` |
| B 分区 → A 分区 | `Loaded app from partition at offset 0x20000` |
| 新固件确认 | 30 秒后 `新固件已确认，回滚标记取消` |
| 升级后 Wi-Fi 凭据 | 保留（nvs 未被擦） |
| **强制回滚** | 确认窗口内复位 → 标记 `ABORTED` → 回到上一个分区 |

那次回滚测试不是刻意设计的，是抓串口日志时意外触发的，详见[开发日志第 5 条](03-开发历程与踩坑.md)。这反而是回滚机制有效的最有力证据。

## 五、手工读 otadata

如果升级"成功"了但设备启动的是老固件，直接读原始数据：

```bash
esptool --chip esp32s3 -p COM11 -b 460800 read_flash 0xf000 0x2000 otadata.bin
od -A x -t x4 -N 32 otadata.bin             # sector 0
od -A x -t x4 -j 0x1000 -N 32 otadata.bin   # sector 1
```

每条记录结构是 `[seq:4][label:20][state:4][crc:4]`：

| `state` | 含义 |
|---|---|
| 0 | `ESP_OTA_IMG_NEW` |
| 1 | `ESP_OTA_IMG_PENDING_VERIFY` |
| 2 | `ESP_OTA_IMG_VALID` |
| 3 | `ESP_OTA_IMG_INVALID` |
| 4 | `ESP_OTA_IMG_ABORTED` —— Bootloader 拒绝启动 |

Bootloader 选 `seq` 最大、且状态不是 INVALID/ABORTED 的那个分区。`seq` 为奇数对应 `ota_0`，偶数为 `ota_1`。

## 六、接口

| 接口 | 方法 | 用途 |
|---|---|---|
| `/api/ota` | POST | 直接接收固件二进制，`Content-Length` 必须等于镜像大小 |
| `/api/sys` | GET | 返回含 `ver`（当前版本）和 `part`（`ota_0` / `ota_1`） |

**`part` 是确认当前跑在哪个分区最快的方式**——比翻启动日志可靠，也不会干扰确认窗口。

鉴权：所有 API 需要 `?k=<WEB_TOKEN>`，token 为空则不校验。这只是可信局域网里的防误触门槛，**不是传输加密**——界面走的是明文 HTTP。
