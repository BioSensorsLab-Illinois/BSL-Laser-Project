# Hardware Recon

## Scope

This document summarizes the hardware information recovered from the local schematic and datasheet set currently in the repository.

Reviewed files:

- [MainPCB.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.NET)
- [MainPCB.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/MainPCB.pdf)
- [ToF-LED-Board.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/ToF-LED-Board.NET)
- [ToF-LED-Board.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/ToF-LED-Board.pdf)
- [USB_PD-PHY.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/USB_PD-PHY.NET)
- [USB-PD.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/USB-PD.pdf)
- [BMS-Board.NET](/Users/zz4/BSL/BSL-Laser/docs/Schematics/BMS-Board.NET)
- [BMS.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/BMS.pdf)
- [ATLS6A214D-3.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/ATLS6A214D-3.pdf)
- [Micro_TEC_Controller_TEC14M5V3R5AS-2.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/Micro_TEC_Controller_TEC14M5V3R5AS-2.pdf)
- [dac80502-2.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/dac80502-2.pdf)
- [lsm6dso.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/lsm6dso.pdf)
- [drv2605.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/drv2605.pdf)
- [stusb4500.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/stusb4500.pdf)
- [vl53l1x.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/vl53l1x.pdf)
- [MPM3530GRF.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/MPM3530GRF.pdf)

This is intentionally a firmware-facing summary, not a substitute for the source schematics.

## Executive Findings

1. The ESP32-S3 I/O map is mostly recoverable and is now explicit below.
2. The default board stuffing shares `GPIO4/GPIO5` across `STUSB4500`, `DAC80502`, `DRV2605`, and the external Sensor & LED board connector.
3. The IMU is wired for direct SPI to the ESP32 by default, with optional I2C stuffing called out but not populated.
4. The visible green laser enable net `GN_LD_EN` is tied to the same ESP32 net as `ERM_TRIG` on `IO37`.
5. `GPIO6/GPIO7` are not local-only nets; they are shared between the main-board Sensor & LED connector and the BMS battery-toggle connector.
6. The ToF and LED daughterboard is now identified as a `VL53L1X` I2C ranger plus a separate LED boost-driver path.
7. The STUSB4500 interrupt/status pins are not wired to the MCU; firmware should assume PD supervision is by I2C polling only.
8. The DAC powers up to zero scale with the shown stuffing, which is the safe default we want.

## Netlist Cross-Validation Summary

The new `.NET` files confirm the following board-to-board paths:

- MainPCB `J1` mates to BMS `J1` and carries `GPIO4`, `GPIO5`, `GPIO6`, `GPIO7`, `D+`, `D-`, `GND`, and switched system power.
- BMS `J3` mates to USB-PD `J2` and carries `VBUS/VSINK`, `D+`, `D-`, and the `STUSB4500` I2C lines back to `GPIO4/GPIO5`.
- MainPCB `J2` is explicitly labeled `B2B - Sensor & LED Board` and exports `GPIO4`, `GPIO5`, `GPIO6`, `GPIO7`, `DVDD_3V3`, and `GND`.
- The uploaded ToF daughterboard uses `VL53L1CXV0FY/1` on `LD_SDA` / `LD_SCL`, routes the sensor interrupt output to `LD_GPIO`, and uses a separate `LD_INT` net to drive the onboard LED boost converter control input.
- BMS `J2` is labeled `Battery Toggle Pins` and exports `GPIO7` and `GPIO6` again, plus `VBAT` and `GND`.

Firmware implication:

- `GPIO4/GPIO5` and `GPIO6/GPIO7` must be treated as cross-board shared nets, not private MCU pins.
- the ToF board itself is now source-backed, but the separate button board is still unresolved.
- the firmware-facing final GPIO table is now maintained in [firmware-pinmap.md](/Users/zz4/BSL/BSL-Laser/docs/firmware-pinmap.md).

