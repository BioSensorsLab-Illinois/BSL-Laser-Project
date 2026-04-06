# Raspberry Pi Device Tree Overlay Reference

Quick reference for dtoverlay and dtparam lines in config.txt. Used for GPIO configuration, conflict detection, and pin claim tracking.

```
config.txt location by platform:

Pi 3 / Zero 2W:               /boot/config.txt
Pi 4 (Bullseye and earlier):  /boot/config.txt
Pi 4 (Bookworm and later):    /boot/firmware/config.txt
Pi 5:                         /boot/firmware/config.txt
```

## Table of Contents

- [Group 1: I2C Overlays](#group-1-i2c-overlays)
  - [dtparam=i2c_arm](#dtparami2c_arm-i2c1--default-user-i2c-bus)
  - [i2c-gpio](#i2c-gpio-bitbang-i2c-on-arbitrary-pins)
  - [i2c-mux](#i2c-mux-i2c-multiplexer--tca9548a)
- [Group 2: SPI Overlays](#group-2-spi-overlays)
  - [dtparam=spi](#dtparamspi-spi0--default-spi-bus)
  - [spi1-1cs / spi1-2cs / spi1-3cs](#spi1-1cs--spi1-2cs--spi1-3cs-spi1--additional-spi-bus)
- [Group 3: UART Overlays](#group-3-uart-overlays)
  - [miniuart-bt](#miniuart-bt-move-bluetooth-to-mini-uart)
  - [disable-bt](#disable-bt-disable-bluetooth-entirely)
  - [uart2 / uart3 / uart4 / uart5](#uart2--uart3--uart4--uart5-additional-hardware-uarts)
- [Group 4: PWM Overlays](#group-4-pwm-overlays)
  - [pwm](#pwm-single-pwm-channel)
  - [pwm-2chan](#pwm-2chan-dual-pwm-channels)
- [Group 5: 1-Wire Overlays](#group-5-1-wire-overlays)
  - [w1-gpio](#w1-gpio-1-wire-bus)
  - [w1-gpio-pullup](#w1-gpio-pullup-1-wire-with-strong-pull-up-for-parasitic-power)
- [Group 6: CAN Bus Overlays](#group-6-can-bus-overlays)
  - [mcp2515-can0](#mcp2515-can0-can-bus-via-mcp2515--primary)
  - [mcp2515-can1](#mcp2515-can1-can-bus-via-mcp2515--secondary)
- [Group 7: Peripheral Overlays](#group-7-peripheral-overlays)
  - [sc16is750-i2c / sc16is752-i2c](#sc16is750-i2c--sc16is752-i2c-i2c-to-uart-bridge)
  - [enc28j60](#enc28j60-spi-ethernet-controller)
  - [gpio-fan](#gpio-fan-gpio-controlled-fan)
  - [gpio-ir / gpio-ir-tx](#gpio-ir--gpio-ir-tx-infrared-receivertransmitter)
  - [pps-gpio](#pps-gpio-pulse-per-second-for-gps-timing)
  - [sdio](#sdio-additional-sd-card-interface)
- [Overlay Conflict Matrix](#overlay-conflict-matrix)

---

## Group 1: I2C Overlays

### dtparam=i2c_arm (I2C1 — Default User I2C Bus)

| Field | Value |
|-------|-------|
| config.txt line | `dtparam=i2c_arm=on` and optionally `dtparam=i2c_arm_baudrate=400000` |
| Purpose | Enable the primary user I2C bus (I2C1) |
| Pins Claimed | GPIO2 (SDA1), GPIO3 (SCL1) |
| Parameters | `i2c_arm=on|off` — enable/disable. `i2c_arm_baudrate=N` — bus speed in Hz (default 100000, common: 100000, 400000) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- I2C0 (GPIO0/1) is reserved for HAT EEPROM identification — never assign user devices to I2C0
- Enabled by default on most distributions. Adding explicitly ensures it's on
- 400kHz (Fast mode) works for most devices. Some (e.g., HTU21D) require 100kHz
- On Pi 5, I2C1 is still GPIO2/3 but hardware is RP1 chip. Same dtparam syntax works

---

### i2c-gpio (Bitbang I2C on Arbitrary Pins)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=i2c-gpio,bus=3,i2c_gpio_sda=23,i2c_gpio_scl=24,i2c_gpio_delay_us=2` |
| Purpose | Create an additional I2C bus on any two GPIO pins using software bitbanging |
| Pins Claimed | Whichever two GPIOs are specified (user-configurable) |
| Parameters | `bus=N` — I2C bus number (use 3+ to avoid conflict with I2C0/1/2). `i2c_gpio_sda=N` — SDA GPIO. `i2c_gpio_scl=N` — SCL GPIO. `i2c_gpio_delay_us=N` — half-clock period in µs (2 = ~250kHz, 5 = ~100kHz) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Useful for second I2C bus to avoid address conflicts (e.g., two BME280 at 0x76)
- Software bitbanging — slower and more CPU-intensive than hardware I2C
- On Pi 4/5, prefer hardware I2C buses (i2c3, i2c4, i2c5, i2c6 overlays) over bitbang

---

### i2c-mux (I2C Multiplexer — TCA9548A)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=i2c-mux,pca9548a,addr=0x70` |
| Purpose | Configure a TCA9548A/PCA9548A I2C multiplexer to create up to 8 sub-buses |
| Pins Claimed | None beyond parent I2C bus (GPIO2/3 for I2C1). Mux is an I2C device itself |
| Parameters | `pca9548a` — chip type (also `pca9545a`, `pca9543a`). `addr=0xNN` — I2C address of mux (0x70–0x77) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Primary solution for I2C address conflicts. Each mux channel is isolated
- Mux itself occupies I2C address 0x70–0x77. Up to 8 muxes = 64 sub-buses
- Sub-buses appear as `/dev/i2c-N` devices. Channel selection is automatic

---

## Group 2: SPI Overlays

### dtparam=spi (SPI0 — Default SPI Bus)

| Field | Value |
|-------|-------|
| config.txt line | `dtparam=spi=on` |
| Purpose | Enable SPI0 bus |
| Pins Claimed | GPIO7 (CE1), GPIO8 (CE0), GPIO9 (MISO), GPIO10 (MOSI), GPIO11 (SCLK) |
| Parameters | `spi=on|off` — enable/disable |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- SPI0 has 2 hardware chip selects: CE0 (GPIO8) and CE1 (GPIO7)
- For more than 2 SPI devices, use additional GPIOs as software chip selects
- SPI0 MOSI (GPIO10) is also used by rpi_ws281x for WS2812B — cannot use both simultaneously

---

### spi1-1cs / spi1-2cs / spi1-3cs (SPI1 — Additional SPI Bus)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=spi1-1cs` or `dtoverlay=spi1-2cs` or `dtoverlay=spi1-3cs` |
| Purpose | Enable SPI1 bus with 1, 2, or 3 hardware chip selects |
| Pins Claimed | `spi1-1cs`: GPIO16 (CE2), GPIO19 (MISO), GPIO20 (MOSI), GPIO21 (SCLK). `spi1-2cs` adds: GPIO17 (CE1). `spi1-3cs` adds: GPIO17 (CE1), GPIO18 (CE0) |
| Parameters | `cs0_pin=N` — override CE0 GPIO. `cs1_pin=N` — override CE1 (2cs/3cs). `cs2_pin=N` — override CE2 (3cs). `cs0_spidev=disabled` — disable spidev for CE0 |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- CONFLICT: SPI1 pins overlap with PCM/I2S audio (GPIO18-21). Cannot use both
- CONFLICT: `spi1-3cs` claims GPIO18 which is also PWM0
- SPI1 is go-to when SPI0 is occupied and you need second SPI bus

---

## Group 3: UART Overlays

### miniuart-bt (Move Bluetooth to Mini-UART)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=miniuart-bt` |
| Purpose | Move onboard Bluetooth to mini-UART, freeing PL011 for GPIO14/15 |
| Pins Claimed | No additional pins. Reassigns existing UART mapping. GPIO14 (TX), GPIO15 (RX) remain serial pins |
| Parameters | None |
| Model Compat | Pi 3, Pi 4, Zero 2W. NOT needed on Pi 5 (separate UART controllers) |

**Notes:**
- On Pi 3/4/Zero2W, PL011 is assigned to Bluetooth by default. GPIO14/15 get mini-UART
- This overlay swaps: PL011 → GPIO14/15, mini-UART → Bluetooth
- Bluetooth may be slightly less reliable on mini-UART
- Must also: remove `console=serial0,115200` from cmdline.txt, add `enable_uart=1`

---

### disable-bt (Disable Bluetooth Entirely)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=disable-bt` |
| Purpose | Disable onboard Bluetooth completely, freeing PL011 for GPIO14/15 |
| Pins Claimed | No additional pins. GPIO14 (TX), GPIO15 (RX) get full PL011 UART |
| Parameters | None |
| Model Compat | Pi 3, Pi 4, Zero 2W. NOT needed on Pi 5 |

**Notes:**
- Preferred over `miniuart-bt` when Bluetooth not needed. Full PL011 with no tradeoffs
- Also run `sudo systemctl disable hciuart` to stop Bluetooth service
- Must also: remove console from cmdline.txt, add `enable_uart=1`

---

### uart2 / uart3 / uart4 / uart5 (Additional Hardware UARTs)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=uart2` or `dtoverlay=uart3` or `dtoverlay=uart4` or `dtoverlay=uart5` |
| Purpose | Enable additional hardware UART ports (Pi 4 and Pi 5 only) |
| Pins Claimed | `uart2`: GPIO0 (TX), GPIO1 (RX). `uart3`: GPIO4 (TX), GPIO5 (RX). `uart4`: GPIO8 (TX), GPIO9 (RX). `uart5`: GPIO12 (TX), GPIO13 (RX) |
| Parameters | `ctsrts` — enable hardware flow control (claims 2 additional GPIOs per UART) |
| Model Compat | Pi 4 and Pi 5 ONLY. Not available on Pi 3 or Zero 2W |

**Notes:**
- CONFLICT: `uart2` uses GPIO0/1 (I2C0/HAT EEPROM). Only use if no HAT attached
- CONFLICT: `uart3` uses GPIO4 (default 1-Wire pin)
- CONFLICT: `uart4` uses GPIO8/9 (SPI0 CE0/MISO)
- CONFLICT: `uart5` uses GPIO12/13 (PWM0/PWM1)
- With CTS/RTS: uart2 adds GPIO2/3 (conflicts I2C1!), uart3 adds GPIO6/7, uart4 adds GPIO10/11, uart5 adds GPIO14/15

---

## Group 4: PWM Overlays

### pwm (Single PWM Channel)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=pwm,pin=12,func=4` |
| Purpose | Enable a single hardware PWM output channel |
| Pins Claimed | One GPIO: the pin specified in parameters |
| Parameters | `pin=N` — GPIO pin (BCM). `func=N` — pin alternate function. Valid: PWM0: `pin=12,func=4` or `pin=18,func=2`. PWM1: `pin=13,func=4` or `pin=19,func=2` |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- RPi has 2 PWM channels: PWM0 and PWM1. Each routes to one of 2 pins
- Prefer GPIO12 (PWM0) and GPIO13 (PWM1) to avoid conflicts with SPI1/PCM
- The `func` parameter must match the pin. Wrong func = PWM won't work

---

### pwm-2chan (Dual PWM Channels)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=pwm-2chan,pin=12,func=4,pin2=13,func2=4` |
| Purpose | Enable both PWM0 and PWM1 channels simultaneously |
| Pins Claimed | Two GPIOs: `pin` and `pin2` as specified |
| Parameters | `pin=N` — PWM0 GPIO. `func=N` — PWM0 ALT function. `pin2=N` — PWM1 GPIO. `func2=N` — PWM1 ALT function. Default: `pin=18,func=2,pin2=19,func2=2`. Recommended: `pin=12,func=4,pin2=13,func2=4` |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- IMPORTANT: Default pins GPIO18/19 conflict with SPI1 and PCM. Use `pin=12,func=4,pin2=13,func2=4`
- Both channels share same clock divisor but independent duty cycles
- WS2812B on GPIO18 uses PWM0 — cannot use this overlay on GPIO18 with NeoPixels

---

## Group 5: 1-Wire Overlays

### w1-gpio (1-Wire Bus)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=w1-gpio` (default GPIO4) or `dtoverlay=w1-gpio,gpiopin=N` |
| Purpose | Enable 1-Wire bus for DS18B20 and other Dallas 1-Wire devices |
| Pins Claimed | GPIO4 (default) or GPIO specified by `gpiopin=N` |
| Parameters | `gpiopin=N` — override default GPIO4. `pullup=0|1` — enable internal pull-up (default off, external 4.7kΩ recommended) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Default GPIO4 conflicts with `uart3` on Pi 4/5. Remap with `gpiopin=N` if using uart3
- External 4.7kΩ pull-up required. Internal pull-up too weak for reliable 1-Wire
- Multiple devices share one bus — each has unique 64-bit ROM ID
- For DHT22: do NOT use this overlay. DHT22 is proprietary protocol, not Dallas 1-Wire
- Devices appear under `/sys/bus/w1/devices/`

---

### w1-gpio-pullup (1-Wire with Strong Pull-up for Parasitic Power)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=w1-gpio-pullup,gpiopin=N,extpullup=N` |
| Purpose | Enable 1-Wire with strong pull-up for parasitic power mode |
| Pins Claimed | GPIO for data (`gpiopin`), optionally second GPIO for external pull-up transistor |
| Parameters | `gpiopin=N` — data GPIO (default 4). `extpullup=N` — GPIO for external pull-up transistor. `pullup=0|1` — internal pull-up |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Parasitic power: DS18B20 draws power from data line (2 wires only). Needs strong pull-up during conversion
- `extpullup` drives GPIO high during conversion to switch external MOSFET
- Use only for parasitic mode. Standard 3-wire mode uses regular `w1-gpio`
- Parasitic unreliable over >3m or many sensors. Prefer powered mode

---

## Group 6: CAN Bus Overlays

### mcp2515-can0 (CAN Bus via MCP2515 — Primary)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=mcp2515-can0,oscillator=8000000,interrupt=25` |
| Purpose | Enable CAN bus via MCP2515 SPI-to-CAN controller on SPI0 CE0 |
| Pins Claimed | SPI0: GPIO8 (CE0), GPIO9 (MISO), GPIO10 (MOSI), GPIO11 (SCLK). Plus interrupt GPIO |
| Parameters | `oscillator=N` — crystal frequency in Hz. MUST match physical crystal (8000000 or 16000000 common). `interrupt=N` — GPIO for INT pin. `speed=N` — SPI speed (default 10000000) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- CRITICAL: `oscillator` MUST match board crystal. Check marking ("8.000" = 8MHz, "16.000" = 16MHz). Wrong value = garbage data
- Uses SPI0 CE0. SPI0 must be enabled (`dtparam=spi=on`)
- Creates `can0` interface. After boot: `sudo ip link set can0 up type can bitrate 500000`
- Requires 120Ω termination at both bus ends. Some modules include it
- Requires external CAN transceiver (TJA1050, MCP2551, SN65HVD230)

---

### mcp2515-can1 (CAN Bus via MCP2515 — Secondary)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=mcp2515-can1,oscillator=16000000,interrupt=24` |
| Purpose | Enable second CAN bus via MCP2515 on SPI0 CE1 |
| Pins Claimed | SPI0: GPIO7 (CE1), GPIO9 (MISO), GPIO10 (MOSI), GPIO11 (SCLK). Plus interrupt GPIO |
| Parameters | Same as mcp2515-can0: `oscillator=N`, `interrupt=N`, `speed=N` |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Uses SPI0 CE1 (GPIO7). Both can0 and can1 share SPI0 bus but separate chip selects
- Use different interrupt GPIO than can0 (e.g., can0 on GPIO25, can1 on GPIO24)
- Both boards can have different crystal frequencies — set `oscillator` independently

---

## Group 7: Peripheral Overlays

### sc16is750-i2c / sc16is752-i2c (I2C-to-UART Bridge)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=sc16is750-i2c,int_pin=24,addr=0x48` or `dtoverlay=sc16is752-i2c,int_pin=24,addr=0x48` |
| Purpose | Add 1 (SC16IS750) or 2 (SC16IS752) additional UARTs via I2C |
| Pins Claimed | I2C bus (GPIO2/3) plus interrupt GPIO. UART TX/RX are on the SC16IS7xx chip itself |
| Parameters | `int_pin=N` — GPIO for interrupt. `addr=0xNN` — I2C address. `xtal=N` — crystal frequency (default 14745600) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Alternative to hardware UARTs. Useful on Pi 3/Zero 2W (which lack uart2-5)
- SC16IS752 gives 2 UARTs from one I2C address
- I2C bandwidth limits throughput — fine for 9600-115200 baud
- TX/RX pins are on the SC16IS7xx breakout, not RPi GPIO header

---

### enc28j60 (SPI Ethernet Controller)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=enc28j60,int_pin=25,speed=12000000` |
| Purpose | Enable ENC28J60 SPI Ethernet, adding 10Mbps wired Ethernet |
| Pins Claimed | SPI0: GPIO8 (CE0), GPIO9 (MISO), GPIO10 (MOSI), GPIO11 (SCLK). Plus interrupt GPIO |
| Parameters | `int_pin=N` — GPIO for interrupt. `speed=N` — SPI clock in Hz (default 12000000) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- 10 Mbps only (10Base-T). Not for high-bandwidth applications
- Uses SPI0 CE0. If also using SPI display on CE0, one must move to CE1
- Creates `eth1` (or similar) network interface
- Consider W5500 for 100Mbps (requires manual driver setup)

---

### gpio-fan (GPIO-Controlled Fan)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=gpio-fan,gpiopin=14,temp=55000` |
| Purpose | Enable automatic fan control based on CPU temperature |
| Pins Claimed | GPIO specified by `gpiopin` |
| Parameters | `gpiopin=N` — GPIO for fan transistor (default 12). `temp=N` — threshold in millidegrees C (55000 = 55°C) |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Simple on/off control (not PWM). Fan on above threshold, off below
- GPIO drives transistor/MOSFET — do NOT connect fan directly to GPIO
- For PWM fan control, use PWM overlay and custom software
- Pi 5 official case fan uses different mechanism (fan header, not GPIO)

---

### gpio-ir / gpio-ir-tx (Infrared Receiver/Transmitter)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=gpio-ir,gpio_pin=17` (receiver) or `dtoverlay=gpio-ir-tx,gpio_pin=18` (transmitter) |
| Purpose | Enable IR receiver (LIRC input) or transmitter (LIRC output) |
| Pins Claimed | GPIO specified by `gpio_pin` |
| Parameters | `gpio_pin=N` — GPIO for IR sensor/LED. `invert=0|1` — invert polarity. `rc-map-name=name` — keymap for receiver |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Receiver: works with TSOP38238 or similar 38kHz IR demodulators
- Transmitter: drives IR LED via transistor to send remote codes
- Uses kernel ir-keytable/rc-core, not older LIRC daemon (though LIRC still usable)
- Pin assignments in examples are conventions — any GPIO works

---

### pps-gpio (Pulse-Per-Second for GPS Timing)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=pps-gpio,gpiopin=18` |
| Purpose | Configure GPIO as PPS input for precision time synchronization |
| Pins Claimed | GPIO specified by `gpiopin` |
| Parameters | `gpiopin=N` — GPIO for PPS signal (default 18). `assert_falling_edge=0|1` — trigger on falling edge. `capture_clear=0|1` — enable clear-on-read |
| Model Compat | All (Pi 3, Pi 4, Pi 5, Zero 2W) |

**Notes:**
- Used with GPS modules (NEO-6M, NEO-M8N) for NTP servers or precision timing
- GPS PPS provides pulse exactly on UTC second boundary
- Pair with chrony or ntpd PPS refclock for sub-microsecond accuracy
- Default GPIO18 conflicts with PWM0 and SPI1 CE0. Use `gpiopin=N` for free GPIO

---

### sdio (Additional SD Card Interface)

| Field | Value |
|-------|-------|
| config.txt line | `dtoverlay=sdio,bus_width=4,gpios_22_25` or `dtoverlay=sdio,bus_width=1,gpios_34_37` |
| Purpose | Enable additional SDIO/SD card interface on GPIO pins |
| Pins Claimed | `gpios_22_25`: GPIO22 (CLK), GPIO23 (CMD), GPIO24-27 (DAT0-3). `gpios_34_37`: GPIO34-37 |
| Parameters | `bus_width=1|4` — 1-bit or 4-bit SD mode. `gpios_22_25` or `gpios_34_37` — pin group. `poll_once=0|1` — disable hotplug polling |
| Model Compat | Pi 3, Pi 4 (GPIO group dependent). Mainly useful on Compute Modules |

**Notes:**
- Rarely used on standard boards — claims 6 GPIOs for 4-bit mode
- Primary use: Compute Module projects needing second SD slot, or SDIO WiFi/BT
- `gpios_22_25` conflicts with many common GPIO uses — check before enabling

---

## Overlay Conflict Matrix

| Overlay A | Overlay B | Conflict | Resolution |
|-----------|-----------|----------|------------|
| spi1-3cs | pwm (GPIO18) | Both claim GPIO18 | Use pwm on GPIO12: `pin=12,func=4` |
| spi1-1cs/2cs/3cs | PCM/I2S audio | SPI1 pins overlap PCM (GPIO18-21) | Choose SPI1 or I2S, not both |
| uart2 | I2C0 (HAT EEPROM) | Both use GPIO0/1 | Only use uart2 if no HAT attached |
| uart3 | w1-gpio (default) | Both use GPIO4 | Remap 1-Wire: `dtoverlay=w1-gpio,gpiopin=N` |
| uart4 | SPI0 | uart4 uses GPIO8/9 (SPI0 CE0/MISO) | Use different UART or move SPI device |
| uart5 | pwm-2chan (GPIO12/13) | Both claim GPIO12/13 | Choose UART5 or PWM, not both |
| miniuart-bt | disable-bt | Both modify BT UART assignment | Use one or the other, never both |
| mcp2515-can0 | enc28j60 | Both use SPI0 CE0 | Move one to CE1 (mcp2515-can1 or software CS) |
| pwm (GPIO18) | WS2812B (GPIO18) | Both use PWM0 on GPIO18 | Use WS2812B on GPIO10 (SPI) or move PWM to GPIO12 |
| pps-gpio (default) | pwm/SPI1 | Default GPIO18 conflicts | Set `gpiopin=N` to free GPIO |
| gpio-fan (default) | pwm (GPIO12) | Both claim GPIO12 | Change gpio-fan to non-PWM GPIO |
| sdio (gpios_22_25) | SPI1, general GPIO | Claims GPIO22-27 | Use only on Compute Module |

When planning pin assignments, load the conflict matrix first to identify which overlays can coexist. The gpio-config agent checks these conflicts automatically during pin validation.
