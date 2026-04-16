---
name: gui-change
description: Mandatory before ANY host console change under host-console/. Documents the dead-code map (domain/, platform/, styles.css, App.css), canonical types.ts surface, transport classes (Mock/WebSerial/WebSocket), and common mistakes. Trigger on edits to host-console/src/**, component additions, transport changes, React hooks edits, CSS changes, or workspace layout changes. Load alongside Uncodixfy for design-language rules.
---

# gui-change

Use this skill before making ANY GUI change.

## Before You Touch Anything

1. Read `.agent/skills/Uncodixfy/SKILL.md` for the design language rules
2. Check `host-console/src/types.ts` for the canonical type definitions
3. Check `host-console/src/hooks/use-device-session.ts` for state management

## Architecture Quick Reference

- State: pure React hooks via `useDeviceSession()`. No Redux/Zustand/context.
- Two-tier telemetry: full `DeviceSnapshot` + `RealtimeTelemetryStore` (useSyncExternalStore)
- `useLiveSnapshot()` merges both into what components see
- CSS: hand-written with custom properties in `index.css`. NOT Tailwind. Use `index.css`, not `styles.css` (legacy).
- Fonts: Space Grotesk (sans) + IBM Plex Mono (mono)

## Dead Code — Do Not Use

These exist but are NOT used by the current app:
- `domain/model.ts`, `domain/protocol.ts`, `domain/mock.ts` — legacy type system
- `platform/bridge.ts`, `platform/browserMock.ts`, `platform/tauriBridge.ts` — legacy Tauri bridge
- `styles.css` — legacy CSS with different font stack
- `App.css` — vestigial Vite scaffold

## Transport Classes

All implement `DeviceTransport` from `types.ts`:
- `MockTransport` — deterministic simulation, auto-deploys on connect
- `WebSerialTransport` — Web Serial API, 115200 baud, esptool-js flashing
- `WebSocketTransport` — WiFi WebSocket, NO firmware transfer

## Common Mistakes

1. **DO NOT use `domain/` types** — use `types.ts` (`DeviceSnapshot`, `CommandEnvelope`)
2. **DO NOT add CSS to `styles.css`** — use `index.css`
3. **DO NOT call private methods on transport classes** — use `sendCommand()` with full `CommandEnvelope` including `type: 'cmd'`
4. **Mock `sendCommand` requires `{id, type: 'cmd', cmd}`** — not just `{id, cmd}`
