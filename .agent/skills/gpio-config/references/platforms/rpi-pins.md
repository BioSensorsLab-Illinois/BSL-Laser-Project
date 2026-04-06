# Raspberry Pi GPIO Pin Reference

## Table of Contents

- [Pin Numbering Schemes](#pin-numbering-schemes)
- [40-Pin Header Layout](#40-pin-header-layout)
- [Power and Ground Pins](#power-and-ground-pins)
  - [Safety Notes](#safety-notes)
- [Reserved Pins](#reserved-pins)
  - [GPIO0 and GPIO1 (Pins 27, 28) — HAT EEPROM](#gpio0-and-gpio1-pins-27-28-hat-eeprom)
  - [GPIO14 and GPIO15 (Pins 8, 10) — UART/Bluetooth Conflict](#gpio14-and-gpio15-pins-8-10-uartbluetooth-conflict)
- [GPIO Pin Detail Table](#gpio-pin-detail-table)
  - [GPIO2](#gpio2)
  - [GPIO3](#gpio3)
  - [GPIO4](#gpio4)
  - [GPIO5](#gpio5)
  - [GPIO6](#gpio6)
  - [GPIO7](#gpio7)
  - [GPIO8](#gpio8)
  - [GPIO9](#gpio9)
  - [GPIO10](#gpio10)
  - [GPIO11](#gpio11)
  - [GPIO12](#gpio12)
  - [GPIO13](#gpio13)
  - [GPIO14](#gpio14)
  - [GPIO15](#gpio15)
  - [GPIO16](#gpio16)
  - [GPIO17](#gpio17)
  - [GPIO18](#gpio18)
  - [GPIO19](#gpio19)
  - [GPIO20](#gpio20)
  - [GPIO21](#gpio21)
  - [GPIO22](#gpio22)
  - [GPIO23](#gpio23)
  - [GPIO24](#gpio24)
  - [GPIO25](#gpio25)
  - [GPIO26](#gpio26)
  - [GPIO27](#gpio27)
- [Protocol Pin Groups](#protocol-pin-groups)
  - [I2C Buses](#i2c-buses)
  - [SPI Buses](#spi-buses)
  - [UARTs](#uarts)
  - [PWM](#pwm)
  - [1-Wire](#1-wire)
  - [PCM/I2S Audio](#pcmi2s-audio)
- [Critical Conflict Matrix](#critical-conflict-matrix)
  - [Pin-Level Conflicts](#pin-level-conflicts)
  - [Resource-Level Conflicts](#resource-level-conflicts)
- [Pi Model Differences](#pi-model-differences)
  - [Notes](#notes)

---

## Pin Numbering Schemes

Three numbering schemes exist for Raspberry Pi GPIO:

| Scheme | Description | Usage |
|--------|-------------|-------|
| **BCM** | Broadcom SoC GPIO numbers (0-27) | Default in this document, gpiozero, RPi.GPIO |
| **Physical** | Board header position (1-40) | Wiring, physical identification |
| **WiringPi** | Legacy library numbering | Deprecated, avoid in new projects |

**Example — GPIO17:**
- BCM = 17
- Physical = 11
- WiringPi = 0

This document uses **BCM numbering** throughout unless otherwise noted.

---

## 40-Pin Header Layout

```text
┌─────────────────────────────────────────────────────────────────┐
│                        USB/ETHERNET SIDE                        │
├─────────────────────────────────────────────────────────────────┤
│  Pin 1  = 3.3V Power          │  Pin 2  = 5V Power              │
│  Pin 3  = GPIO2 (SDA1)        │  Pin 4  = 5V Power              │
│  Pin 5  = GPIO3 (SCL1)        │  Pin 6  = GND                   │
│  Pin 7  = GPIO4 (GPCLK0)      │  Pin 8  = GPIO14 (TXD0)         │
│  Pin 9  = GND                 │  Pin 10 = GPIO15 (RXD0)         │
│  Pin 11 = GPIO17              │  Pin 12 = GPIO18 (PCM_CLK)      │
│  Pin 13 = GPIO27              │  Pin 14 = GND                   │
│  Pin 15 = GPIO22              │  Pin 16 = GPIO23                │
│  Pin 17 = 3.3V Power          │  Pin 18 = GPIO24                │
│  Pin 19 = GPIO10 (SPI0_MOSI)  │  Pin 20 = GND                   │
│  Pin 21 = GPIO9 (SPI0_MISO)   │  Pin 22 = GPIO25                │
│  Pin 23 = GPIO11 (SPI0_SCLK)  │  Pin 24 = GPIO8 (SPI0_CE0)      │
│  Pin 25 = GND                 │  Pin 26 = GPIO7 (SPI0_CE1)      │
│  Pin 27 = GPIO0 (ID_SD)       │  Pin 28 = GPIO1 (ID_SC)         │
│  Pin 29 = GPIO5               │  Pin 30 = GND                   │
│  Pin 31 = GPIO6               │  Pin 32 = GPIO12 (PWM0)         │
│  Pin 33 = GPIO13 (PWM1)       │  Pin 34 = GND                   │
│  Pin 35 = GPIO19 (SPI1_MISO)  │  Pin 36 = GPIO16 (SPI1_CE2)     │
│  Pin 37 = GPIO26              │  Pin 38 = GPIO20 (SPI1_MOSI)    │
│  Pin 39 = GND                 │  Pin 40 = GPIO21 (SPI1_SCLK)    │
├─────────────────────────────────────────────────────────────────┤
│                          SD CARD SIDE                           │
└─────────────────────────────────────────────────────────────────┘
```

---

## Power and Ground Pins

| Type | Pins | Specifications |
|------|------|----------------|
| **3.3V** | 1, 17 | Max 50mA from GPIO; regulator limit ~800mA shared with Pi |
| **5V** | 2, 4 | Direct from USB power input, no regulation |
| **GND** | 6, 9, 14, 20, 25, 30, 34, 39 | 8 ground pins total |
| **ID EEPROM** | 27, 28 | GPIO0/GPIO1 — reserved for HAT detection |

### Safety Notes

- All GPIO pins operate at **3.3V logic levels**
- **NEVER** connect 5V directly to any GPIO input — this will damage the SoC
- Use level shifters or voltage dividers when interfacing with 5V devices
- The 5V pins can supply significant current but are unprotected — add fuses for external devices

---

## Reserved Pins

### GPIO0 and GPIO1 (Pins 27, 28) — HAT EEPROM

- **Function:** I2C0 bus for HAT identification EEPROM
- **Status:** Reserved — never assign for general use
- **Exception:** May use if no HAT is present AND you explicitly need I2C0
- **Warning:** Assigning these pins may prevent HAT detection and cause boot issues

### GPIO14 and GPIO15 (Pins 8, 10) — UART/Bluetooth Conflict

| Model | Conflict Status |
|-------|-----------------|
| Pi 3B/3B+ | UART0 and Bluetooth share GPIO14/15 |
| Pi 4B | UART0 and Bluetooth share GPIO14/15 |
| Pi Zero 2W | UART0 and Bluetooth share GPIO14/15 |
| Pi 5 | **No conflict** — separate UART and BT controllers |

**To use UART0 for serial on Pi 3/4/Zero2W:**
- Option A: `dtoverlay=miniuart-bt` — moves BT to mini-UART (unstable baud)
- Option B: `dtoverlay=disable-bt` — disables Bluetooth entirely

---

## GPIO Pin Detail Table

### GPIO2
- **Physical Pin:** 3
- **ALT0:** SDA1 | **ALT1:** SA3 | **ALT2:** LCD_VSYNC | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** I2C1 data line
- **Conflicts:** Cannot use as GPIO when I2C1 is enabled

### GPIO3
- **Physical Pin:** 5
- **ALT0:** SCL1 | **ALT1:** SA2 | **ALT2:** LCD_HSYNC | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** I2C1 clock line
- **Conflicts:** Cannot use as GPIO when I2C1 is enabled

### GPIO4
- **Physical Pin:** 7
- **ALT0:** GPCLK0 | **ALT1:** SA1 | **ALT2:** LCD_DEN | **ALT3:** Reserved | **ALT4:** ARM_TDI | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** 1-Wire default pin, general-purpose clock
- **Conflicts:** UART3 RX on Pi 4/5

### GPIO5
- **Physical Pin:** 29
- **ALT0:** GPCLK1 | **ALT1:** SA0 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** ARM_TDO | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** General-purpose clock
- **Conflicts:** UART3 TX on Pi 4/5

### GPIO6
- **Physical Pin:** 31
- **ALT0:** GPCLK2 | **ALT1:** SOE_N | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** ARM_RTCK | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** General-purpose clock
- **Conflicts:** I2C4 SDA on Pi 4/5

### GPIO7
- **Physical Pin:** 26
- **ALT0:** SPI0_CE1 | **ALT1:** SWE_N | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** SPI0 chip enable 1
- **Conflicts:** I2C4 SCL on Pi 4/5

### GPIO8
- **Physical Pin:** 24
- **ALT0:** SPI0_CE0 | **ALT1:** SD0 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Up
- **Primary Use:** SPI0 chip enable 0
- **Conflicts:** UART4 TX on Pi 4/5

### GPIO9
- **Physical Pin:** 21
- **ALT0:** SPI0_MISO | **ALT1:** SD1 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** SPI0 data in (MISO)
- **Conflicts:** UART4 RX on Pi 4/5

### GPIO10
- **Physical Pin:** 19
- **ALT0:** SPI0_MOSI | **ALT1:** SD2 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** SPI0 data out (MOSI)
- **Conflicts:** None significant

### GPIO11
- **Physical Pin:** 23
- **ALT0:** SPI0_SCLK | **ALT1:** SD3 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** Reserved | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** SPI0 clock
- **Conflicts:** None significant

### GPIO12
- **Physical Pin:** 32
- **ALT0:** PWM0 | **ALT1:** SD4 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** ARM_TMS | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** Hardware PWM channel 0 (preferred)
- **Conflicts:** I2C5 SDA, UART5 TX on Pi 4/5

### GPIO13
- **Physical Pin:** 33
- **ALT0:** PWM1 | **ALT1:** SD5 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** ARM_TCK | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** Hardware PWM channel 1 (preferred)
- **Conflicts:** I2C5 SCL, UART5 RX on Pi 4/5

### GPIO14
- **Physical Pin:** 8
- **ALT0:** TXD0 | **ALT1:** SD6 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** TXD1 | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** UART0 transmit
- **Conflicts:** Bluetooth on Pi 3/4/Zero2W

### GPIO15
- **Physical Pin:** 10
- **ALT0:** RXD0 | **ALT1:** SD7 | **ALT2:** Reserved | **ALT3:** Reserved | **ALT4:** RXD1 | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** UART0 receive
- **Conflicts:** Bluetooth on Pi 3/4/Zero2W

### GPIO16
- **Physical Pin:** 36
- **ALT0:** Reserved | **ALT1:** SD8 | **ALT2:** Reserved | **ALT3:** CTS0 | **ALT4:** SPI1_CE2 | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** SPI1 chip enable 2
- **Conflicts:** None significant

### GPIO17
- **Physical Pin:** 11
- **ALT0:** Reserved | **ALT1:** SD9 | **ALT2:** Reserved | **ALT3:** RTS0 | **ALT4:** SPI1_CE1 | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO, SPI1 chip enable 1
- **Conflicts:** None significant — clean GPIO

### GPIO18
- **Physical Pin:** 12
- **ALT0:** PCM_CLK | **ALT1:** SD10 | **ALT2:** Reserved | **ALT3:** BSCSL_SDA | **ALT4:** SPI1_CE0 | **ALT5:** PWM0
- **Default Pull:** Down
- **Primary Use:** PCM/I2S clock, alternate PWM0
- **Conflicts:** PCM ↔ SPI1_CE0 ↔ PWM0 — three-way conflict

### GPIO19
- **Physical Pin:** 35
- **ALT0:** PCM_FS | **ALT1:** SD11 | **ALT2:** Reserved | **ALT3:** BSCSL_SCL | **ALT4:** SPI1_MISO | **ALT5:** PWM1
- **Default Pull:** Down
- **Primary Use:** PCM/I2S frame sync, alternate PWM1
- **Conflicts:** PCM ↔ SPI1_MISO ↔ PWM1 — three-way conflict

### GPIO20
- **Physical Pin:** 38
- **ALT0:** PCM_DIN | **ALT1:** SD12 | **ALT2:** Reserved | **ALT3:** BSCSL_MISO | **ALT4:** SPI1_MOSI | **ALT5:** GPCLK0
- **Default Pull:** Down
- **Primary Use:** PCM/I2S data in
- **Conflicts:** PCM ↔ SPI1_MOSI

### GPIO21
- **Physical Pin:** 40
- **ALT0:** PCM_DOUT | **ALT1:** SD13 | **ALT2:** Reserved | **ALT3:** BSCSL_CE | **ALT4:** SPI1_SCLK | **ALT5:** GPCLK1
- **Default Pull:** Down
- **Primary Use:** PCM/I2S data out
- **Conflicts:** PCM ↔ SPI1_SCLK

### GPIO22
- **Physical Pin:** 15
- **ALT0:** Reserved | **ALT1:** SD14 | **ALT2:** Reserved | **ALT3:** SD1_CLK | **ALT4:** ARM_TRST | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** I2C6 SDA on Pi 4/5 — clean otherwise

### GPIO23
- **Physical Pin:** 16
- **ALT0:** Reserved | **ALT1:** SD15 | **ALT2:** Reserved | **ALT3:** SD1_CMD | **ALT4:** ARM_RTCK | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** I2C6 SCL on Pi 4/5 — clean otherwise

### GPIO24
- **Physical Pin:** 18
- **ALT0:** Reserved | **ALT1:** SD16 | **ALT2:** Reserved | **ALT3:** SD1_DAT0 | **ALT4:** ARM_TDO | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** None — clean GPIO

### GPIO25
- **Physical Pin:** 22
- **ALT0:** Reserved | **ALT1:** SD17 | **ALT2:** Reserved | **ALT3:** SD1_DAT1 | **ALT4:** ARM_TCK | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** None — clean GPIO

### GPIO26
- **Physical Pin:** 37
- **ALT0:** Reserved | **ALT1:** Reserved | **ALT2:** Reserved | **ALT3:** SD1_DAT2 | **ALT4:** ARM_TDI | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** None — clean GPIO

### GPIO27
- **Physical Pin:** 13
- **ALT0:** Reserved | **ALT1:** Reserved | **ALT2:** Reserved | **ALT3:** SD1_DAT3 | **ALT4:** ARM_TMS | **ALT5:** Reserved
- **Default Pull:** Down
- **Primary Use:** General GPIO
- **Conflicts:** None — clean GPIO

---

## Protocol Pin Groups

### I2C Buses

| Bus | SDA | SCL | Enable | Notes |
|-----|-----|-----|--------|-------|
| I2C0 | GPIO0 (pin 27) | GPIO1 (pin 28) | Reserved | HAT EEPROM — never use |
| I2C1 | GPIO2 (pin 3) | GPIO3 (pin 5) | `dtparam=i2c_arm=on` | Primary user bus |
| I2C3 (Pi 4/5) | varies | varies | `dtoverlay=i2c3` | Additional bus |
| I2C4 (Pi 4/5) | GPIO6 (pin 31) | GPIO7 (pin 26) | `dtoverlay=i2c4` | Conflicts SPI0_CE1 |
| I2C5 (Pi 4/5) | GPIO12 (pin 32) | GPIO13 (pin 33) | `dtoverlay=i2c5` | Conflicts PWM0/1 |
| I2C6 (Pi 4/5) | GPIO22 (pin 15) | GPIO23 (pin 16) | `dtoverlay=i2c6` | Clean — no conflicts |

### SPI Buses

| Bus | MOSI | MISO | SCLK | CE0 | CE1 | CE2 | Enable |
|-----|------|------|------|-----|-----|-----|--------|
| SPI0 | GPIO10 (19) | GPIO9 (21) | GPIO11 (23) | GPIO8 (24) | GPIO7 (26) | — | `dtparam=spi=on` |
| SPI1 | GPIO20 (38) | GPIO19 (35) | GPIO21 (40) | GPIO18 (12) | GPIO17 (11) | GPIO16 (36) | `dtoverlay=spi1-Ncs` |

### UARTs

| UART | TX | RX | Enable | Notes |
|------|----|----|--------|-------|
| UART0 (PL011) | GPIO14 (pin 8) | GPIO15 (pin 10) | `enable_uart=1` | BT conflict Pi 3/4/Zero2W |
| UART1 (mini) | GPIO14 (pin 8) | GPIO15 (pin 10) | — | Unstable baud rate |
| UART2 (Pi 4/5) | GPIO0 (pin 27) | GPIO1 (pin 28) | `dtoverlay=uart2` | Conflicts I2C0! |
| UART3 (Pi 4/5) | GPIO4 (pin 7) | GPIO5 (pin 29) | `dtoverlay=uart3` | Conflicts 1-Wire default |
| UART4 (Pi 4/5) | GPIO8 (pin 24) | GPIO9 (pin 21) | `dtoverlay=uart4` | Conflicts SPI0! |
| UART5 (Pi 4/5) | GPIO12 (pin 32) | GPIO13 (pin 33) | `dtoverlay=uart5` | Conflicts PWM! |

### PWM

| Channel | Primary Pin | Alternate Pin | Enable |
|---------|-------------|---------------|--------|
| PWM0 | GPIO12 (pin 32) ALT0 | GPIO18 (pin 12) ALT5 | `dtoverlay=pwm` |
| PWM1 | GPIO13 (pin 33) ALT0 | GPIO19 (pin 35) ALT5 | `dtoverlay=pwm-2chan` |

**Note:** Prefer GPIO12/13 for PWM. Using GPIO18/19 for PWM disables analog audio output (3.5mm jack).

### 1-Wire

- **Default Pin:** GPIO4 (pin 7)
- **Enable:** `dtoverlay=w1-gpio`
- **Requires:** 4.7kΩ pull-up resistor to 3.3V
- **Alternate:** Any GPIO via `dtoverlay=w1-gpio,gpiopin=N`

### PCM/I2S Audio

| Signal | GPIO | Pin |
|--------|------|-----|
| PCM_CLK | GPIO18 | 12 |
| PCM_FS | GPIO19 | 35 |
| PCM_DIN | GPIO20 | 38 |
| PCM_DOUT | GPIO21 | 40 |

**Warning:** PCM/I2S conflicts with entire SPI1 bus — cannot use both simultaneously.

---

## Critical Conflict Matrix

### Pin-Level Conflicts

| GPIO | Function A | Function B | Function C |
|------|------------|------------|------------|
| 18 | PCM_CLK (ALT0) | SPI1_CE0 (ALT4) | PWM0 (ALT5) |
| 19 | PCM_FS (ALT0) | SPI1_MISO (ALT4) | PWM1 (ALT5) |
| 20 | PCM_DIN (ALT0) | SPI1_MOSI (ALT4) | — |
| 21 | PCM_DOUT (ALT0) | SPI1_SCLK (ALT4) | — |
| 14 | UART0_TX (ALT0) | Bluetooth* | — |
| 15 | UART0_RX (ALT0) | Bluetooth* | — |

*Pi 3/4/Zero 2W only. Pi 5 has no UART/BT conflict.

### Resource-Level Conflicts

| Resource A | Resource B | Conflict Description |
|------------|------------|----------------------|
| HW PWM (any) | Analog audio (3.5mm) | Mutually exclusive — PWM disables audio |
| SPI1 (any pin) | PCM/I2S | Share GPIO18-21 |
| UART0 PL011 | Bluetooth | Pi 3/4/Zero2W only |
| I2C4 | SPI0_CE1 | Share GPIO7 |
| I2C5 | PWM0/PWM1 | Share GPIO12/13 |
| UART3 | 1-Wire default | Share GPIO4 |
| UART4 | SPI0 | Share GPIO8/9 |
| UART5 | PWM | Share GPIO12/13 |

---

## Pi Model Differences

| Feature | Pi 3B+ | Pi 4B | Pi 5 | Pi Zero 2W |
|---------|--------|-------|------|------------|
| SoC | BCM2837B0 | BCM2711 | BCM2712 | BCM2710A1 |
| I2C buses | 2 | 6 | 6 | 2 |
| SPI buses | 2 | 7 | 5 | 2 |
| UARTs | 2 | 6 | 5 | 2 |
| HW PWM channels | 2 | 2 | 4 | 2 |
| UART/BT conflict | Yes | Yes | No | Yes |
| Config path | /boot/config.txt | /boot/config.txt or /boot/firmware/config.txt | /boot/firmware/config.txt | /boot/config.txt |
| RP1 south bridge | No | No | Yes | No |

### Notes

- I2C0 is reserved on all models for HAT EEPROM
- User-accessible I2C starts at I2C1
- Pi 4 with Bookworm uses `/boot/firmware/config.txt`; older OS uses `/boot/config.txt`
- Pi 5's RP1 southbridge means different peripheral base addresses — most userspace code is unaffected
- GPIO28+ exist on Compute Modules only — not covered in this document
