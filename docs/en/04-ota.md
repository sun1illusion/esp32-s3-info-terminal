# OTA Updates

[中文版](../zh/04-OTA升级实现.md) · [Back to README](../../README.md)

---

## 1. Partition layout

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000      # settings + Wi-Fi credentials
otadata,  data, ota,     0xf000,   0x2000      # which bank to boot
phy_init, data, phy,     0x11000,  0x1000      # RF calibration
ota_0,    app,  ota_0,   0x20000,  0x400000    # bank A
ota_1,    app,  ota_1,   0x420000, 0x400000    # bank B
```

| Detail | Rationale |
|---|---|
| **Two 4 MB banks** | The firmware is 1.58 MB, so each bank has 2.4 MB of headroom — enough for future features without repartitioning |
| **`otadata` is 8 KB** | It must span exactly two 4 KB sectors. Writing a new record erases one sector while the other still holds the previous record, so a power loss mid-write is always recoverable |
| **64 KB alignment** | Application partitions must start on a 64 KB boundary |
| **No `factory` partition** | It would consume 4 MB and can never be written by OTA anyway. Recovery relies on a full flash backup instead |

> Changing the partition table **invalidates everything already flashed**. The first deployment after this change must be a full `idf.py flash` over USB.

## 2. Update flow

```
browser ──POST /api/ota──► httpd task
                              │
                              ├─ Ota_Begin(content_length)
                              │     └─ esp_ota_get_next_update_partition()   → the *other* bank
                              │     └─ esp_ota_begin(part, size, &handle)
                              │
                              ├─ loop: httpd_req_recv() → Ota_Write()
                              │     └─ esp_ota_write()   4 KB at a time
                              │
                              ├─ Ota_End()
                              │     └─ esp_ota_end()               verify SHA-256
                              │     └─ esp_ota_set_boot_partition() update otadata
                              │
                              └─ respond "ok" → esp_timer(1.5 s) → esp_restart()
```

**The running partition is never touched.** Every failure mode — bad upload, failed verification, power loss — leaves the active bank intact and the device still running.

The 1.5 second delay before restarting exists so the HTTP response reaches the browser. Rebooting immediately produces a connection reset instead of a success message.

## 3. Rollback

With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE = y`:

```
new firmware boots  →  state = ESP_OTA_IMG_PENDING_VERIFY
                       │
                       ├─ app calls esp_ota_mark_app_valid_cancel_rollback()
                       │       within 30 s  →  state = VALID, boot permanently
                       │
                       └─ resets before confirming  →  bootloader marks ABORTED
                                                      and boots the previous bank
```

`app_ota.c` implements the confirmation with a one-shot timer:

```c
void Ota_Init(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {

        /* 30 s is long enough for Wi-Fi, UI and peripherals to come up.
         * If anything crashes in that window, the timer never fires and
         * the bootloader rolls back on the next reset. */
        esp_timer_start_once(timer, 30 * 1000000ULL);
    }
}

static void confirm_cb(void *arg)
{
    esp_ota_mark_app_valid_cancel_rollback();
}
```

This is deliberately lenient: it only checks that the firmware survived 30 seconds, not that Wi-Fi connected. A firmware that cannot reach the network is usually a network problem, not a firmware problem — rolling back on every router outage would be worse than the disease.

## 4. Measured results

| Test | Result |
|---|---|
| Upload 1.58 MB over Wi-Fi | **7-10 s** (~300 KB/s), HTTP 200 |
| Bank A → B | `Loaded app from partition at offset 0x420000` |
| Bank B → A | `Loaded app from partition at offset 0x20000` |
| Confirmation | `新固件已确认，回滚标记取消` after 30 s |
| Wi-Fi credentials after update | Preserved (NVS untouched) |
| **Forced rollback** | Reset during the confirmation window → `ABORTED` → previous bank restored |

The rollback test was not intentional — it happened while capturing serial output during verification, as described in [the development log](03-development-log.md#5-ota-returns-http-200-but-the-device-still-runs-the-old-firmware). It is the most convincing evidence that the mechanism works.

## 5. Reading `otadata` by hand

When an update appears to succeed but the device boots the old firmware, read the raw region:

```bash
esptool --chip esp32s3 -p COM11 -b 460800 read_flash 0xf000 0x2000 otadata.bin
od -A x -t x4 -N 32 otadata.bin             # sector 0
od -A x -t x4 -j 0x1000 -N 32 otadata.bin   # sector 1
```

Each record is `[seq:4][label:20][state:4][crc:4]`:

| `state` | Meaning |
|---|---|
| 0 | `ESP_OTA_IMG_NEW` |
| 1 | `ESP_OTA_IMG_PENDING_VERIFY` |
| 2 | `ESP_OTA_IMG_VALID` |
| 3 | `ESP_OTA_IMG_INVALID` |
| 4 | `ESP_OTA_IMG_ABORTED` — boot loader refused it |

The bootloader picks the bank with the highest `seq` whose state is not INVALID or ABORTED. Odd `seq` maps to `ota_0`, even to `ota_1`.

## 6. API surface

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/ota` | POST | Raw firmware binary; `Content-Length` must equal the image size |
| `/api/sys` | GET | Includes `ver` (running version) and `part` (`ota_0` / `ota_1`) |

`part` is the quickest way to confirm which bank is running — more reliable than reading boot logs, and it does not disturb the confirmation window.

Authentication: all API endpoints require `?k=<WEB_TOKEN>`; with an empty token the check is disabled. This is a convenience gate for a trusted LAN, **not** transport security — the UI is served over plain HTTP.
