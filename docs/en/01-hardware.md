# Hardware and Wiring

[中文版](../zh/01-硬件与接线.md) · [Back to README](../../README.md)

---

## 1. Component selection

| Part | Why this part |
|---|---|
| **ESP32-S3** | Built-in Wi-Fi (no external module, no extra UART protocol), 240 MHz dual-core with enough headroom for LVGL, and a complete SDK (lwIP, mbedTLS, NVS, OTA) — none of which has to be ported |
| **ILI9341 2.8" 240×320** | Cheap, well-documented SPI panel; the resolution is enough for a dense dashboard while keeping a full frame at 153,600 bytes |
| **XPT2046** | Ships bonded to the panel, shares the SPI bus with the display, costs almost nothing |
| **SSD1306 128×32** | A separate always-on status line (time, temperature) that stays readable even when the main screen is asleep |
| **MAX98357A** | I²S amplifier with no configuration interface — three wires and it works |

The **PSRAM on this module is unusable**: the v0.2 chip revision has a silicon defect that causes random data corruption when external RAM is enabled. `CONFIG_SPIRAM` is therefore permanently disabled, and the whole design is constrained to internal SRAM.

## 2. Pin map

### SPI bus (SPI2) — shared by display and touch

| Signal | GPIO | Notes |
|---|---|---|
| SCK | 21 | 40 MHz |
| MOSI | 40 | Display data out |
| MISO | 35 | Touch data in (display never reads) |
| LCD_CS | 39 | Display chip select |
| LCD_DC | 38 | Data/command select |
| LCD_RST | 37 | Display reset |
| TOUCH_CS | 36 | Touch chip select |
| TOUCH_IRQ | 47 | Touch interrupt (active low) |

The display and the touch controller share SCK/MOSI/MISO but have **separate chip selects**. The ESP-IDF SPI driver arbitrates between the two devices automatically; each transaction is atomic.

> The touch controller runs at a much lower clock (1 MHz) than the display (40 MHz) — its SPI device handle is configured separately, not inherited from the display.

### I²C bus (I²C0) — status OLED

| Signal | GPIO |
|---|---|
| SCL | 42 |
| SDA | 41 |

### Other peripherals

| Function | GPIO | Notes |
|---|---|---|
| Backlight PWM | 1 | LEDC, 5 kHz, 8-bit |
| GPIO output 0-7 | 8, 9, 10, 11, 12, 13, 14, 18 | Push-pull, input+output mode |

### Why GPIO1 for the backlight

The panel's LED pin is wired to 3V3 from the factory, so PWM dimming requires **moving that wire to a GPIO**. GPIO1 was chosen because it is:

- broken out on the right-hand header
- a plain IO (not a strapping pin)
- not used by USB (D-/D+ are 19/20)
- not the on-board LED (48)

## 3. Power

| Rail | Load | Notes |
|---|---|---|
| 5 V | Input, 2 A adapter | Feeds the panel backlight and the amplifier |
| 3.3 V | ESP32-S3, OLED, touch | From the board regulator |

**All grounds must be common.** A floating ground between the display module and the MCU is the most common cause of "works on the bench, glitches when assembled".

## 4. Peripherals beyond the display

### GPIO outputs

Eight channels configured as `GPIO_MODE_INPUT_OUTPUT` rather than plain output. The reason is the **pin integrity self-test**: a pin configured as output-only always reads back 0, which would report a healthy pin as broken. Input-output mode allows reading back the actual level.

### Audio

I²S output to a MAX98357A. The audio path is implemented and was verified with a test tone, but is **not initialised by default** (`APP_AUDIO_TEST_TONE = 0`) to save about 6 KB of DMA descriptors. It is infrastructure that costs nothing when unused.

## 5. Wiring pitfalls

| Symptom | Cause |
|---|---|
| Screen stays white or shows random noise | SPI clock too high for the wiring; drop from 40 MHz to 26 MHz, then 20 MHz |
| Touch coordinates drift toward the edges | Resistive panel dead zone; needs linear calibration (see below) |
| OLED shows nothing, no error | I²C address wrong (0x3C vs 0x3D) or SDA/SCL swapped |
| Random reboots after assembly | Insufficient power supply or a missing common ground |

### Touch calibration

The ADC range of a resistive panel does not map linearly onto pixel coordinates, and the outer few millimetres respond poorly. Four calibration constants define the *usable* ADC window, which is then linearly mapped and clamped:

```c
#define CAL_X_MIN   25
#define CAL_X_MAX   215
#define CAL_Y_MIN   25
#define CAL_Y_MAX   295

cx = (raw_x - CAL_X_MIN) * 240 / (CAL_X_MAX - CAL_X_MIN);
if (cx < 0) cx = 0;
if (cx >= 240) cx = 239;
```

Without the clamp, pressing near an edge produces coordinates outside the screen and the LVGL hit-test fails silently.
