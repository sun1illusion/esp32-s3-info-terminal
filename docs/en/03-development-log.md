# Development Log and Pitfalls

[中文版](../zh/03-开发历程与踩坑.md) · [Back to README](../../README.en.md)

---

## Timeline

| Phase | Work |
|---|---|
| 1 | Display bring-up: SPI bus, ILI9341 init, backlight PWM |
| 2 | LVGL integration, touch calibration, first working page |
| 3 | Wi-Fi, SNTP, HTTPS weather fetch |
| 4 | Multi-page UI, settings, GPIO abstraction, mobile web UI |
| 5 | Memory reduction under the no-PSRAM constraint |
| 6 | System monitor page, diagnostics, font tooling |
| 7 | OTA: partition table, dual bank, rollback verification |

---

## Pitfalls

Each entry is written as: **symptom → root cause → fix**. These are the ones that cost the most time.

### 1. OLED initialisation crashes with `Double exception`

**Symptom.** Calling the OLED init function reboots the chip. The serial log shows `Double exception` with a stack pointer that points into invalid memory.

**Root cause.** The SSD1306 frame buffer is 512 bytes. The code copied all 512 bytes into a **129-byte stack buffer** before sending. The overflow overwrote the return address and several stack frames.

**Fix.** Send in chunks of 128 bytes, matching the I²C transaction limit:

```c
for (int i = 0; i < 512; i += 128) {
    i2c_master_transmit(dev, &buf[i], 128, timeout);
}
```

**Lesson.** `memcpy` into a stack buffer has no bounds checking. The compiler will not warn, and the crash appears somewhere unrelated.

---

### 2. PSRAM is unusable on this module

**Symptom.** Enabling `CONFIG_SPIRAM` causes random corruption — occasionally a crash, more often silently wrong data.

**Root cause.** The module uses a **v0.2 chip revision** with a known defect in the external RAM interface.

**Fix.** `CONFIG_SPIRAM` permanently disabled. Every subsequent memory decision in this project follows from this constraint.

**Consequence.** Internal SRAM only — roughly 320 KB for everything. This is what forced the draw-buffer tuning, the font trimming, and the memory diagnostics described below.

---

### 3. Shrinking the LVGL pool from 64 KB to 56 KB crashes on the 60-second mark

**Symptom.** After reducing `CONFIG_LV_MEM_SIZE`, the device boots fine and runs for about a minute, then crashes. Short tests looked perfectly healthy.

**Root cause.** Peak LVGL usage (46 KB at the time) occurs while a page is being **rendered**, not while it is created. The short test only measured the creation phase, which uses far less. The crash appeared when the first full render cycle ran out of pool.

**Fix.** Measure the peak, then size the pool with headroom. It is now 72 KB with a 52 KB peak.

**Lesson.** A memory figure taken right after initialisation is not the peak. Peak occurs during the first heavy operation — a full redraw, a TLS handshake, a large JSON parse.

---

### 4. Missing Chinese glyphs render as blank boxes, with no error

**Symptom.** A new UI string shows an empty square instead of a character. Nothing in the log. LVGL does not consider it an error.

**Root cause.** The custom bitmap font is generated from the characters present in the source code **at generation time**. Text added afterwards is simply not in the font.

**Fix, two layers.**

- *Build time*: `tools/check_glyphs.py` scans every string literal in the sources and reports missing codepoints; `tools/regen_font.py` regenerates the font as the union of existing codepoints and newly used ones.
- *Runtime*: `Diag_CheckGlyphs()` walks every `lv_label` on every screen after initialisation and asks `lv_font_get_glyph_dsc()` whether each character exists, logging the exact codepoint of anything missing.

```
I (734)  font: 【主页】字库完整
E (xxx)  font: 【系统监控】缺字 U+8D1F
```

This pitfall was hit **twice** (「占」 and 「负」) before the runtime check was added. The lesson: when a library fails silently, add an assertion, not a habit.

---

### 5. OTA returns HTTP 200 but the device still runs the old firmware

**Symptom.** Uploading a firmware image completes successfully, the browser shows "ok", the device reboots — and comes up on the **old** version.

**Root cause.** Not an OTA bug. Reading `otadata` directly revealed:

```
sector0: seq=3 state=4 (ABORTED)   ← the new partition was rejected
sector1: seq=2 state=2 (VALID)     ← fallback to this one
```

The new image had been written correctly and marked for boot. But the serial capture script used during verification **pulls RTS to reset the chip** — and it was run within the 30-second confirmation window, before the new firmware could call `esp_ota_mark_app_valid_cancel_rollback()`. The bootloader interpreted the reset as a failed boot, marked the partition `ABORTED` and rolled back.

