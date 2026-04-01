# Peripheral Programming Notes

These notes distill the local datasheets into firmware-facing guidance for bring-up, mode selection, and safe defaults.

Use this file as a starting point, not a substitute for reading the original datasheet before writing a driver.

## Scope

Sources currently present in [docs/Datasheets](/Users/zz4/BSL/BSL-Laser/docs/Datasheets):

- [dac80502-2.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/dac80502-2.pdf)
- [drv2605.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/drv2605.pdf)
- [lsm6dso.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/lsm6dso.pdf)
- [stusb4500.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/stusb4500.pdf)
- [vl53l1x.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/vl53l1x.pdf)
- [ATLS6A214D-3.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/ATLS6A214D-3.pdf)
- [Micro_TEC_Controller_TEC14M5V3R5AS-2.pdf](/Users/zz4/BSL/BSL-Laser/docs/Datasheets/Micro_TEC_Controller_TEC14M5V3R5AS-2.pdf)

## Cross-Cutting Rules

- Prefer reset-to-known-state on every service-mode attach and every driver init.
- Keep high-risk outputs in their non-active state before touching config registers.
- Use the board-level rail enables as the primary hard safety boundary. Peripheral register programming is secondary.
- If a peripheral supports both autonomous and host-driven behavior, keep the autonomous safety-relevant behavior unless there is a clear reason to override it.
- Treat shared-bus access as a first-class design constraint. `GPIO4/GPIO5` are a shared I2C bus on this board.

## DAC80502

Board context:

- The board uses the DAC in I2C mode.
- Board-level recon indicates the stuffed address is `0x48`.

Datasheet-derived behavior:

- Interface mode is selected by `SPI2C` at power-up and must remain static afterward.
- Power-on reset drives the DACs to zero scale or midscale depending on `RSTSEL`.
- Communication is valid only after the POR delay.
- Internal 2.5 V reference is enabled by default.
- DAC output update is asynchronous by default. In I2C mode, output updates on the last acknowledge unless synchronous mode is enabled.
- Software reset is triggered through the `TRIGGER` register.
- Power-down control for outputs and internal reference lives in the `CONFIG` register.
- Reference divide and output buffer gain live in the `GAIN` register.

Firmware guidance:

- Assume startup output is only as safe as the board stuffing around `RSTSEL`. Verify this on hardware before enabling any downstream actuator path.
- On init, read back the device identity path indirectly by verifying register access and then program:
  - output update mode explicitly
  - reference source explicitly
  - gain/divider explicitly
  - both channels to known-safe shadow values before any actuator rail is enabled
- Keep channel ownership fixed:
  - channel A: laser-current command
  - channel B: TEC target command
- Do not rely on power-up defaults alone. Reassert zero-safe codes during bring-up and after software reset.
- Prefer software reset only in service mode or during controller boot, not while any real actuator rail is enabled.

Bring-up configuration knobs worth exposing in tooling:

- reference source:
  - internal `2.5 V`
  - external reference only after board stuffing is confirmed
- output update mode:
  - asynchronous update on write
  - synchronous update requiring later trigger or LDAC-style commit
- output span controls:
  - `BUFF-GAIN` per channel
  - `REF-DIV` for reference divide-by-two behavior
- safe-state reminders:
  - `RSTSEL` decides zero-scale versus midscale power-up posture
  - `CONFIG` can power down the outputs and internal reference

Suggested driver shape:

- `dac_init_safe()`
- `dac_write_shadow()`
- `dac_commit_outputs()`
- `dac_enter_powerdown()`
- `dac_soft_reset()`

## DRV2605

Board context:

- Board-level recon indicates the stuffed I2C address is `0x5A`.
- The board netlist suggests the haptic trigger net may be entangled with the visible laser enable net. Treat that as hazardous until the hardware is proven otherwise.

Datasheet-derived behavior:

- `STANDBY` in register `0x01` is asserted by default.
- `DEV_RESET` in register `0x01` performs a full device reset and self-clears.
- `MODE[2:0]` also lives in register `0x01`.
- `RTP_INPUT` is register `0x02`.
- `LIBRARY_SEL` is register `0x03`.
- `GO` is register `0x0C`.
- The device supports ERM and LRA. Actuator selection is made through the feedback-control configuration path.
- The part supports internal library playback, RTP mode, PWM/analog mode, diagnostics, and auto-calibration.
- The part keeps register contents in standby and can stop playback immediately when standby is asserted.

Firmware guidance:

- Default policy should keep the DRV2605 in standby unless a short, deterministic haptic event is being played.
- Use explicit mode programming every time:
  - reset
  - configure actuator type
  - configure library or RTP mode
  - clear standby only for the minimum time needed
- For this project, start with simple ERM playback through internal trigger or RTP mode only. Do not enable analog or PWM modes unless they are specifically needed.
- Keep haptics outside the safety path. Loss of DRV2605 must never affect beam shutdown.
- If the shared `IO37` net hazard is real, do not allow any autonomous haptic toggling that could couple into visible-laser control.

