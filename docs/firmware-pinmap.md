# Firmware Pin Map

## Scope

This document finishes the firmware-facing pin designation using the uploaded netlists and schematic PDFs currently present in the repository:

- [MainPCB.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.NET)
- [MainPCB.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.pdf)
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
4. The current repository still does not include the Sensor & LED board netlist or schematic, so the ToF sensor, two-stage trigger, and any other sensor-board-only wiring remain unresolved at source level.
5. The board as drawn assumes a compatible ESP32-S3 module variant that exposes `GPIO35/36/37` and supports `GPIO47/48` at 3.3 V logic.

## Confirmed ESP32 GPIO Map

### Safety and power control

| GPIO | Module pin | Net | Direction | Terminates at | Firmware role |
| --- | --- | --- | --- | --- | --- |
| `GPIO15` | `8` | `PWR_TEC_EN` | output | `U2 MPM3530GRF EN` | TEC rail enable |
| `GPIO16` | `9` | `PWR_TEC_GOOD` | input | `U2 MPM3530GRF PG` | TEC rail power-good |
| `GPIO17` | `10` | `PWR_LD_EN` | output | `U5 MPM3530GRF EN`, also `LMV331` shutdown path net | LD rail enable |
| `GPIO18` | `11` | `PWR_LD_PGOOD` | input | `U5 MPM3530GRF PG` | LD rail power-good |
| `GPIO13` | `21` | `LD_SBDN` | output | `U3 ATLS6A214 SBDN` | fast beam-off / standby control |
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

This connector is present on the main board, but the mating board netlist is not in the repository:

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

- the missing Sensor & LED board likely contains the unresolved ToF, button, or auxiliary LED circuitry
- until that board is reviewed, `GPIO4/5/6/7` must remain board-configurable and safety-reviewed before hard binding new functions

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

## Remaining Unknowns

The following are still not finished because the corresponding board files are not in the repository as of March 31, 2026:

- ToF sensor device identity and pin-level wiring
- two-stage trigger button pin-level wiring
- any LED-board-only or sensor-board-only interrupts
- whether the Sensor & LED board adds pull-ups or other loading to `GPIO4/5/6/7`

Until those files exist, any firmware binding of ToF or trigger signals would still be a hardware assumption rather than a netlist-backed fact.