**Fix.** Wait 35 seconds after upload before opening the serial monitor, or check `part` in `/api/sys` first.

**What this actually proved.** The rollback mechanism works. A firmware that cannot boot itself — for any reason — is automatically rejected, and the device stays usable without a cable.

---

### 6. Frame rate does not change when the draw buffer is reduced

**Symptom.** Expecting a frame-rate drop when cutting the draw buffer from 60 to 40 lines. Measured result: exactly the same, 16 fps.

**Root cause.** The bottleneck is not the SPI transfer. LVGL refreshes on a 33 ms period (`CONFIG_LV_DEF_REFR_PERIOD`). A full 240×320×2 frame is 153,600 bytes; at 40 MHz that takes 30.7 ms, already close to the period. Splitting it into 6 or 8 transactions does not reduce the total bytes.

The measurement was done with a dedicated probe: a full-screen rectangle animating through the HSV colour space forces a complete redraw every frame, and `LV_EVENT_REFR_READY` counts completed refreshes.

> **Why not count SPI `on_color_trans_done` callbacks?** That callback is already registered by `esp_lvgl_port` to synchronise flushing. Registering another one replaces it, and the display shows garbage.

---

### 7. SNTP fails silently if started before the IP is assigned

**Symptom.** Wi-Fi connects, but the clock stays at 1970. No error is logged.

**Root cause.** SNTP was started during `WiFi_Init()`, before `IP_EVENT_STA_GOT_IP`. The first request goes out with no source address and fails. lwIP's retry interval for this path is **one hour**, so nothing appears to happen.

**Fix.** Start SNTP from the `IP_EVENT_STA_GOT_IP` handler, not during initialisation.

---

### 8. A GPIO self-test reports a healthy pin as broken

**Symptom.** The pin integrity check (write a level, read it back) always reads 0, so every pin looks damaged.

**Root cause.** The pin was configured with `GPIO_MODE_OUTPUT`. In that mode the input path is disabled, so reading always returns 0.

**Fix.** Use `GPIO_MODE_INPUT_OUTPUT`. Reading back now reflects the actual pad level.

---

### 9. The "CPU usage" column showed 97% for idle and confused everyone

**Symptom.** The system monitor listed `IDLE0 97%`, which reads as "the CPU is 97% busy" — the opposite of reality.

**Root cause.** The percentage is *the task's own CPU share*. The idle task running for 97% of the time means the CPU is **97% idle**. Presenting it alongside normal tasks inverts the meaning.

**Fix.** Filter idle tasks out of the list, and show a single **CPU load** figure at the top, computed as `100% − average idle share`.

**Related.** Summing all tasks exceeds 100% on a dual-core chip: `total_run` in `uxTaskGetSystemState` is wall-clock time, and two cores run in parallel, so per-task shares sum to roughly 200%. A single task is still capped at 100%.

---

### 10. A short tap is sometimes not recognised

**Symptom.** After the screen-off feature was added, occasionally a tap would not wake the display.

**Root cause.** The wake condition originally checked "is the touch currently pressed" every second. A quick tap can begin and end entirely between two polls.

**Fix.** Record a timestamp on every press and detect a **change** in that timestamp, rather than sampling the current state:

```c
if (last_press_ms != seen_ms) { wake(); }
```

---

### 11. Changing the partition table requires a full flash erase

**Symptom.** After switching from a single `factory` partition to `otadata` + dual OTA banks, flashing only the application leaves the device unbootable.

**Root cause.** The bootloader reads the partition table at 0x8000. The application binary alone does not update it, nor `otadata`, nor the bootloader that understands rollback.

**Fix.** Flash `bootloader.bin` (0x0), `partition-table.bin` (0x8000), `ota_data_initial.bin` (0xF000) and the application (0x20000) together — which is what `idf.py flash` does. A full 16 MB backup was taken before touching the partition table, and restoring it is the recovery path if anything goes wrong.

---

## Diagnostics built as a result

These pitfalls produced a set of tools rather than one-off fixes:

| Tool | Purpose |
|---|---|
| `[diag]` periodic log | Heap free / largest block / low-water mark / LVGL pool / fragmentation |
| `[font]` startup check | Reports missing glyph codepoints per screen |
| `APP_GPIO_SELFTEST` | Distinguishes broken pin / missing pull-up / unpowered module |
| `APP_FPS_PROBE` | Measures real frame rate with a forced full-screen redraw |
| `tools/check_glyphs.py` | Static pre-build check of font coverage |
| `tools/regen_font.py` | Regenerates the bitmap font from actual source strings |
| `tools/serial_capture.py` | Captures the full boot log by controlling RTS |
