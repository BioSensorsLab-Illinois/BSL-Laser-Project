# Firmware Pin Map

## Scope

This document finishes the firmware-facing pin designation using the uploaded netlists and schematic PDFs currently present in the repository:

- [MainPCB.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.NET)
- [MainPCB.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.pdf)
- [ToF-LED-Board.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/ToF-LED-Board.NET)
- [ToF-LED-Board.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/ToF-LED-Board.pdf)
- [BMS-Board.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/BMS-Board.NET)
- [BMS.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/BMS.pdf)
- [USB_PD-PHY.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/USB_PD-PHY.NET)
- [USB-PD.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/USB-PD.pdf)

ESP32-S3 module pin numbers were cross-referenced against the official Espressif ESP32-S3-WROOM-1 / WROOM-1U datasheet Table 3-1:

- [Espressif ESP32-S3-WROOM-1 / WROOM-1U Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)

## Netlist-Backed Conclusions

1. `GPIO4/GPIO5` are not just a local bus. They span:
   - local DAC80502
   - local DRV2605
   - STUSB4500 on the USB-PD board through the BMS board
   - the external Sensor & LED board connector on the main PCB
2. `GPIO6/GPIO7` are also shared off-board. They leave the main PCB to:
   - the Sensor & LED board connector
   - the BMS battery-toggle connector
3. `GPIO37` is electrically shared between the DRV2605 `IN/TRIG` input and the visible laser switch enable path.
4. The ToF daughterboard is now source-backed and uses `VL53L1X` over the shared `GPIO4/GPIO5` I2C bus, with optional sideband on `GPIO7` and `GPIO6`.
5. The button board (MCP23017 + TLC59116) is now source-backed (2026-04-15); see the "Button board" section below.
6. The board as drawn assumes a compatible ESP32-S3 module variant that exposes `GPIO35/36/37` and supports `GPIO47/48` at 3.3 V logic.

## Confirmed ESP32 GPIO Map

### Safety and power control

| GPIO | Module pin | Net | Direction | Terminates at | Firmware role |
| --- | --- | --- | --- | --- | --- |
| `GPIO15` | `8` | `PWR_TEC_EN` | output | `U2 MPM3530GRF EN` | TEC rail enable |
| `GPIO16` | `9` | `PWR_TEC_GOOD` | input | `U2 MPM3530GRF PG` | TEC rail power-good |
| `GPIO17` | `10` | `PWR_LD_EN` | output | `U5 MPM3530GRF EN`, also `LMV331` shutdown path net | LD rail enable |
| `GPIO18` | `11` | `PWR_LD_PGOOD` | input | `U5 MPM3530GRF PG` | LD rail power-good |
| `GPIO13` | `21` | `LD_SBDN` | tri-state (output or input/Hi-Z) | `U3 ATLS6A214 SBDN` | three-state control: drive LOW = shutdown (20 us fast beam-off), drive HIGH = operate, input/Hi-Z = standby (external R27=13k7 / R28=29k4 divider holds 2.25 V in datasheet 2.1-2.4 V standby band). Pulls MUST stay disabled — internal pulls would fight the divider. Fault paths force drive-LOW, never Hi-Z. |
| `GPIO21` | `23` | `LD-PCN` | output | `U3 ATLS6A214 PCN` | low/high current selection |
| `GPIO14` | `22` | `LD_LPGD` | input | `U3 ATLS6A214 LPGD` | driver loop-good input |
| `GPIO47` | `24` | `TEC_TEMPGD` | input | `U4 TEC14M5V3R5AS TEMPGD` | TEC settle-good input |
| `GPIO48` | `25` | `ERM_EN` | output | `U8 DRV2605 EN` | haptic driver enable |
| `GPIO37` | `30` | `ERM_TRIG` and `GN_LD_EN` | output | `U8 DRV2605 IN/TRIG`, `U9 TPS22918 ON` | shared haptic trigger and visible alignment enable; hazardous shared net |

### Analog supervision

| GPIO | Module pin | Net | ESP32 ADC function | Terminates at | Firmware role |
| --- | --- | --- | --- | --- | --- |
| `GPIO1` | `39` | `LD-TMO` | `ADC1_CH0` | `U3 ATLS6A214 TMO` | laser driver temperature monitor |
| `GPIO2` | `38` | `LD-LIO` | `ADC1_CH1` | `U3 ATLS6A214 LIO` | laser current monitor |
| `GPIO8` | `12` | `TEC_TMO` | `ADC1_CH7` | `U4 TEC14M5V3R5AS TMO`, `LMV331` comparator input | TEC temperature proxy |
| `GPIO9` | `17` | `TEC_ITEC` | `ADC1_CH8` | `U4 TEC14M5V3R5AS ITEC` | TEC current telemetry |
| `GPIO10` | `18` | `TEC_VTEC` | `ADC1_CH9` | `U4 TEC14M5V3R5AS VTEC` | TEC voltage telemetry |