Bring-up configuration knobs worth exposing in tooling:

- mode selection from register `0x01`:
  - internal trigger
  - external trigger edge
  - external trigger level
  - PWM or analog input
  - audio-to-vibe
  - RTP
  - diagnostics
  - auto-calibration
- actuator selection:
  - ERM
  - LRA
- library selection:
  - ERM libraries `1` through `5`
  - LRA library `6`
- service-only amplitude and effect controls:
  - waveform/effect ID
  - RTP level

Suggested driver shape:

- `drv2605_init_safe_erm()`
- `drv2605_set_standby(bool)`
- `drv2605_set_mode()`
- `drv2605_play_effect(effect_id)`
- `drv2605_rtp_write(level)`
- `drv2605_stop()`

## LSM6DSO

Board context:

- Target runtime bus is SPI.
- Board-level recon indicates:
  - `CS = GPIO39`
  - `SCLK = GPIO40`
  - `SDI = GPIO38`
  - `SDO = GPIO41`
  - `INT2 = GPIO42`

Datasheet-derived behavior:

- `WHO_AM_I` at `0x0F` must read `0x6C`.
- Primary interface supports SPI modes 0 and 3.
- Multi-byte register access is controlled by `IF_INC` in `CTRL3_C` (`0x12`).
- Software reset is controlled in `CTRL3_C`.
- The I2C block can be disabled with `I2C_disable` in `CTRL4_C` (`0x13`).
- Accelerometer ODR is configured in `CTRL1_XL` (`0x10`).
- Gyroscope ODR is configured in `CTRL2_G` (`0x11`).
- Interrupt routing uses `INT1_CTRL` (`0x0D`) and `INT2_CTRL` (`0x0E`).
- Accelerometer and gyroscope both support power-down until ODR is set nonzero.

Firmware guidance:

- Use SPI only in production firmware and explicitly disable I2C in `CTRL4_C` after link verification.
- Safe init sequence should:
  - read `WHO_AM_I`
  - issue software reset
  - wait for reset completion
  - set `BDU`
  - set `IF_INC`
  - disable I2C
  - configure accelerometer and gyroscope ODR/full-scale
  - route only the interrupts actually used
- Do not treat embedded tilt/6D/orientation engines as the primary safety authority. They may be used as auxiliary hints or low-power diagnostics only.
- The host safety decision must continue using raw sampled data and explicit freshness checks.
- For first bring-up, prefer:
  - accelerometer on
  - gyro optional
  - timestamping enabled if practical
  - polling path working before interrupt-driven shortcuts

Bring-up configuration knobs worth exposing in tooling:

- ODR options commonly worth staging first:
  - `12.5 Hz`
  - `26 Hz`
  - `52 Hz`
  - `104 Hz`
  - `208 Hz`
  - `416 Hz`
- accelerometer full-scale:
  - `2 g`
  - `4 g`
  - `8 g`
  - `16 g`
- gyroscope full-scale:
  - `125 dps`
  - `250 dps`
  - `500 dps`
  - `1000 dps`
  - `2000 dps`
- runtime flags:
  - `BDU`
  - `IF_INC`
  - `I2C_disable`
  - timestamp enable
  - LPF2 selection when filtering is intentionally being evaluated

Suggested driver shape:

- `lsm6dso_reset_and_identify()`
- `lsm6dso_configure_runtime_spi()`
- `lsm6dso_read_sample()`
- `lsm6dso_check_freshness()`
- `lsm6dso_route_interrupts()`

## STUSB4500

Board context:

- Board-level recon indicates the stuffed address is `0x28`.
- The current board does not wire `ALERT`, `ATTACH`, `POWER_OK`, or `RESET` back to the MCU.

Datasheet-derived behavior:

- The part is an autonomous USB-PD sink controller with NVM-configured sink PDOs.
- Available I2C addresses are `0x28` through `0x2B` via `ADDR0/ADDR1`.
- It negotiates power autonomously in auto-run mode using NVM-configured sink PDOs.
- `VBUS_EN_SNK` can be configured to assert for any attached source or only above 5 V explicit contracts.
- `POWER_OK2` and `POWER_OK3` indicate negotiated contract status depending on NVM configuration.
- `ALERT` is an interrupt output, but this board does not connect it to the MCU.
- VBUS monitoring and discharge behavior are built into the device.

Firmware guidance:

- Treat this device as mostly autonomous at runtime.
- Do not build firmware policy around missing GPIOs. Poll negotiated-status registers instead.
- Read actual contract state and classify it into firmware power tiers:
  - host-only / 5 V
  - insufficient
  - reduced
  - full
- Do not assume the sink PDO configuration in NVM is correct. Read and log what the chip reports.
- If reprogramming NVM is ever needed, do it as a manufacturing/service action only, not in the normal control loop.
- Treat STUSB4500 NVM writes as finite-endurance provisioning actions. Validate runtime PDO behavior first and avoid using NVM burn as a tuning loop.

Suggested driver shape:

- `stusb4500_probe()`
- `stusb4500_read_contract()`
- `stusb4500_classify_power_tier()`
- `stusb4500_dump_nvm_summary()`

## ATLS6A214 Laser Driver

Datasheet-derived behavior:

- `SBDN` selects shutdown, standby, or operation by voltage threshold.
- Pulling `SBDN` low shuts the driver down quickly.
- Releasing `SBDN` into operation has a much slower startup time.
- `PCN` selects which current-set input is active:
  - low selects `LISL`
  - high selects `LISH`
- `PCN` has an internal pull-up, so an un-driven `PCN` defaults toward high-state current selection.
- `LISH` and `LISL` accept `0` to `2.5 V` analog current commands.
- `LIO` provides an analog current monitor with the same `0` to `2.5 V` scaling.
- `LPGD` indicates loop-good state.
- `TMO` reports internal temperature, and the part forces itself into standby above its internal overtemperature threshold.

Firmware guidance:

- Hard safety policy should continue to use `SBDN` as the primary fast beam-off path.
- Because `PCN` pulls high internally, do not leave it floating. Drive it explicitly.
- Keep `LISL` at safe-low or zero by default. Do not assume the inactive current path is harmless.
- Verify `LIO` against commanded `LISH/LISL` continuously and fault on mismatch.
- Sample `LPGD` only as an additional status indicator, not the sole proof of safe operation.

Suggested driver shape:

- `laser_driver_set_sbdn_state()`
- `laser_driver_set_currents(lisl_v, lish_v)`
- `laser_driver_set_pcn(bool high_state)`
- `laser_driver_read_lio_current()`
- `laser_driver_read_temp()`

## TEC14M5V3R5AS

Datasheet-derived behavior:

- This is an analog TEC controller, not a digital register-programmed peripheral.
- `TMS` is the temperature setpoint input.
- `TEMPGD` indicates setpoint reached.
- `VTEC` reports TEC voltage.
- `ITEC` reports TEC current, with a datasheet transfer function around a 1.25 V center point.
- `TMS` is tied internally so the default network centers around 25 C in the stock configuration.
- The stock network shown in the datasheet covers a 15 C to 35 C setpoint span, but the module is customizable with external resistor selection.

Firmware guidance:

- Treat the controller as an analog plant supervised by firmware, not something to be “configured” over a bus.
- The real firmware job is:
  - generate a safe `TMS` voltage through the DAC
  - read `TEMPGD`
  - digitize `VTEC` and `ITEC`
  - enforce plausibility and timeout rules
- Do not blindly use the stock 15 C to 35 C mapping from the module datasheet for this project. Your bench table and board calibration clearly extend beyond that.
- Keep all TEC setpoint and wavelength mapping in the project calibration layer, not hard-coded in the driver.

Suggested driver shape:

- `tec_set_target_temp_c()`
- `tec_set_target_lambda_nm()`
- `tec_read_status()`
- `tec_is_settled()`

## ToF Sensor

Board context:

- The uploaded ToF daughterboard uses `VL53L1CXV0FY/1`, the `VL53L1X` family optical ToF sensor.
- The daughterboard wiring is:
  - `LD_SDA` -> sensor `SDA`
  - `LD_SCL` -> sensor `SCL`
  - `LD_GPIO` -> sensor `GPIO1` interrupt output
  - sensor `XSHUT` has a local `10 kOhm` pull-up and is not exported to the host connector
- The same daughterboard also carries a separate LED boost driver whose control input is named `LD_INT`; that net is not the ToF interrupt.

Datasheet-derived behavior:

- Interface is `I2C`, up to `400 kHz`.
- Datasheet default address is `0x52` on the wire, which corresponds to `0x29` in 7-bit host notation.
- `GPIO1` is an open-drain interrupt output.
- `XSHUT` is active low and is required for hardware standby, but this board revision does not export it to the MCU.
- `GPIO1` and `XSHUT` both want approximately `10 kOhm` pull-ups.
- The part uses a single `2.6 V` to `3.5 V` supply.
- ST recommends I2C pull-ups near the host; the ToF daughterboard netlist does not add its own `SDA` / `SCL` pull-ups.

Firmware guidance:

- Treat this board as an I2C ToF target on the existing shared `GPIO4/GPIO5` bus. Do not put it on SPI.
- Because `XSHUT` is not exported, firmware cannot use a dedicated GPIO for hardware reset, deep standby entry, or multi-sensor address sequencing on this revision.
- Use polling as the baseline implementation; `GPIO1` can be added as an optional interrupt/ready hint later, but host logic must remain authoritative for stale/invalid handling.
- If the board illumination LEDs are not under test, drive `LD_INT` low or leave the LED-control path otherwise forced inactive. Do not leave that control input floating.
- Driver work still needs the ST API/register model review before registers are hardcoded into mainline firmware.

## Practical Next Step

When real drivers land, each module patch should include:

- the init sequence derived from its datasheet
- the safe default state after init
- the reset path
- the mode selection policy
- any board-specific strap assumptions
- the exact fault behavior when the part is missing, stale, or implausible