## ESP32-S3 Pin Map

This table reflects the actual schematic wiring recovered from the ESP32 page and cross-checked against the destination sheets.

| ESP32 pin | Net | Function | Confidence |
| --- | --- | --- | --- |
| `RXD0` | `ESP_RX` | UART debug RX | confirmed |
| `TXD0` | `ESP_TX` | UART debug TX | confirmed |
| `IO20` | `ESP_D_P` | native USB `D+` | confirmed |
| `IO19` | `ESP_D_N` | native USB `D-` | confirmed |
| `IO15` | `PWR_TEC_EN` | TEC 5 V regulator enable | confirmed |
| `IO16` | `PWR_TEC_GOOD` | TEC rail power-good input | confirmed |
| `IO17` | `PWR_LD_EN` | LD 8 V regulator enable | confirmed |
| `IO18` | `PWR_LD_PGOOD` | LD rail power-good input | confirmed |
| `IO21` | `LD_PCN` | ATLS6A214 pulse/current-select control | confirmed |
| `IO37` | `ERM_TRIG` and `GN_LD_EN` | haptic trigger and visible-laser enable share one net | confirmed, needs review |
| `IO38` | `IMU_SDA-SDI` | IMU SPI MOSI / SDI | confirmed |
| `IO39` | `IMU_CS` | IMU chip select | confirmed |
| `IO40` | `IMU_SCL-SCLK` | IMU SPI clock | confirmed |
| `IO41` | `IMU_SDO` | IMU SPI MISO / SDO | confirmed |
| `IO42` | `IMU_INT2` | IMU interrupt 2 | confirmed |
| `IO47` | `TEC_TEMPGD` | TEC temperature-good digital input | confirmed |
| `IO48` | `ERM_EN` | DRV2605 enable | confirmed |
| `IO1` | `LD-TMO` | laser driver temperature monitor analog input | confirmed |
| `IO2` | `LD-LIO` | laser current monitor analog input | confirmed |
| `IO4` | `ESP_GPIO4` | default shared I2C SDA rail and external board routing | confirmed |
| `IO5` | `ESP_GPIO5` | default shared I2C SCL rail and external board routing | confirmed |
| `IO6` | `ESP_GPIO6` | exported to external boards / sensor board | confirmed |
| `IO7` | `ESP_GPIO7` | exported to external boards / sensor board | confirmed |
| `IO8` | `TEC_TMO` | TEC temperature monitor analog input | confirmed |
| `IO9` | `TEC_ITEC` | TEC current telemetry analog input | confirmed |
| `IO10` | `TEC_VTEC` | TEC voltage telemetry analog input | confirmed |
| `IO11` | `GPIO11` | optional alternate DAC I2C SDA via stuffing | confirmed |
| `IO12` | `GPIO12` | optional alternate DAC I2C SCL via stuffing | confirmed |
| `IO13` | `LD_SBDN` | ATLS6A214 shutdown / standby / operate control | confirmed |
| `IO14` | `LD-LPGD` | ATLS6A214 loop-good digital input | confirmed |
| `IO35` | `GPIO35` | optional alternate ERM I2C SDA via stuffing | confirmed |
| `IO36` | `GPIO36` | optional alternate ERM I2C SCL via stuffing | confirmed |
| `IO0` | `ESP_GPIO0` | boot button | confirmed |
| `IO46` | `ESP_GPIO46` | boot option strap | confirmed |
| `IO3` | `GPIO_3` | kept open for internal functions per schematic note | confirmed |
| `IO45` | `GPIO_45` | kept open for internal functions per schematic note | confirmed |

## Safety-Relevant Electrical Topology

### Always-On Digital Rail

- `DVDD_3V3` is generated on the main board by `U6`, shown as `MPM3612`.
- Its `EN` is tied high for always-on behavior so the MCU remains alive for monitoring and shutdown decisions.
- The `MPM3612` datasheet is not included in the current repository, so firmware-facing electrical details for that rail are still incomplete.