### IMU interface

| GPIO | Module pin | Net | Direction | Terminates at | Firmware role |
| --- | --- | --- | --- | --- | --- |
| `GPIO38` | `31` | `IMU_SDI` / `IMU_SDA-SDI` | output | `U1 LSM6DSOTR SDI` | SPI MOSI |
| `GPIO39` | `32` | `IMU_CS` | output | `U1 LSM6DSOTR CS` | SPI chip select |
| `GPIO40` | `33` | `IMU_SCLK` / `IMU_SCL-SCLK` | output | `U1 LSM6DSOTR SCLK` | SPI clock |
| `GPIO41` | `34` | `IMU_SDO` | input | `U1 LSM6DSOTR SDO` | SPI MISO |
| `GPIO42` | `35` | `IMU_INT2` | input | `U1 LSM6DSOTR INT2` | data-ready / interrupt assist |

### Shared and exported buses

| GPIO | Module pin | Net | Default stuffing | Connected load | Firmware role |
| --- | --- | --- | --- | --- | --- |
| `GPIO4` | `4` | `GPIO4` | populated | `DAC_SDA` via `R65`, `ERM_SDA` via `R61`, BMS connector, USB-PD connector as `ST_SDA`, Sensor & LED board connector | primary shared I2C SDA |
| `GPIO5` | `5` | `GPIO5` | populated | `DAC_SCL` via `R66`, `ERM_SCL` via `R62`, BMS connector, USB-PD connector as `ST_SCL`, Sensor & LED board connector | primary shared I2C SCL |
| `GPIO11` | `19` | `GPIO11` | `DNP` route | alternate `DAC_SDA` via `R67` | optional dedicated DAC I2C SDA |
| `GPIO12` | `20` | `GPIO12` | `DNP` route | alternate `DAC_SCL` via `R68` | optional dedicated DAC I2C SCL |
| `GPIO35` | `28` | `GPIO35` | `DNP` route | alternate `ERM_SDA` via `R63` | optional dedicated ERM I2C SDA |
| `GPIO36` | `29` | `GPIO36` | `DNP` route | alternate `ERM_SCL` via `R64` | optional dedicated ERM I2C SCL |
| `GPIO6` | `6` | `GPIO6` | direct | BMS battery-toggle connector, Sensor & LED board connector | external shared GPIO |
| `GPIO7` | `7` | `GPIO7` | direct | BMS battery-toggle connector, Sensor & LED board connector | external shared GPIO |

### USB, UART, boot, and reserved

| GPIO / signal | Module pin | Net | Terminates at | Firmware note |
| --- | --- | --- | --- | --- |
| `GPIO19` | `13` | `ESP_D_N` | BMS board, USB-PD board, USB-C connector | native USB `D-` |
| `GPIO20` | `14` | `ESP_D_P` | BMS board, USB-PD board, USB-C connector | native USB `D+` |
| `GPIO44` / `RXD0` | `36` | `ESP_RX` | test point `TP15` / debug header path | UART0 RX |
| `GPIO43` / `TXD0` | `37` | `ESP_TX` | test point `TP14` / debug header path | UART0 TX |
| `GPIO0` | `27` | `ESP_GPIO0` | boot button `SW2`, test point `TP16` | strapping pin; do not use for safety I/O |
| `GPIO46` | `16` | `ESP_GPIO46` | optional pull-up `R21 (DNP)` | strapping pin; boot option |
| `GPIO3` | `15` | `GPIO_3` | test point `TP2` only | keep open per schematic note |
| `GPIO45` | `26` | `GPIO_45` | test point `TP1` only | keep open per schematic note |

## Cross-Board Connector Topology

### MainPCB J1 to BMS J1

The main board `J1` and BMS board `J1` match pin-for-pin:

| Pin | Net on MainPCB | Net on BMS | Meaning |
| --- | --- | --- | --- |
| `1` | `GPIO4` | `GPIO4` | shared I2C SDA path |
| `2` | `ESP_D_P` | `D_P` | native USB `D+` path |
| `3` | `GPIO5` | `GPIO5` | shared I2C SCL path |
| `4` | `ESP_D_N` | `D_N` | native USB `D-` path |
| `5` | `GPIO7` | `GPIO7` | external shared GPIO |
| `6` | `GPIO6` | `GPIO6` | external shared GPIO |
| `7` | `GND` | `GND` | return |
| `8` | `VIN` | `VSYS` | switched system power |
| `9` | `GND` | `GND` | return |
| `10` | `VIN` | `VSYS` | switched system power |

