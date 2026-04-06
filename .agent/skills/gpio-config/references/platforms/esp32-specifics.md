# ESP32 Specifics Reference

## Table of Contents

- [Strapping Pins Deep Dive](#strapping-pins-deep-dive)
  - [GPIO0 — Boot Mode Selection](#gpio0--boot-mode-selection)
  - [GPIO2 — Boot Mode (Secondary)](#gpio2--boot-mode-secondary)
  - [GPIO5 — SDIO Timing](#gpio5--sdio-timing)
  - [GPIO12 (MTDI) — DANGER — Flash Voltage Selection](#gpio12-mtdi--danger--flash-voltage-selection)
  - [GPIO15 (MTDO) — Debug Log Control](#gpio15-mtdo--debug-log-control)
  - [Strapping Pin Summary Table](#strapping-pin-summary-table)
- [ADC2/WiFi Conflict Explained](#adc2wifi-conflict-explained)
  - [The Problem](#the-problem)
  - [Technical Details](#technical-details)
  - [ADC2 Channels Affected](#adc2-channels-affected)
  - [ADC1 Channels (Always Safe)](#adc1-channels-always-safe)
  - [Design Rules](#design-rules)
  - [Variants Without This Conflict](#variants-without-this-conflict)
- [Flash and PSRAM Pin Details](#flash-and-psram-pin-details)
  - [SPI Flash Pins (GPIO6-11) — NEVER USE](#spi-flash-pins-gpio6-11--never-use)
  - [PSRAM Pins (GPIO16-17) — WROVER Only](#psram-pins-gpio16-17--wrover-only)
- [Input-Only Pins](#input-only-pins)
  - [GPIO34, 35, 36, 39 — Hardware Limitations](#gpio34-35-36-39--hardware-limitations)
  - [What "Input Only" Means](#what-input-only-means)
  - [No Internal Pull Resistors](#no-internal-pull-resistors)
  - [Best Use Cases for Input-Only Pins](#best-use-cases-for-input-only-pins)
- [RTC Domain and Deep Sleep](#rtc-domain-and-deep-sleep)
  - [RTC GPIO Overview](#rtc-gpio-overview)
  - [Complete RTC GPIO List](#complete-rtc-gpio-list)
  - [Deep Sleep Wake Sources](#deep-sleep-wake-sources)
  - [Non-RTC GPIOs During Deep Sleep](#non-rtc-gpios-during-deep-sleep)
- [Variant-Specific Details](#variant-specific-details)
  - [ESP32-S2](#esp32-s2)
  - [ESP32-S3](#esp32-s3)
  - [ESP32-C3](#esp32-c3)
  - [ESP32-C6](#esp32-c6)
  - [Quick Variant Selection Guide](#quick-variant-selection-guide)

---

## Strapping Pins Deep Dive

ESP32 reads the state of five GPIO pins at boot to determine operating mode. These pins are safe to use for other purposes after boot completes, but their state at reset/power-on matters.

### GPIO0 — Boot Mode Selection

| State at Boot | Result |
|---------------|--------|
| HIGH (default) | Normal execution from flash |
| LOW | Download mode (firmware upload via UART) |

**Details:**
- Has internal pull-up resistor (default HIGH)
- Safe to use after boot for any function
- Commonly used for: buttons, capacitive touch, general I/O

**Warning:** If using GPIO0 as a button input, ensure it is not held LOW during reset/power-on, or the chip will enter download mode instead of running your program.

**Recovery:** If accidentally entering download mode, simply release GPIO0 and reset the chip.

---

### GPIO2 — Boot Mode (Secondary)

| State at Boot | Result |
|---------------|--------|
| LOW or floating | Required for download mode on some modules |
| HIGH | Normal boot |

**Details:**
- Often connected to onboard LED on development boards
- Safe to use after boot
- Commonly used for: status LED, general I/O

**Note:** On some older modules, GPIO2 must be LOW or floating to enter download mode along with GPIO0 LOW. Modern modules are less strict.

---

### GPIO5 — SDIO Timing

| State at Boot | Result |
|---------------|--------|
| HIGH (default) | Normal SDIO timing |
| LOW | Different SDIO timing (rarely needed) |

**Details:**
- Has internal pull-up resistor
- Rarely causes boot issues in practice
- Default VSPI chip select pin

**Usage:** Generally safe to use without concern. The SDIO timing affects SD card interface which most projects don't use in SDIO mode.

---

### GPIO12 (MTDI) — DANGER — Flash Voltage Selection

| State at Boot | Result |
|---------------|--------|
| **LOW (required)** | 3.3V flash voltage — CORRECT for most modules |
| **HIGH** | 1.8V flash voltage — WILL FAIL TO BOOT on 3.3V flash modules |

**THIS IS THE MOST DANGEROUS STRAPPING PIN**

**What happens if GPIO12 is HIGH at boot on a 3.3V flash module:**
1. ESP32 configures flash interface for 1.8V
2. Flash chip (which is 3.3V) cannot communicate properly
3. Chip fails to boot — appears completely dead/bricked
4. No serial output, no response

**Common causes of accidental HIGH on GPIO12:**
- External pull-up resistor
- Connected peripheral that drives HIGH at boot
- Floating pin picking up noise (rare)

**Safe usage patterns:**
- Use external pull-DOWN resistor if GPIO12 must be used
- Ensure any connected device doesn't drive HIGH during boot
- Use `espefuse.py` to permanently set flash voltage (irreversible)

**Recovery from "bricked" state:**
1. Disconnect anything from GPIO12
2. Add pull-down resistor (10kΩ to GND) if needed
3. Hold GPIO0 LOW
4. Power cycle the module
5. Flash should now be accessible via esptool

**Burning eFuse (permanent fix):**
```bash
# WARNING: This is IRREVERSIBLE
espefuse.py --port /dev/ttyUSB0 set_flash_voltage 3.3V
```

**Best practice:** Avoid GPIO12 entirely unless absolutely necessary. If you must use it, always add an external pull-down resistor.

---

### GPIO15 (MTDO) — Debug Log Control

| State at Boot | Result |
|---------------|--------|
| HIGH (default) | Normal boot messages on UART0 |
| LOW | Silence UART0 debug output at boot |

**Details:**
- Has internal pull-up resistor (default HIGH)
- Safe to use after boot
- Default HSPI chip select pin

**Usage:** Can be used to suppress boot messages if GPIO15 is held LOW at boot. After boot, functions normally as GPIO.

---

### Strapping Pin Summary Table

| GPIO | Function | Safe Default | Danger Level | Internal Pull |
|------|----------|--------------|--------------|---------------|
| 0 | Boot mode | HIGH | Low | Pull-up |
| 2 | Boot mode (secondary) | LOW/floating | Low | None |
| 5 | SDIO timing | HIGH | Very Low | Pull-up |
| **12** | **Flash voltage** | **LOW** | **CRITICAL** | None |
| 15 | Debug log | HIGH | Very Low | Pull-up |

---

## ADC2/WiFi Conflict Explained

### The Problem

ADC2 shares internal hardware resources with the WiFi RF calibration and transmission circuits. When WiFi (or Bluetooth on original ESP32) is active, ADC2 readings are unreliable or completely invalid.

### Technical Details

- ADC2 uses the same SAR (Successive Approximation Register) ADC that WiFi uses for RF calibration
- WiFi periodically recalibrates, interrupting ADC2 reads
- During WiFi TX, ADC2 returns garbage data
- This is a hardware limitation — cannot be fixed in software

### ADC2 Channels Affected

All ADC2 channels are affected when WiFi is active:

| Channel | GPIO |
|---------|------|
| ADC2_CH0 | GPIO4 |
| ADC2_CH1 | GPIO0 |
| ADC2_CH2 | GPIO2 |
| ADC2_CH3 | GPIO15 |
| ADC2_CH4 | GPIO13 |
| ADC2_CH5 | GPIO12 |
| ADC2_CH6 | GPIO14 |
| ADC2_CH7 | GPIO27 |
| ADC2_CH8 | GPIO25 |
| ADC2_CH9 | GPIO26 |

### ADC1 Channels (Always Safe)

ADC1 has its own dedicated hardware and works regardless of WiFi state:

| Channel | GPIO |
|---------|------|
| ADC1_CH0 | GPIO36 |
| ADC1_CH1 | GPIO37 |
| ADC1_CH2 | GPIO38 |
| ADC1_CH3 | GPIO39 |
| ADC1_CH4 | GPIO32 |
| ADC1_CH5 | GPIO33 |
| ADC1_CH6 | GPIO34 |
| ADC1_CH7 | GPIO35 |

### Design Rules

1. **If your project uses WiFi:** Only use ADC1 pins (GPIO32-39) for analog inputs
2. **If your project does NOT use WiFi:** ADC2 pins are fine
3. **If you need more than 8 analog inputs with WiFi:** Consider external ADC chip (e.g., ADS1115)

### Variants Without This Conflict

The following ESP32 variants do NOT have the ADC2/WiFi conflict:
- **ESP32-C3:** Single ADC, always available
- **ESP32-C6:** Single ADC, always available

Original ESP32, ESP32-S2, and ESP32-S3 all have the conflict.

---

## Flash and PSRAM Pin Details

### SPI Flash Pins (GPIO6-11) — NEVER USE

These pins are hardwired to the SPI flash memory chip inside the module. Using them will immediately crash the ESP32.

| GPIO | Flash Signal | What Happens If Used |
|------|--------------|----------------------|
| 6 | SPICLK | Immediate crash — flash clock disrupted |
| 7 | SPIQ (MISO) | Crash — flash data corruption |
| 8 | SPID (MOSI) | Crash — flash data corruption |
| 9 | SPIHD (Hold) | Crash — flash communication failure |
| 10 | SPIWP (Write Protect) | Crash — flash communication failure |
| 11 | SPICS0 (Chip Select) | Crash — flash deselected |

**Why these pins exist on the pinout:**
- For Compute Module variants that may use external flash
- For JTAG debugging (some overlap)
- They're simply not disconnectable on most modules

**Rule:** Never assign GPIO6-11 in your pin configuration. The validation scripts should reject any attempt to use them.

### PSRAM Pins (GPIO16-17) — WROVER Only

ESP32-WROVER and WROVER-B modules include onboard PSRAM (Pseudo-Static RAM) that uses GPIO16 and GPIO17.

| Module Type | GPIO16 | GPIO17 |
|-------------|--------|--------|
| ESP32-WROOM-32 | Usable | Usable |
| ESP32-WROOM-32D | Usable | Usable |
| ESP32-WROOM-32E | Usable | Usable |
| ESP32-WROVER | **Reserved** | **Reserved** |
| ESP32-WROVER-B | **Reserved** | **Reserved** |
| ESP32-WROVER-E | **Reserved** | **Reserved** |

**How to identify your module:**
- Check the module label/marking
- WROVER modules are physically larger
- Code check: `ESP.getPsramSize() > 0` indicates WROVER

**ESP32-S3 Note:** S3 variants with octal PSRAM use different pins (GPIO26-32 range). Always check the specific S3 module datasheet.

---

## Input-Only Pins

### GPIO34, 35, 36, 39 — Hardware Limitations

These four pins have permanent hardware restrictions:

| GPIO | Alias | Restriction | Reason |
|------|-------|-------------|--------|
| 34 | — | Input only | No output driver |
| 35 | — | Input only | No output driver |
| 36 | SENSOR_VP | Input only | No output driver |
| 39 | SENSOR_VN | Input only | No output driver |

### What "Input Only" Means

These pins CANNOT be used for:
- Digital output (LEDs, relays, etc.)
- I2C (requires bidirectional SDA, open-drain SCL)
- SPI MOSI or SCLK
- UART TX
- PWM output
- Any protocol requiring output capability

These pins CAN be used for:
- Digital input (buttons, switches)
- Analog input (ADC1 — all four are ADC1 channels)
- Interrupt input
- SPI MISO (input only by definition)
- UART RX (input only by definition)

### No Internal Pull Resistors

Unlike most ESP32 GPIOs, the input-only pins have no internal pull-up or pull-down resistors:

| GPIO | Internal Pull-Up | Internal Pull-Down |
|------|------------------|-------------------|
| 34 | None | None |
| 35 | None | None |
| 36 | None | None |
| 39 | None | None |

**Implication:** If using these pins for buttons or switches, you MUST add external pull-up or pull-down resistors. Floating inputs cause unreliable readings.

### Best Use Cases for Input-Only Pins

1. **Analog sensors** — All four are ADC1 channels, work with WiFi
2. **Buttons with external pull resistors** — Saves other pins for output
3. **Interrupt inputs** — Low-latency event detection
4. **Voltage sensing** — Using voltage dividers into ADC

---

## RTC Domain and Deep Sleep

### RTC GPIO Overview

Only specific GPIOs can function during deep sleep or wake the ESP32 from deep sleep. These are called RTC GPIOs because they're connected to the RTC (Real-Time Clock) power domain.

### Complete RTC GPIO List

| RTC GPIO | GPIO | Wake Capable | ULP Accessible | ADC | Touch |
|----------|------|--------------|----------------|-----|-------|
| RTC_GPIO0 | 36 | Yes | Yes | ADC1_CH0 | — |
| RTC_GPIO1 | 37 | Yes | Yes | ADC1_CH1 | — |
| RTC_GPIO2 | 38 | Yes | Yes | ADC1_CH2 | — |
| RTC_GPIO3 | 39 | Yes | Yes | ADC1_CH3 | — |
| RTC_GPIO4 | 34 | Yes | Yes | ADC1_CH6 | — |
| RTC_GPIO5 | 35 | Yes | Yes | ADC1_CH7 | — |
| RTC_GPIO6 | 25 | Yes | Yes | ADC2_CH8 | — |
| RTC_GPIO7 | 26 | Yes | Yes | ADC2_CH9 | — |
| RTC_GPIO8 | 33 | Yes | Yes | ADC1_CH5 | TOUCH8 |
| RTC_GPIO9 | 32 | Yes | Yes | ADC1_CH4 | TOUCH9 |
| RTC_GPIO10 | 4 | Yes | Yes | ADC2_CH0 | TOUCH0 |
| RTC_GPIO11 | 0 | Yes | Yes | ADC2_CH1 | TOUCH1 |
| RTC_GPIO12 | 2 | Yes | Yes | ADC2_CH2 | TOUCH2 |
| RTC_GPIO13 | 15 | Yes | Yes | ADC2_CH3 | TOUCH3 |
| RTC_GPIO14 | 13 | Yes | Yes | ADC2_CH4 | TOUCH4 |
| RTC_GPIO15 | 12 | Yes | Yes | ADC2_CH5 | TOUCH5 |
| RTC_GPIO16 | 14 | Yes | Yes | ADC2_CH6 | TOUCH6 |
| RTC_GPIO17 | 27 | Yes | Yes | ADC2_CH7 | TOUCH7 |

### Deep Sleep Wake Sources

**EXT0 Wake — Single Pin Level:**
- Uses one RTC GPIO
- Wakes on HIGH or LOW level (configurable)
- Simple to configure

```cpp
esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);  // Wake on HIGH
```

**EXT1 Wake — Multiple Pins:**
- Uses multiple RTC GPIOs
- Wake modes:
  - `ESP_EXT1_WAKEUP_ANY_HIGH` — Any pin goes HIGH
  - `ESP_EXT1_WAKEUP_ALL_LOW` — All pins are LOW

```cpp
uint64_t mask = (1ULL << 32) | (1ULL << 33);  // GPIO32 and GPIO33
esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
```

**Touch Pad Wake:**
- Any touch channel can wake from deep sleep
- Configure threshold before sleep

```cpp
esp_sleep_enable_touchpad_wakeup();
```

**ULP Coprocessor Wake:**
- ULP runs during deep sleep
- Can read ADC, toggle RTC GPIOs, make decisions
- Programmable wake conditions

### Non-RTC GPIOs During Deep Sleep

GPIOs that are NOT in the RTC domain (e.g., GPIO5, 16, 17, 18, 19, 21, 22, 23):
- Cannot trigger wake
- State may not be preserved during deep sleep
- Must be reconfigured after wake

**Design Rule:** If you need a GPIO to trigger wake from deep sleep, it MUST be an RTC GPIO.

---

## Variant-Specific Details

### ESP32-S2

**Key Differences from Original ESP32:**
- Single-core Xtensa LX7 (vs dual-core LX6)
- No Bluetooth (WiFi only)
- USB OTG support on GPIO19 (D-) and GPIO20 (D+)
- 43 GPIOs total (vs 34)
- ADC2/WiFi conflict still present
- 14 touch channels (vs 10)
- 2 DAC channels (same as original)

**USB OTG Pin Reservation:**
If using USB:
- GPIO19 = USB D-
- GPIO20 = USB D+
- Reserve both for USB functionality

**Strapping Pins:**
- GPIO0 — Boot mode (same as original)
- GPIO45 — VDD_SPI voltage
- GPIO46 — Boot mode/ROM logging

**Flash Pins (DO NOT USE):**
GPIO26-32 (different from original ESP32!)

---

### ESP32-S3

**Key Differences:**
- Dual-core Xtensa LX7
- USB OTG on GPIO19/20 (same as S2)
- 45 GPIOs total
- No DAC (unlike S2)
- AI acceleration (vector extensions)
- BLE 5.0 (no Classic Bluetooth)

**USB OTG Pin Reservation:**
Same as S2 — GPIO19 and GPIO20

**Octal PSRAM Notes:**
S3 variants with octal SPI PSRAM use more pins than original WROVER:
- GPIO26-32 may be reserved
- Check specific module datasheet

**Strapping Pins:**
- GPIO0 — Boot mode
- GPIO3 — JTAG signal source
- GPIO45 — VDD_SPI voltage
- GPIO46 — Boot mode/ROM logging

---

### ESP32-C3

**Major Architecture Change:**
- RISC-V single-core (not Xtensa)
- Only 22 GPIOs (GPIO0-GPIO21)
- Significantly reduced peripheral set

**Key Differences:**
- **No touch sensing** — capacitive touch not available
- **No DAC** — no analog output
- **No ADC2/WiFi conflict** — all ADC works with WiFi!
- BLE 5.0 only (no Classic Bluetooth)

**Strapping Pins:**
- GPIO2 — Boot mode
- GPIO8 — ROM code printing (also controls boot mode)
- GPIO9 — Boot mode

**Flash Pins (DO NOT USE):**
GPIO12-17 (different from original!)

**Pin Count Impact:**
With only 22 GPIOs, pin planning is critical. Prioritize essential functions.

---

### ESP32-C6

**Key Differences:**
- RISC-V single-core
- 30 GPIOs total
- **WiFi 6 (802.11ax)** — only ESP32 with WiFi 6
- **Thread/Zigbee (802.15.4)** — for IoT mesh networks
- BLE 5.0

**No ADC2/WiFi Conflict:**
Like C3, all ADC channels work with WiFi active.

**No Touch Sensing or DAC:**
Like C3, these features are absent.

**Strapping Pins:**
- GPIO8 — Boot mode
- GPIO9 — Boot mode
- GPIO15 — JTAG signal source

**Flash Pins (DO NOT USE):**
GPIO24-29

**Unique Capability — Thread/Zigbee:**
C6 can function as a Thread border router or Zigbee coordinator, useful for smart home applications.

---

### Quick Variant Selection Guide

| Use Case | Recommended Variant |
|----------|---------------------|
| General IoT with WiFi + BLE | ESP32 (original) or ESP32-S3 |
| USB device (HID, CDC) | ESP32-S2 or ESP32-S3 |
| Analog sensing + WiFi | ESP32-C3 or ESP32-C6 (no ADC conflict) |
| WiFi 6 requirement | ESP32-C6 |
| Thread/Zigbee mesh | ESP32-C6 |
| Touch sensing | ESP32, ESP32-S2, or ESP32-S3 |
| Analog output (DAC) | ESP32 or ESP32-S2 |
| Lowest cost | ESP32-C3 |
| AI/ML on edge | ESP32-S3 |

---
