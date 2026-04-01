# Host Console Architecture

## Goal

Provide a modern PC/Mac operator and bench console for:

- live telemetry and state monitoring
- event and fault review
- guarded command dispatch
- firmware package inspection and staged update workflow

The host is intentionally not the safety authority. The controller firmware remains authoritative for beam permission, interlocks, and fault latching.

## Safety Posture

The host console must preserve these rules:

- no direct beam-enable path from the GUI
- no assumption that a command acknowledgement means output is safe or active
- no hidden service-only controls in the normal operator surface
- firmware-update actions must require a beam-off, safe-state preflight
- transport loss or stale host data must never be interpreted as permission to continue emission

The GUI reflects controller state. It does not override it.

## Implementation

The host app lives in [host-console](/Users/zz4/BSL/BSL-Laser/host-console) and is built with:

- React 19
- TypeScript
- Vite
- Framer Motion
- `lucide-react`

The visual direction is a light clinical instrument console rather than a generic dark dashboard.

## File Map

- [host-console/src/App.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/App.tsx)
  Main shell, navigation, workspace selection, and top-level composition.
- [host-console/src/hooks/use-device-session.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/hooks/use-device-session.ts)
  Session state, transport lifecycle, command history, firmware progress, and export flow.
- [host-console/src/lib/mock-transport.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/mock-transport.ts)
  Deterministic simulation rig for UI development and protocol shaping.
- [host-console/src/lib/web-serial-transport.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/web-serial-transport.ts)
  Browser-side Web Serial transport for newline-delimited JSON protocol traffic.
- [host-console/src/lib/firmware.ts](/Users/zz4/BSL/BSL-Laser/host-console/src/lib/firmware.ts)
  Firmware package parsing, checksum calculation, and update preflight checks.
- [host-console/src/components/StatusRail.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/StatusRail.tsx)
  Live KPI strip for power, distance, pitch, TEC target, and protocol version.
- [host-console/src/components/SafetyMatrix.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/SafetyMatrix.tsx)
  Host rendering of controller beam-permission prerequisites.
- [host-console/src/components/EventTimeline.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/EventTimeline.tsx)
  Searchable and severity-filtered event view.
- [host-console/src/components/CommandDeck.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/CommandDeck.tsx)
  Guarded read, write, service, and mock-injection command panels.
- [host-console/src/components/ControlWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/ControlWorkbench.tsx)
  Bench-control workspace for staged laser, TEC, and modulation requests.
- [host-console/src/components/BringupWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/BringupWorkbench.tsx)
  Service-mode bring-up workspace for module expectations, bus diagnostics, and tuning.
- [host-console/src/components/FirmwareWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/FirmwareWorkbench.tsx)
  Package loading, segment inspection, preflight checklist, and transfer progress.
- [host-console/src/components/InspectorRail.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/InspectorRail.tsx)
  Device identity, transport state, fault summary, command history, and session export.

## Views

### Overview

Shows:

- controller identity and operating state
- live power, rails, laser, TEC, IMU, and ToF telemetry
- beam-permission matrix
- recent event timeline

### Events

Shows:

- full event and fault history
- search by title, detail, or category
- severity filtering

### Firmware

Shows:

- manifest or binary package import
- SHA-256 digest
- package metadata and segments
- safe-state preflight checklist
- staged transfer progress

Current limitation:

- direct flashing is only simulated in mock mode
- Web Serial transport does not yet implement an ESP32 ROM flashing pipeline

### Control

Shows:

- staged laser power, TEC target, and modulation requests
- explicitly armed service-only output intent commands
- live power and efficiency estimates
- visual status feedback for NIR, alignment, TEC settling, and faults

This is a bench workspace, not a normal operator workflow. The controller firmware still decides whether anything may actually turn on.

### Bring-up

Shows:

- protected service-mode entry and exit
- module expected-present and debug-enabled toggles
- I2C and SPI diagnostics
- staged DAC, IMU, ToF, and DRV2605 tuning
- host-local draft save and restore for partially populated bench builds

## Transport Model

### Mock

`MockTransport` is the default development transport. It simulates:

- power tiers
- TEC settling
- beam-pitch and distance telemetry
- fault latching
- firmware transfer progress

Use it to validate UI flows before the real board drivers are complete.

### Web Serial

`WebSerialTransport` expects newline-delimited JSON messages based on [protocol-spec.md](/Users/zz4/BSL/BSL-Laser/docs/protocol-spec.md).

Current assumptions:

- 115200 baud
- UTF-8 text lines
- `cmd`, `resp`, and `event` envelopes
- optional status snapshot returned in `resp.result` or `event.payload`

## Firmware Update Model

The current workflow is:

1. Load a firmware manifest or raw binary.
2. Compute SHA-256 locally in the browser.
3. Verify preflight conditions:
   - package present
   - host link live
   - both optical outputs off
   - controller in service or programming state
   - no active latched fault
4. Begin transfer.

This is production-shaped, but not yet production-complete, because actual serial flashing and signing enforcement still need implementation.

## Next Work

1. Replace the firmware mock board/service backends with real peripheral drivers while preserving the existing JSON contract.
2. Add structured snapshot schemas and stricter protocol validation.
3. Add signed-manifest verification and board-revision compatibility checks.
4. Decide whether production flashing should be done through:
   - firmware-resident update commands, or
   - a native wrapper around `esptool`
5. Add operator authentication or protected workflows before exposing service-only actions outside bench use.