### BMS J3 to USB-PD J2

The BMS board bridges the USB-PD board onto the same USB and I2C nets:

| Pin | BMS J3 net | USB-PD J2 net | Meaning |
| --- | --- | --- | --- |
| `1` | `GND` | `GND` | return |
| `2` | `VBUS` | `VSINK` | PD sink power into BMS |
| `3` | `GND` | `GND` | return |
| `4` | `VBUS` | `VSINK` | PD sink power into BMS |
| `5` | `GND` | `GND` | return |
| `6` | `VBUS` | `VSINK` | PD sink power into BMS |
| `7` | `D_P` | `D_P` | USB `D+` |
| `8` | `GPIO4` / `ST_SDA` | `ST_SDA` | STUSB4500 I2C SDA |
| `9` | `D_N` | `D_N` | USB `D-` |
| `10` | `GPIO5` / `ST_SCL` | `ST_SCL` | STUSB4500 I2C SCL |

### MainPCB J2 Sensor & LED board

This connector is present on the main board and now cross-validates against the uploaded ToF daughterboard:

| Pin | Net | Meaning |
| --- | --- | --- |
| `1` | `GPIO4` | shared I2C SDA / external GPIO |
| `2` | `GPIO4` | duplicated net on second row |
| `3` | `GPIO5` | shared I2C SCL / external GPIO |
| `4` | `GPIO5` | duplicated net on second row |
| `5` | `GPIO7` | external shared GPIO |
| `6` | `GPIO6` | external shared GPIO |
| `7` | `DVDD_3V3` | supply |
| `8` | `DVDD_3V3` | supply |
| `9` | `GND` | return |
| `10` | `GND` | return |

Firmware implication:

- confirmed ToF board wiring is:
  - `GPIO4` -> `VL53L1X SDA`
  - `GPIO5` -> `VL53L1X SCL`
  - `GPIO7` -> recommended interrupt input from `VL53L1X GPIO1` (`LD_GPIO` on the daughterboard)
  - `GPIO6` -> recommended output to the onboard LED-driver `CTRL` input (`LD_INT` on the daughterboard)
- `VL53L1X XSHUT` is pulled up locally on the daughterboard and is not exported, so no dedicated MCU shutdown line exists on this revision
- the ToF board is I2C only; do not route it onto the IMU SPI bus
- `GPIO4/5/6/7` must remain board-configurable; GPIO7 is now `LASER_CONTROLLER_GPIO_BUTTON_INTA` (button-board ISR) — see the "Button board" section below

### BMS J2 battery-toggle connector

The BMS board also exports:

| Pin | Net | Meaning |
| --- | --- | --- |
| `1` | `GPIO7` | external control / battery-toggle line |
| `2` | `GPIO6` | external control / battery-toggle line |
| `3` | `VBAT` | battery |
| `4` | `GND` | return |
| `5` | `VBAT` | battery |
| `6` | `GND` | return |
| `7` | `VBAT` | battery |
| `8` | `GND` | return |

## Stuffing and Variant Constraints

### Current stuffing assumptions

- `R61`, `R62`, `R65`, and `R66` are populated, so DAC and DRV2605 share `GPIO4/GPIO5` with the STUSB4500.
- `R63`, `R64`, `R67`, and `R68` are `DNP`, so the alternate dedicated I2C routes are inactive.
- the default pull-up network on the shared I2C bus is effectively three `4.7 kOhm` pull-ups in parallel on each line before any Sensor & LED board loading is added
- `R52` is populated and `R53` is `DNP`, so the visible laser switch is fed from `VDS_TEC_5V0`.
- `R42` is populated and `R41` is `DNP`, so the DAC powers up at zero scale.

### Module compatibility requirements

The schematic symbol is generic `ESP32-S3-WROOM-1U`, but the actual fitted subvariant matters:

- if the production module uses Octal-SPI PSRAM variants that reserve `GPIO35/36/37`, this board-level routing is incompatible
- if the production module uses a variant with `GPIO47/48` at `1.8 V`, the current `3.3 V` logic wiring is incompatible

Production firmware should therefore lock the allowed module BOM, not just the symbol family.

## Button board (MCP23017 + TLC59116) — 2026-04-15

The button board is now source-backed and shares the **MainPCB J2 Sensor & LED** physical connector with the ToF daughterboard.

