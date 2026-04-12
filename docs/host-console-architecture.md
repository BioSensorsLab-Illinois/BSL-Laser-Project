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

The visual direction is now a restrained light instrument console with five normal workspaces:

- `System`
- `Operate`
- `Integrate`
- `Update`
- `History`

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
- [host-console/src/components/OperateConsole.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/OperateConsole.tsx)
  Operate workspace for compact deployment control, runtime-mode selection, staged output, TEC targets, and the bottom-mounted deployment log.
- [host-console/src/components/BringupWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/BringupWorkbench.tsx)
  Service-mode bring-up workspace for module expectations, bus diagnostics, and tuning.
- [host-console/src/components/FirmwareWorkbench.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/FirmwareWorkbench.tsx)
  Package loading, segment inspection, preflight checklist, and transfer progress.
- [host-console/src/components/InspectorRail.tsx](/Users/zz4/BSL/BSL-Laser/host-console/src/components/InspectorRail.tsx)
  Device identity, transport state, fault summary, command history, and session export.

## Workspaces

### System

Shows:

- controller identity and operating state
- live power, rails, laser, TEC, IMU, and ToF telemetry
- beam-permission matrix
- recent event timeline
- transport and Wi-Fi workflow

### Operate

Shows:

- deployment entry and exit
- a wizard-style deployment workspace with:
  - narrow checklist rail
  - single active-step panel
  - compact runtime support cards
- fixed 25 C deployment context instead of a deployment target editor
- runtime-mode selection
- runtime output and modulation controls
- independent green-laser and GPIO6 LED controls
- passive PD state only, with any explicit PD refresh or PDO write owned by `Integrate`
- read-only safety summary
- deployment-causal log at the bottom of the page with primary fault and secondary effects split

Checklist success is only meaningful when the controller reaches true idle-ready posture with TEC and LD held up correctly.

### Integrate

Shows:

- protected service-mode entry and exit
- persistent runtime safety editor
- bring-up module expectations, bus diagnostics, and tuning
- direct command workspace for low-level tool access
- the only UI surface allowed to trigger STUSB4500 refreshes, runtime PDO writes, firmware PDO-plan saves, or STUSB NVM burns

### Update

Shows:

- manifest or binary package import
- SHA-256 digest
- package metadata and segments
- safe-state preflight checklist
- staged transfer progress

### History

Shows:

- full event and fault history
- search by title, detail, or category
- severity filtering

Current limitation:

- direct flashing is only simulated in mock mode
- Web Serial transport does not yet implement an ESP32 ROM flashing pipeline

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

1. Finish moving the active host contract toward `status.*`, `deployment.*`, `operate.*`, and `integrate.*`.
2. Add structured snapshot schemas and stricter protocol validation.
3. Add signed-manifest verification and board-revision compatibility checks.
4. Decide whether production flashing should be done through:
   - firmware-resident update commands, or
   - a native wrapper around `esptool`
5. Add operator authentication or protected workflows before exposing service-only actions outside bench use.