### High-Power Rails

#### LD rail

- `VDS_LD_8V0` is generated by `U5`, an `MPM3530GRF`.
- `PWR_LD_EN` from `IO17` drives its `EN`.
- `PWR_LD_PGOOD` from the regulator `PG` output returns to `IO18`.
- The schematic note explicitly says the rail is default OFF and the ESP32 may enable it only after conditions are met.

#### TEC rail

- `VDS_TEC_5V0` is generated by `U2`, also an `MPM3530GRF`.
- `PWR_TEC_EN` from `IO15` drives its `EN`.
- `PWR_TEC_GOOD` from the regulator `PG` output returns to `IO16`.
- The schematic note again makes this rail default OFF.

### Analog Hardware Overtemperature Path

- The LD 8 V rail sheet includes an `LMV331` comparator producing `LD_OT_SHDN`.
- Its input is sourced from `TEC_TMO`, not `LD-TMO`.
- Threshold components `R59` and `R60` are marked `DNP`, so the exact hardware overtemperature trip behavior must be verified from the assembled BOM, not only from the schematic.

### Visible Green Alignment Laser

- The visible laser switch uses `TPS22918DBVR`.
- It is controlled by `GN_LD_EN`.
- The power source is selectable:
  - `R52 = 0R` routes `VDS_TEC_5V0` to the visible laser switch input.
  - `R53 = DNP` is the alternate `DVDD_3V3` option.
- The schematic currently shows the 5 V path populated and the 3.3 V path unpopulated.

## Shared-Net Warning: `IO37`

The ESP32 sheet shows `IO37` driving a net that is ported as both:

- `ERM_TRIG`
- `GN_LD_EN`

That means the haptic trigger input and the visible alignment-laser enable appear to be electrically tied together in the current schematic set.

Firmware implication:

- treat `IO37` as a hazardous shared output until the hardware owner confirms whether this is intentional
- do not assume haptic trigger control is independent from visible-laser enable
- if this net sharing is real, software must avoid any haptic mode that could cause unintended visible-laser switching

## Bus Topology

### Default shared I2C bus

The ESP32 sheet includes resistor stuffing options with these notes:

- populate `R65`, `R66` for the DAC
- populate `R61`, `R62` for ERM
- this shares one I2C controller for `STUSB4500`, `DAC`, and `ERM`
- alternate stuffing `R63`, `R64`, `R67`, `R68` moves ERM and DAC to separate GPIO pairs

Default-stuffed connections shown:

| Signal | Default GPIO | Alternate GPIO | Stuffing |
| --- | --- | --- | --- |
| `ERM_SDA` | `GPIO4` | `GPIO35` | `R61=0R`, `R63=DNP` |
| `ERM_SCL` | `GPIO5` | `GPIO36` | `R62=0R`, `R64=DNP` |
| `DAC_SDA` | `GPIO4` | `GPIO11` | `R65=0R`, `R67=DNP` |
| `DAC_SCL` | `GPIO5` | `GPIO12` | `R66=0R`, `R68=DNP` |

The USB-PD board exports `ST_SDA` and `ST_SCL` to the main board connectors, so with default stuffing the board is using a single shared I2C bus on `GPIO4/GPIO5`.

Firmware implication:

- bus recovery and timeout handling on `GPIO4/GPIO5` are safety-relevant
- a stuck I2C bus can simultaneously blind PD, DAC, and haptic control
- arbitration is simple because the ESP32 is master, but failure impact is broad

## IC Interface Notes

### STUSB4500

Board wiring:

- `SDA` and `SCL` are connected to the main board.
- `ADDR0` and `ADDR1` are each pulled to `GND` with `100k`, so the effective 7-bit address is `0x28`.
- `ALERT`, `RESET`, `ATTACH`, `POWER_OK2`, `POWER_OK3`, and `GPIO` are not connected to the MCU.
- `VBUS_EN_SNK` is used to drive the sink power path.
- `CC1DB` and `CC2DB` are wired for dead-battery support.