Two I²C devices fitted, both with all address pins strapped to GND:

| Device | Address | Role |
| --- | --- | --- |
| MCP23017 | `0x20` | GPIO expander; 4 active button inputs on GPA0..GPA3, INTA → ESP32 GPIO7 |
| TLC59116 | `0x60` | LED driver; RGB status LED on OUT0/OUT1/OUT2 (B/R/G channel order — non-standard) |

MCP23017 button pin assignments:

| MCP23017 pin | Function | Notes |
| --- | --- | --- |
| `GPA0` | Main trigger stage 1 (shallow press) | Active-low to GND, idle-high via 3V3 + internal pull-up |
| `GPA1` | Main trigger stage 2 (deep press) | Active-low; stage2 mechanically implies stage1 |
| `GPA2` | Side button 1 (LED brightness +10%) | Active-low |
| `GPA3` | Side button 2 (LED brightness -10%) | Active-low |
| `GPA4..GPA7`, `GPB0..GPB7` | Reserved / unused | Configured as input with internal pull-up |

Firmware configures the MCP23017 via:

- `IOCON = 0x64` (BANK=0, MIRROR=1, SEQOP=1, ODR=1 → INTA open-drain, mirrors INTB)
- `IODIRA = 0xFF` (all inputs)
- `IPOLA = 0x00` (no hardware inversion; firmware inverts to "pressed")
- `GPPUA = 0x0F` (internal pull-up on the four button pins)
- `GPINTENA = 0x0F` (interrupt-on-change on all four)
- `INTCONA = 0x00` (any-edge — compare against previous, not DEFVAL)
- `DEFVALA = 0xFF` (idle-high reference, deterministic init)

TLC59116 is configured via:

- `MODE1 = 0x01` (SLEEP=0, ALLCALL enabled at default 0x68 — harmless because no other device on the bus uses 0x68)
- `MODE2.DMBLNK` flips between dim/blink mode at runtime (`0` = solid, `1` = group blink at fixed 1 Hz / 50 % duty)
- `LEDOUT0 = 0b00111111` (mode 3 on OUT0/1/2: individual PWM × group dim/blink; OUT3 off). LEDOUT1..3 = 0x00.
- `GRPFREQ = 23` → period = `(GRPFREQ+1)/24 = 1.0 s` per the datasheet formula
- `GRPPWM = 128` → 50 % duty
- `PWM0 = B`, `PWM1 = R`, `PWM2 = G` (0..255)

### GPIO7 ownership transfer (2026-04-15)

`GPIO7` was previously `LASER_CONTROLLER_GPIO_TOF_GPIO1_INT` (VL53L1X data-ready). It is now `LASER_CONTROLLER_GPIO_BUTTON_INTA` exclusively. The pinmap macro was renamed in `components/laser_controller/include/laser_controller_pinmap.h`. The ToF daughterboard physically shares the same J2 connector, so its GPIO1 output net is no longer wired into the ESP-side input on this revision; the ToF runs in polling-only mode (RANGE_STATUS register).

`GPIO7` now requires an internal pull-up on the ESP side because MCP23017 INTA is open-drain. The firmware ISR is registered in `laser_controller_board.c` (`laser_controller_board_button_inta_isr`); it does NOT perform any I²C work — it only increments an atomic counter (`laser_controller_buttons_on_isr_fired`) for control-task drainage and telemetry.

### Shared-bus loading after button-board addition

GPIO4/5 now carries six addresses + ALLCALL:

| 7-bit | Device |
| --- | --- |
| `0x20` | MCP23017 (button board) |
| `0x28` | STUSB4500 (USB-PD) |
| `0x29` | VL53L1X (ToF) |
| `0x48` | DAC80502 |
| `0x5A` | DRV2605 |
| `0x60` | TLC59116 (button board) |
| `0x68` | TLC59116 ALLCALL (passive — no other device claims this slot) |

No collisions. Bus pull-up network is unchanged — the existing 3 × 4.7 kΩ parallel pulls remain adequate at the standard 100 kHz I²C clock; if a future revision adds a 7th address or moves to 400 kHz, re-verify with a scope.

## Remaining Unknowns

The following are still not finished:

- the total added I2C pull-up/loading once every external daughterboard is populated (verify scope at 400 kHz if the bus speed is bumped)
- the exact mechanical interlock model for the J2 connector when both ToF and button board are present (firmware assumes ToF GPIO1 is physically not driving the line; hardware team must verify)

Both the ToF board and the button board are now source-backed in this repo. The two-stage trigger button pin-level wiring is no longer unknown — see the Button board section above.
