# Software Architecture

[中文版](../zh/02-软件架构.md) · [Back to README](../../README.md)

---

## 1. Layering

```
┌──────────────────────────────────────────────┐
│  UI        ui_home / ui_weather / ui_settings│
│            ui_net / ui_wifi / ui_sysmon      │
├──────────────────────────────────────────────┤
│  App       app_io     GPIO abstraction       │
│            app_ota    A/B OTA state machine  │
│            app_sysinfo RAM/Flash/task data   │
│            app_screen idle screen-off        │
│            app_diag   memory & font checks   │
├──────────────────────────────────────────────┤
│  Network   net_wifi / net_time /             │
│            net_weather / net_web             │
├──────────────────────────────────────────────┤
│  Board     board_lcd / board_touch /         │
│            board_oled / board_backlight /    │
│            board_audio                       │
├──────────────────────────────────────────────┤
│  ESP-IDF   FreeRTOS / LVGL / lwIP / NVS      │
└──────────────────────────────────────────────┘
```

The rule is one-directional dependency: **UI never touches a register, a driver never knows about business logic**. `board_lcd.c` does not know that a weather page exists; `ui_weather.c` does not know that the panel is an ILI9341.

## 2. Task design

| Task | Priority | Core | Created by | Role |
|---|---|---|---|---|
| `wifi` | 23 | 0 | ESP-IDF | 802.11 state machine — hard real-time, must never be starved |
| `ipc0` / `ipc1` | 24 / 24 | 0 / 1 | ESP-IDF | Inter-processor call handling |
| `sys_evt` | 20 | 0 | ESP-IDF | System event dispatch (the task that runs `esp_event` handlers) |
| `esp_timer` | 22 | 0 | ESP-IDF | High-resolution timer callbacks |
| `httpd` | 5 | any | ESP-IDF | HTTP server, one connection at a time |
| `taskLVGL` | 5 | **1** | `esp_lvgl_port` | Rendering and touch polling, 30 ms period |
| `weather` | 3 | any | Application | One-shot HTTPS fetch, exits when done |
| `wifi_scan` | 3 | any | Application | One-shot scan, created on demand |
| `main` | 1 | any | Application | Initialisation, then blocks forever |
| `IDLE0` / `IDLE1` | 0 | 0 / 1 | FreeRTOS | Idle; reclaims deleted task stacks, feeds the task watchdog |

**Why LVGL is pinned to core 1.** The Wi-Fi stack lives on core 0. Rendering plus SPI transmission is CPU-intensive; putting it on the same core as the protocol stack causes packet loss and disconnects during full-screen redraws.

**Why no queues.** The application-level data exchanged between tasks is a handful of scalar state flags (`g_wifi_connected`, `g_brightness`). A 32-bit aligned read or write is atomic on Xtensa, so a `volatile` variable plus an event callback is sufficient. Queues would add copies and synchronisation for no benefit. If a producer/consumer imbalance appeared — say a 100 Hz sensor feeding a slow upload task — a queue would become necessary, and the design would change.

## 3. Memory budget

Available heap for the application is roughly **320 KB** of internal SRAM.

| Consumer | Size | Notes |
|---|---|---|
| LVGL memory pool | 72 KB (66 KB usable) | Objects, styles, animations, label text |
| Draw buffers (double) | 37.5 KB | 240 × 40 × 2 bytes × 2 |
| Wi-Fi + lwIP stacks | ~40-60 KB | Allocated at `WiFi_Init()` |
| Font (flash, not RAM) | — | `myfont_16.c` is `const`, lives in flash |
| **Free after everything is up** | **88 KB** | Largest contiguous block: 31 KB |

The figure that actually matters is the **largest contiguous block**, not total free. A TLS handshake needs roughly 16 KB of *contiguous* memory; total free can look healthy at 88 KB while fragmentation makes handshakes fail.

This is why the firmware logs both numbers every 10 minutes:

```
diag: RAM free=88KB 最大块=31KB 水位=40KB | LVGL池 free=14KB 峰值=52KB/65KB 碎片3%
```

(`水位` = historical minimum free heap — a hardware-tracked low-water mark that no sampling interval can miss.)

## 4. Key design decisions

### 4.1 Draw buffer: 40 lines, not 60

| Config | Buffer memory | Frame rate |
|---|---|---|
| 60 lines | 57.6 KB | 16 fps |
| 40 lines | 37.5 KB | **16 fps** |

Identical frame rate at 18.75 KB less memory. The bottleneck is `CONFIG_LV_DEF_REFR_PERIOD = 33 ms` (LVGL's refresh period), not the SPI transfer: a full frame is 153,600 bytes, which takes 30.7 ms at 40 MHz — already close to the period limit. Dividing the frame into 6 or 8 chunks does not change the total byte count.

### 4.2 All screens are created once and never destroyed

Switching is `lv_scr_load()`. Creating a screen on demand would introduce allocation latency and heap fragmentation; all nine screens together peak at 52 KB of the 66 KB pool. If the page count grows, this becomes the first thing to change.

### 4.3 A custom Chinese font, trimmed to the actual text

The built-in LVGL fonts contain no CJK glyphs. Rather than embedding a full 20,000-glyph set (megabytes), a 16 px bitmap font is generated containing exactly the characters used in the source code — currently **296 codepoints**, about 1 MB of flash as a `.c` array.

The trade-off: adding a new Chinese string requires regenerating the font. This is automated (`tools/check_glyphs.py` detects missing characters, `tools/regen_font.py` regenerates), and the firmware also performs a **runtime check** at startup — LVGL renders a missing glyph as a blank box without reporting anything, so the check walks every label on every screen and logs the exact codepoint of anything missing.

### 4.4 Thread safety around LVGL

LVGL is not thread-safe. Every call from a non-LVGL task must be wrapped:

```c
lvgl_port_lock(0);
Home_Init();
lvgl_port_unlock();
```

Without the lock, concurrent `lv_malloc` from two tasks corrupts the TLSF metadata, producing a `StoreProhibited` crash at a location that has nothing to do with the actual bug.

`lvgl_port_lock` is a **recursive** mutex, so a timer callback (already running in the LVGL task) can safely call it again.

### 4.5 Screen timeout

30 seconds of no touch turns the backlight off; any touch restores it immediately.

Deliberately **not** using light sleep: it would drop the Wi-Fi connection, and for a dashboard device that means weather stops updating and remote control goes offline, plus a 3-5 second reconnect on wake. Turning off the backlight alone halves power consumption (100-150 mA is the largest single consumer) while keeping the device fully online.

The touch controller keeps polling while the backlight is off — that is what makes "tap to wake" instantaneous.
