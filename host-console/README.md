# BSL Host Console

Cross-platform host monitoring software for the handheld 785 nm surgical laser controller.

## What It Includes

- live controller overview
- bench control page for laser, TEC, wavelength, and PCN modulation testing
- bring-up page for module-population profiling and staged device tuning
- service-focused bring-up landing page with decoded I2C/SPI tooling and runtime safety policy editing
- event and fault timeline
- guarded command deck
- firmware package staging, preflight, and raw app-binary browser flashing over Web Serial
- mock transport for UI development and protocol shaping
- Web Serial transport for browser-based USB CDC integration
- Wireless transport over the ESP32 bench SoftAP and WebSocket bridge
- session export for bench records and fault review

## GUI Shape

The interface is built as a modern instrument console rather than a consumer-style admin panel:

- left navigation for workflow selection
- a dense telemetry workspace in the center
- an inspector rail for identity, faults, recent commands, and archive export
- restrained motion and an operator-friendly dark theme by default

## Safety Position

This host console does not create a bypass path around firmware interlocks.

- All operator actions are modeled as commands, not direct pin control.
- Service actions are visually separated and intentionally gated.
- Firmware staging checks for beam-off and safe-state conditions before transfer.

## Run

Fastest start on macOS:

```bash
/Users/zz4/BSL/BSL-Laser/start-host-console.command
```

Manual start:

```bash
npm install
npm run dev
```

Then open the local Vite URL in a Chromium-based browser.

Use `Mock rig` for the fully interactive demo.

Use `Web Serial` to connect to a live controller over browser-supported serial.

Use `Wireless` when USB-C is occupied by the PD source and the controller is running the wireless bench image.
The current bench wireless path is Wi‑Fi SoftAP + WebSocket on purpose; BLE is intentionally not the primary browser link because sustained browser support and telemetry stability are worse.
The current wireless bench defaults are:

- SSID: `BSL-HTLS-Bench`
- password: `bslbench2026`
- WebSocket URL: `ws://192.168.4.1/ws`

Typical wireless bench sequence:

1. Power the controller from the PD source.
2. Join the laptop to `BSL-HTLS-Bench`.
3. Keep the host console open on `localhost`.
4. Choose `Wireless` in the GUI and connect to `ws://192.168.4.1/ws`.

The `Control` tab now includes:

- optical-power setpoint control for the 5 W / 5 A laser model, including optional auto-follow without a separate apply step
- TEC target control in either temperature or wavelength space using the provided calibration map
- PCN modulation setup with frequency, duty cycle, and LISL low-state current
- service-mode entry and exit directly from the bench control page
- live estimated optical power, electrical power, TEC cooling power, and total wall draw
- color-coded rail PGOOD, lambda stability, and TEC thermal status badges

The `Bring-up` tab now includes:

- a service landing page plus dedicated pages for IMU, DAC, DRV2605, ToF, PD, buttons, laser-driver, and TEC bring-up
- explicit service-mode entry and exit for bench-only staged integration
- module expectation toggles so missing ToF, buttons, TEC, or laser hardware can be declared during bring-up
- host-local draft save and restore for persistent module/tuning profiles before firmware NVS exists
- hover help on bring-up controls so settings are explained in-place
- richer datasheet-backed tuning for:
  - DAC80502 reference, gain, divider, update mode, and channel shadows
  - LSM6DSO ODR, full-scale, gyro enablement, BDU, IF_INC, LPF2, timestamping, and I2C disable
  - DRV2605 mode, library, actuator type, RTP level, and effect selection
  - generic ToF range and stale-timeout staging while the actual ToF datasheet is still missing
- I2C scan plus I2C register read and write helpers
- SPI register read and write helpers for the IMU path
- decoded bus readback so key I2C/SPI activity is annotated with device and register meaning instead of only raw hex

The `Firmware` tab now includes:

- package staging and preflight
- raw app-binary browser flashing over Web Serial for images with a valid embedded BSL firmware signature block
- a visual board guide showing the physical `RST` and `BOOT` buttons on the actual board layout
- a step-by-step fallback BOOT/RST sequence for stubborn flash sessions

Current live-device expectation:

- the firmware emits newline-delimited JSON envelopes
- USB CDC and Wireless both carry the same newline-delimited JSON protocol
- `status_snapshot` events update the main telemetry snapshot
- `log` events populate the event timeline
- the current bench firmware accepts the full `Control` and `Bring-up` page command surface, but mutating commands are only honored while service mode is requested or active
- the live snapshot includes `bench`, `safety`, and `bringup` sections so the UI does not have to infer staged controller state
- the live snapshot now also includes a `wireless` section so the GUI can show AP readiness and live client count
- non-JSON boot text and ESP-IDF console output are preserved as `console` events instead of being dropped

For a production bundle:

```bash
npm run build
```

## Current Limits

- Web Serial flashing currently supports raw local app binaries only, requires a valid embedded BSL firmware signature block, and assumes the app image should be written at `0x10000`.
- Wireless transport does not implement browser flashing. Use it for monitoring and bench control only.
- Use the ESP-IDF CLI for first-program, bootloader, partition-table, or full-chip recovery work.
- The current firmware implements both service-only bring-up staging and bench-control staging, but real peripheral transactions are still backed by a mock board layer until hardware drivers land.
- The mock rig supports firmware-transfer simulation, not real flash programming.
- The TEC wavelength mapping in the host is calibration-driven from the supplied bench table, including two endpoint temperatures inferred from the stated 5 °C to 65 °C operating range.
- No desktop packaging is included yet; this is a desktop-oriented local web app.
- The host remains advisory only. All safety interlocks and beam-permission decisions must stay in firmware.