Datasheet-derived notes:

- autonomous sink controller with up to three sink PDOs in NVM
- default NVM example in the datasheet is `5 V / 1.5 A`, `15 V / 1.5 A`, `20 V / 1.0 A`
- `POWER_ONLY_ABOVE_5V` exists and can keep the power path open until PDO2 or PDO3 is negotiated
- first I2C access is only valid after NVM load time `TLOAD` after power-up or reset

Firmware implication:

- power-tier classification must be done by register polling, not interrupt-driven GPIOs
- actual NVM PDO contents on the built board must be read out; they cannot be inferred from the schematic alone
- default datasheet PDOs are not sufficient for a `>35 W` full-power mode policy, so production units likely need custom NVM programming

### DAC80502

Board wiring:

- `SPI2C` is pulled high for I2C mode.
- `SYNC/A0` is tied low, so the 7-bit address is `0x48`.
- `R42` is populated for zero-scale power-up and `R41` is `DNP`, so the board powers up with DAC outputs at zero scale.
- `VOUTA -> DAC_OUTA -> ATLS6A214 LISH`
- `VOUTB -> DAC_OUTB -> TEC14M5V3R5AS TMS`

Datasheet-derived notes:

- power-on reset keeps outputs at zero or midscale depending on `RSTSEL`
- internal reference supports 1.25 V / 2.5 V / 5 V ranges depending on configuration
- in I2C mode the base address family is `1001xxx`; `A0 = GND` yields `1001000` (`0x48`)

Firmware implication:

- zero-scale startup behavior is aligned with a safe actuator default
- firmware should explicitly set gain/reference registers and then write both channels from a known-safe state
- readback/shadowing is still needed because this device controls both the LD current command and TEC target command

### LSM6DSO

Board wiring:

- direct SPI mode is the intended default
- `IO39 -> CS`
- `IO40 -> SCLK`
- `IO38 -> SDI`
- `IO41 -> SDO`
- `IO42 -> INT2`
- schematic notes say `R22` to `R26` are populated only for I2C operation, and the board note says mode 1 is the direct ESP32 connection

Datasheet-derived notes:

- supports SPI modes 0 and 3
- 4-wire SPI available up to 10 MHz per the datasheet timing section
- accelerometer and gyroscope can run independently
- embedded tilt/FSM features exist, but the host processor remains the correct safety authority

Firmware implication:

- SPI is the right runtime target
- `INT2` can be used as an assist for data-ready or coarse motion events, but not as the sole shutdown path
- host-side stale-data timing and beam-axis estimation still need to be implemented in firmware

### ATLS6A214 laser driver

Board wiring:

- `LD_SBDN` from `IO13`
- `LD_PCN` from `IO21`
- `DAC_OUTA -> LISH`
- `LISL` is hard tied to `GND`
- `LD_LIO -> IO2`
- `LD-TMO -> IO1`
- `LD-LPGD -> IO14`

Board notes:

- `LISL/LISH` map `0 V` to `2.5 V` into `0 A` to `6 A`
- `SBDN` resistor network creates three ESP32-controlled states:
  - GPIO low -> `0 V`
  - GPIO high -> `3.3 V`
  - GPIO Hi-Z -> about `2.25 V`

Datasheet-derived notes:

- `SBDN` thresholds:
  - `0 V` to about `0.4 V` = shutdown
  - about `2.1 V` to `2.4 V` = standby
  - about `2.6 V` to `14 V` = operation
- shutdown time after pulling `SBDN` low is about `20 us`
- start-up time after releasing `SBDN` into operate is about `20 ms`
- `PCN` selects `LISL` when low and `LISH` when high
- `LIO` provides `0 V` to `2.5 V` corresponding to `0 A` to `6 A`
- `TMO` has a temperature conversion defined in the datasheet
- `LPGD` indicates loop-good and goes low if setpoint current is not achieved or protection intervenes

