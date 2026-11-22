# ESP32-S3 多功能信息终端

基于 **ESP32-S3 + FreeRTOS + LVGL 9.6** 的触摸屏信息终端，集成天气查询、网页远程控制、设备自监控，以及**带自动回滚的 A/B 分区 OTA 升级**。

[English](README.en.md) · [文档索引](#文档)

---

## 项目简介

一台常驻桌面的信息终端：显示天气、支持 8 路 GPIO 的屏幕与手机双端控制、上报自身运行状况，并且可以**不插线**通过 Wi-Fi 更新固件。

整个系统在**无 PSRAM** 的前提下运行——所用模组的片外 RAM 存在硬件缺陷不可用，因此每一字节内存都要与内部 SRAM（应用可用约 320KB）精打细算。

| 指标 | 数值 |
|---|---|
| 固件大小 | 1.58 MB |
| 可用堆（Wi-Fi + UI 初始化后） | 88 KB |
| 最大连续空闲块 | 31 KB |
| LVGL 内存池 | 72 KB（峰值 52 KB） |
| FreeRTOS 任务数 | 13 |
| 界面页数 | 9 |
| 全屏刷新率 | 16 fps（全屏动画最坏情况） |

## 功能

- **天气** — HTTPS 拉取公共天气接口，JSON 解析，四天预报
- **远程控制** — 内置 HTTP 服务器提供手机网页；8 路 GPIO 可从屏幕或手机操作
- **系统监控** — RAM/Flash 环形图，逐任务 CPU 占用与栈余量
- **OTA 升级** — 浏览器上传 `.bin`；双分区布局 + SHA-256 校验 + Bootloader 回滚
- **双屏显示** — 240×320 SPI 主屏 + 128×32 I²C OLED 副屏，两条总线独立互不阻塞
- **息屏省电** — 30 秒无触摸关闭背光，触摸立即唤醒（Wi-Fi 保持在线）
- **自诊断** — 堆水位定时日志、运行时字库完整性自检、帧率探针

## 硬件

| 部件 | 型号 | 接口 |
|---|---|---|
| 主控 | ESP32-S3（16MB Flash，**PSRAM 关闭**） | — |
| 主屏 | ILI9341，240×320，2.8 寸 | SPI2 @ 40MHz |
| 触摸 | XPT2046 电阻屏 | SPI2（共用），IRQ 接 GPIO47 |
| 副屏 | SSD1306，128×32 OLED | I²C0，SCL=GPIO42，SDA=GPIO41 |
| 音频 | MAX98357A I²S 功放 | I²S |
| 背光 | LEDC PWM 调光 | GPIO1 |
| GPIO 输出 | 8 路 | GPIO 8/9/10/11/12/13/14/18 |

完整引脚表与接线注意事项见 [`docs/zh/01-硬件与接线.md`](docs/zh/01-硬件与接线.md)

## 软件栈

| 层次 | 组件 |
|---|---|
| SDK | ESP-IDF v5.5.6 |
| 操作系统 | FreeRTOS（SMP 双核） |
| 图形 | LVGL 9.6 + `esp_lvgl_port` |
| 网络 | lwIP、`esp_http_server`、mbedTLS（证书包） |
| 存储 | NVS（设置项、Wi-Fi 凭据） |
| 升级 | `esp_ota_ops` + `BOOTLOADER_APP_ROLLBACK_ENABLE` |

## 架构

```
┌──────────────────────────────────────────────┐
│  UI 层         ui_home / ui_weather /        │
│                ui_settings / ui_net /        │
│                ui_sysmon                     │
├──────────────────────────────────────────────┤
│  应用层        app_io / app_ota /            │
│                app_sysinfo / app_screen /    │
│                app_diag                      │
├──────────────────────────────────────────────┤
│  网络层        net_wifi / net_weather /      │
│                net_time / net_web            │
├──────────────────────────────────────────────┤
│  驱动层        board_lcd / board_touch /     │
│                board_oled / board_backlight  │
├──────────────────────────────────────────────┤
│  ESP-IDF       FreeRTOS / LVGL / lwIP / NVS  │
└──────────────────────────────────────────────┘
```

任务划分、内存预算与设计取舍见 [`docs/zh/02-软件架构.md`](docs/zh/02-软件架构.md)

## 编译与烧写

### 前置条件

- 已安装并 export 的 **ESP-IDF v5.5.6**
- 与上表引脚一致的硬件（或自行改 `main/app_config.h`）

### 先填自己的凭据

Wi-Fi 账号密码与网页控制 token **不会**提交到仓库。编译前请修改 `main/app_config.h`：

```c
#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASS           "YOUR_WIFI_PASSWORD"
#define WEB_TOKEN           "CHANGE_ME_TOKEN"     /* 网页控制密码，留空则不校验 */
```

### 编译烧写

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <串口号> flash monitor
```

首次启动后设备会连接 Wi-Fi 并打印 IP：

```
I (3029) ui_net: 手机访问地址: http://192.168.1.100/
```

手机首次访问需带上 token 参数：

```
http://192.168.1.100/?k=CHANGE_ME_TOKEN
```

### OTA 升级

打开网页，滑到底部「固件升级」，选择 `big_screen.bin` 上传。设备写入备用分区、校验通过后重启进入新固件；若新固件 30 秒内未自确认，Bootloader 自动回滚。

原理与实测数据见 [`docs/zh/04-OTA升级实现.md`](docs/zh/04-OTA升级实现.md)

## 目录结构

```
.
├── main/
│   ├── Board/          硬件驱动（LCD / 触摸 / OLED / 背光 / 音频）
│   ├── Network/        网络（Wi-Fi / SNTP / 天气 / HTTP 服务）
│   ├── UI/             LVGL 界面
│   ├── app_*.c/h       应用逻辑（IO / OTA / 系统信息 / 诊断）
│   ├── main.c          初始化与任务编排
│   └── myfont_16.c     生成的中文点阵字库（296 个码点）
├── tools/              字库生成与校验、串口抓取脚本
├── docs/               中英文文档
├── partitions.csv      自定义分区表（otadata + 双 OTA 分区）
└── sdkconfig           完整配置，便于复现构建
```

## 关键技术点

**无 PSRAM 运行。** 模组片外 RAM 因芯片缺陷不可用，LVGL 内存池、绘图缓冲与全部业务数据都落在内部 SRAM 上。把绘图缓冲从 60 行降到 40 行回收了 18.75KB，而帧率**毫无变化**——瓶颈是 LVGL 的 33ms 刷新周期，不是 SPI 传输。

**真正兜底的 OTA。** 两个 4MB 应用分区加 8KB `otadata`。新固件写入非运行分区、SHA-256 校验通过后才标记启动；首次启动后必须在 30 秒内调用 `esp_ota_mark_app_valid_cancel_rollback()` 确认，否则 Bootloader 将其标记为 `ABORTED` 并回退——这一点经过实测验证，不只是理论。

**把诊断当功能做。** 与其盯着屏幕找问题，不如让固件自己报告：每 10 分钟输出一次堆水位、屏幕上实时显示任务 CPU 与栈余、启动时扫描全部文本并报出字库缺失的字符（LVGL 遇缺字只画空白框，不会报错）。

## 文档

| 文档 | 中文 | English |
|---|---|---|
| 硬件与接线 | [zh](docs/zh/01-硬件与接线.md) | [en](docs/en/01-hardware.md) |
| 软件架构与任务 | [zh](docs/zh/02-软件架构.md) | [en](docs/en/02-architecture.md) |
| 开发历程与踩坑 | [zh](docs/zh/03-开发历程与踩坑.md) | [en](docs/en/03-development-log.md) |
| OTA 升级实现 | [zh](docs/zh/04-OTA升级实现.md) | [en](docs/en/04-ota.md) |

## 许可

[MIT](LICENSE)
