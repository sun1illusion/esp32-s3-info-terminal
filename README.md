# ESP32-S3 Information Terminal

A touch-screen information dashboard built on **ESP32-S3 + FreeRTOS + LVGL 9.6**, featuring a weather widget, remote GPIO control over HTTP, an on-device system monitor, and **A/B OTA updates with automatic rollback**.

[中文文档](README.zh-CN.md) · [Documentation](#documentation)

---

## Overview

This is a standalone desk terminal that displays weather, provides 8 channels of GPIO control from both the touch screen and a phone browser, reports its own runtime health, and can update its firmware over Wi-Fi without a cable.

The whole system runs **without PSRAM** — the module used here has a hardware defect that makes external RAM unusable, so every byte is accounted for against the ~320 KB of internal SRAM available to the application.

| Metric | Value |
|---|---|
| Firmware size | 1.58 MB |
| Free heap (after Wi-Fi + UI init) | 88 KB |
| Largest free block | 31 KB |
| LVGL memory pool | 72 KB (peak 52 KB) |
| FreeRTOS tasks | 13 |
| UI screens | 9 |
| Full-screen refresh rate | 16 fps (worst case, full-screen animation) |

## Features

- **Weather** — HTTPS fetch from a public weather API, JSON parsing, 4-day forecast
- **Remote control** — built-in HTTP server serves a mobile web UI; 8 GPIO channels controllable from screen or phone
- **System monitor** — live RAM/Flash ring gauges, per-task CPU usage and stack headroom
- **OTA updates** — upload a `.bin` from the browser; dual-bank layout with SHA-256 verification and bootloader rollback
- **Dual display** — 240×320 SPI main screen plus a 128×32 I²C OLED status display on independent buses
- **Screen timeout** — backlight off after 30 s of inactivity, instant wake on touch (Wi-Fi stays connected)
- **Self-diagnostics** — heap watermark logging, runtime font-coverage check, FPS probe

## Hardware

| Component | Part | Interface |
|---|---|---|
| MCU | ESP32-S3 (16 MB flash, **PSRAM disabled**) | — |
| Main display | ILI9341, 240×320, 2.8" | SPI2 @ 40 MHz |
| Touch | XPT2046 resistive | SPI2 (shared), IRQ on GPIO47 |
| Status display | SSD1306, 128×32 OLED | I²C0, SCL=GPIO42, SDA=GPIO41 |
| Audio | MAX98357A I²S amplifier | I²S |
| Backlight | LEDC PWM | GPIO1 |
| GPIO outputs | 8 channels | GPIO 8, 9, 10, 11, 12, 13, 14, 18 |

Full pin map and wiring notes: [`docs/en/01-hardware.md`](docs/en/01-hardware.md)

## Software stack

| Layer | Component |
|---|---|
| SDK | ESP-IDF v5.5.6 |
| RTOS | FreeRTOS (SMP, dual-core) |
| GUI | LVGL 9.6 + `esp_lvgl_port` |
| Networking | lwIP, `esp_http_server`, mbedTLS (certificate bundle) |
| Storage | NVS (settings, Wi-Fi credentials) |
| OTA | `esp_ota_ops` with `BOOTLOADER_APP_ROLLBACK_ENABLE` |

## Architecture

```
┌──────────────────────────────────────────────┐
│  UI layer      ui_home / ui_weather /        │
│                ui_settings / ui_net /        │
│                ui_sysmon                     │
├──────────────────────────────────────────────┤
│  App layer     app_io / app_ota /            │
│                app_sysinfo / app_screen /    │
│                app_diag                      │
├──────────────────────────────────────────────┤
│  Network       net_wifi / net_weather /      │
│                net_time / net_web            │
├──────────────────────────────────────────────┤
│  Board         board_lcd / board_touch /     │
│                board_oled / board_backlight  │
├──────────────────────────────────────────────┤
│  ESP-IDF       FreeRTOS / LVGL / lwIP / NVS  │
└──────────────────────────────────────────────┘
```

Task design, memory budget and design trade-offs: [`docs/en/02-architecture.md`](docs/en/02-architecture.md)

## Getting started

### Prerequisites

- ESP-IDF **v5.5.6** installed and exported
- A board matching the pin map above (or adjust `main/app_config.h`)

### Configure your credentials

Wi-Fi credentials and the web control token are **not** committed to this repository. Before building, edit `main/app_config.h`:

```c
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASS           "YOUR_WIFI_PASSWORD"
#define WEB_TOKEN           "CHANGE_ME_TOKEN"     /* web UI password; leave "" to disable */
```

### Build and flash

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

On first boot the device connects to Wi-Fi and prints its IP address:

```
I (3029) ui_net: Web UI: http://192.168.1.100/
```

Open that address on a phone, appending `?k=<WEB_TOKEN>` on first visit:

```
http://192.168.1.100/?k=CHANGE_ME_TOKEN
```

### OTA update

Open the web UI, scroll to **Firmware update**, select a `big_screen.bin` and upload. The device writes to the inactive partition, verifies it, then reboots into the new image. If the new firmware fails to confirm itself within 30 s, the bootloader rolls back automatically.

Details and measured timings: [`docs/en/04-ota.md`](docs/en/04-ota.md)

## Project structure

```
.
├── main/
│   ├── Board/          Hardware drivers (LCD, touch, OLED, backlight, audio)
│   ├── Network/        Wi-Fi, SNTP, weather API, HTTP server
│   ├── UI/             LVGL screens
│   ├── app_*.c/h       Application logic (IO, OTA, sysinfo, diagnostics)
│   ├── main.c          Initialisation and task orchestration
│   └── myfont_16.c     Generated Chinese bitmap font (296 glyphs)
├── tools/              Font generation / verification, serial capture
├── docs/               English and Chinese documentation
├── partitions.csv      Custom partition table (otadata + dual OTA banks)
└── sdkconfig           Full configuration for reproducible builds
```

## Key technical decisions

**Running without PSRAM.** The module's external RAM is unusable due to a silicon defect, so the LVGL pool, draw buffers and all application data live in internal SRAM. Reducing the draw buffer from 60 to 40 lines recovered 18.75 KB with **zero** frame-rate impact — the bottleneck is the 33 ms LVGL refresh period, not the SPI transfer.

**OTA with a real safety net.** Two 4 MB application partitions plus an 8 KB `otadata` region. A new image is written to the inactive partition, verified by SHA-256, then marked for boot. It must call `esp_ota_mark_app_valid_cancel_rollback()` within 30 s of first boot, otherwise the bootloader marks it `ABORTED` and falls back — verified in practice, not just in theory.

**Diagnostics as a first-class feature.** Rather than discovering problems by looking at the screen, the firmware reports them: heap watermark every 10 minutes, per-task CPU and stack headroom on screen, and a startup scan that reports any character missing from the bitmap font (LVGL renders missing glyphs as blank boxes without raising an error).

## Documentation

| Document | English | 中文 |
|---|---|---|
| Hardware & wiring | [en](docs/en/01-hardware.md) | [zh](docs/zh/01-硬件与接线.md) |
| Architecture & tasks | [en](docs/en/02-architecture.md) | [zh](docs/zh/02-软件架构.md) |
| Development log & pitfalls | [en](docs/en/03-development-log.md) | [zh](docs/zh/03-开发历程与踩坑.md) |
| OTA implementation | [en](docs/en/04-ota.md) | [zh](docs/zh/04-OTA升级实现.md) |

## License

[MIT](LICENSE)