Firmware implication:

- `SBDN` is the fast beam-off path
- because `LISL` is hard-grounded, `PCN=low` is effectively a zero-current selection
- `SBDN` Hi-Z defaulting to standby matches the stated hardware intent

### TEC14M5V3R5AS

Board wiring:

- `DAC_OUTB -> TMS`
- `TEC_TMO -> IO8`
- `TEC_ITEC -> IO9`
- `TEC_VTEC -> IO10`
- `TEC_TEMPGD -> IO47`
- power input `VPS` is `VDS_TEC_5V0`

Board notes:

- `VLIM` is set to `5 V` by `R35=0R`, `R36=DNP`
- `ILIM` is set to `1 A heating / 3.5 A cooling` by `R33=83k5`, `R34=52k3`
- `R38` to `R40` are chosen for a `5 C / 35 C / 65 C` temperature range

Datasheet-derived notes:

- default controller assumes `TMS = 1.25 V` corresponds to `25 C`
- default standard range is `15 C` to `35 C` for `0.1 V` to `2.5 V`
- `VPS` nominal input is `5 V`
- `ITEC = (VITEC - 1.25) / 0.285`
- `TEMPGD` indicates the target temperature has been reached

Firmware implication:

- the board is not using the datasheet-default 15 C to 35 C range; firmware must use board-specific calibration
- do not reuse generic TEC14 voltage-to-temperature assumptions without per-board validation

### DRV2605

Board wiring:

- `ERM_EN -> IO48`
- `ERM_TRIG -> IO37` on the shared net discussed above
- `ERM_SDA` and `ERM_SCL` go through the stuffing matrix
- actuator is an ERM motor, not an LRA, based on the schematic labels and part choice

Datasheet-derived notes:

- 7-bit I2C address is `0x5A`
- `EN` gates device active/shutdown state
- `IN/TRIG` can be used for external trigger, PWM, or analog input depending on mode

Firmware implication:

- simplest safe use is I2C configuration plus deterministic `EN` control
- external-trigger mode must be treated carefully because `IN/TRIG` appears shared with `GN_LD_EN`

## Connectors

### Main board: VIN board connector `J1`

From the main PCB connectors sheet:

| Pin | Signal |
| --- | --- |
| 1 | `ESP_GPIO4` |
| 2 | `ESP_USP-DP` |
| 3 | `ESP_GPIO5` |
| 4 | `ESP_USP-DN` |
| 5 | `ESP_GPIO7` |
| 6 | `ESP_GPIO6` |
| 9 | `GND` |
| 10 | `VIN` |

Pins `7` and `8` appear unassigned on this sheet.

### Main board: Sensor & LED board connector `J2A/J2B`

Visible connections on the main PCB connectors sheet:

| Pin | Signal |
| --- | --- |
| 1 | `DVDD_3V3` |
| 2 | `ESP_GPIO4` |
| 4 | `ESP_GPIO5` |
| 5 | `ESP_GPIO7` |
| 6 | `ESP_GPIO6` |
| 8 | `DVDD_3V3` |
| 10 | `GND` |

The uploaded ToF board makes the following functional use of those exported nets:

- `GPIO4` -> `LD_SDA` -> `VL53L1X SDA`
- `GPIO5` -> `LD_SCL` -> `VL53L1X SCL`
- `GPIO7` -> recommended host input for `LD_GPIO` -> `VL53L1X GPIO1` interrupt output
- `GPIO6` -> recommended host output for `LD_INT` -> `TPS61169 CTRL` on the LED string driver
- `DVDD_3V3` -> ToF board supply
- `GND` -> ToF board return

The board-level netlist does not show any `SDA` / `SCL` pull-ups on the ToF daughtercard, so the host-side shared I2C bus pull-ups remain required.

Firmware implication:

- the ToF board is I2C, not SPI
- the sensor device is `VL53L1X` with a default datasheet address of `0x52` on the wire (`0x29` 7-bit)
- `XSHUT` is pulled up locally on the daughterboard and is not exported to the MCU, so firmware cannot use a dedicated hardware-shutdown GPIO for this board revision
- `GPIO1` from the `VL53L1X` is exported and should be treated as an optional interrupt/ready line, not a sole safety authority
- the `LD_INT` sideband is not the ToF interrupt; it is the LED-driver control path and should be held low unless the illumination LEDs are intentionally being exercised

### BMS / battery board notes

From [BMS.pdf](/Users/zz4/BSL/BSL-Laser/docs/Schematics/BMS.pdf):

- `TPS2121` load sharing prioritizes `VBUS` over `VBAT` when `VBUS > 5 V`
- board exposes `VSYS`, `VBUS`, `VBAT`, USB `D+/D-`, and `GPIO6/GPIO7`
- this appears to be the future battery-capable front end, not the present laser-control core

## Missing or Incomplete Hardware Information

These gaps block a complete firmware pin/peripheral spec:

1. The separate button board is still missing, so the two-stage trigger wiring remains unresolved.
2. No datasheet is present for `MPM3612`, even though it generates the always-on 3.3 V rail.
3. No datasheets are present for several support ICs that matter during bring-up:
   - `TPS22918`
   - `TPS2121`
   - `TPD6S300A`
   - `LMV331`
4. The actual STUSB4500 NVM PDO programming for this board is not documented in the schematics.
5. The `IO37` shared-net situation should be verified in the source CAD files, not only from the PDF export.

## Firmware Recommendations Based On The Hardware

1. Treat `GPIO4/GPIO5` as a shared critical bus and implement bus-reset, timeout, and stale-device detection from day one.
2. Treat `IO37` as unsafe to use until the `ERM_TRIG` / `GN_LD_EN` sharing is explicitly accepted or fixed.
3. Reserve `IO1`, `IO2`, `IO8`, `IO9`, and `IO10` for ADC1-only usage as the schematic note intends.
4. Default the DAC to an explicit zero-safe configuration immediately after boot even though the hardware stuffing already biases it that way.
5. Plan STUSB4500 supervision around I2C polling only, because no alert/status GPIOs are wired.
6. Do not finalize the ToF interlock module until the `VL53L1X` driver, timing budget, stale-data handling, and interrupt policy are implemented and validated on real hardware.

## Button Board Addendum (2026-04-15)

The button board is now source-backed and shares the **MainPCB J2 Sensor & LED** physical connector with the ToF daughterboard. The board adds two I²C devices to the shared `GPIO4`/`GPIO5` bus:

- `MCP23017` GPIO expander @ `0x20` — A2:A0 strapped to GND. Open-drain `INTA` output → ESP32 `GPIO7`. Internal pull-up enabled on `GPIO7` ESP-side.
- `TLC59116` LED driver @ `0x60` — A3:A0 strapped to GND. Drives a single RGB LED on `OUT0`/`OUT1`/`OUT2` with non-standard `B`/`R`/`G` channel order per the schematic.

`GPIO7` was previously the `VL53L1X GPIO1` data-ready input. As of 2026-04-15 it is owned exclusively by the MCP23017 INTA. The ToF chip's `GPIO1` output is no longer wired into the ESP-side input — verify on the new revision schematic before populating both daughterboards together. The ToF runs in polling-only mode via the `RANGE_STATUS` register (existing 75 ms intermeasurement cadence).

Implication for stuffing: with the button board fitted, the shared `GPIO4`/`GPIO5` bus carries 6 distinct addresses + the TLC59116 ALLCALL slot (`0x68`). No collisions. If a future revision pushes I²C to 400 kHz, re-verify the parallel pull-up network on a scope.
