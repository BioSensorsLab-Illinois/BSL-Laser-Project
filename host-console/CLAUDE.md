# Host Console Guardrails

You are editing the **BSL host console** — React 19 + TypeScript browser-based operator console for the laser controller. This file applies in addition to the repo-root `CLAUDE.md` and `.agent/AGENT.md`.

**Mandatory before any edit here:**

- `.agent/skills/gui-change/SKILL.md` — canonical types, transport classes, dead-code map, common mistakes.
- `.agent/skills/Uncodixfy/SKILL.md` — the design language. Read it fully.

## Dead Code — Do Not Touch / Import From

- `src/domain/model.ts`, `src/domain/protocol.ts`, `src/domain/mock.ts` — legacy type system
- `src/platform/bridge.ts`, `src/platform/browserMock.ts`, `src/platform/tauriBridge.ts` — legacy Tauri bridge
- `styles.css` — legacy CSS (Work Sans + JetBrains Mono)
- `App.css` — vestigial Vite scaffold

**Use instead:**

- `src/types.ts` — canonical `DeviceSnapshot`, `CommandEnvelope`, transport contract
- `src/hooks/use-device-session.ts` — state management via hooks (no Redux/Zustand/context)
- `src/index.css` — canonical CSS with Space Grotesk + IBM Plex Mono, design tokens in custom properties
- `src/lib/mock-transport.ts` / `src/lib/web-serial-transport.ts` / `src/lib/web-socket-transport.ts` — transports that implement `DeviceTransport`

## Command Envelope Shape

Always `{id, type: 'cmd', cmd, args}`. Missing `type: 'cmd'` will break `MockTransport`.

## Design Language (Uncodixfy, in one paragraph)

This is an instrument console, not a marketing page. Restrained. Functional. Think Linear / Raycast / Stripe / GitHub. **Banned**: glass / frosted / dark-glass surfaces, hero sections in dashboards, pill overload, oversized rounded corners (>12px), gradient text, dramatic drop shadows (>8px blur), transform animations (scale/rotate/skew), ornamental labels, serif+sans combos, decorative color drift toward unused blue. **Required**: 240-260px fixed sidebar, solid 1px borders, 4/8/12/16/24/32 px spacing scale, body 14-16px, radii 8-12px max, shadows `0 2px 8px rgba(0,0,0,0.1)` max, transitions 100-200ms opacity/color only.

## Operate Workspace Boundaries

- Operate owns deployment + runtime control. NO explicit PD refresh or PDO writes. Those live in `BringupWorkbench` / Integrate only.
- Disabled controls MUST have a `title` / `aria-label` / visible hover help explaining why.
- OFF / INVALID / IN-DEPLOYMENT telemetry states render explicitly. Never silently blank.
- No hidden service controls in normal operator UI.

## After Every Edit

1. Run `cd host-console && npm run build`. Must pass.
2. Run `/render-check` — starts dev server, captures screenshots, runs Uncodixfy critique.
3. Run `/audit-gui` (spawns `bsl-uncodixfy-reviewer`, `bsl-interaction-reviewer`, `bsl-consistency-reviewer`).
4. If a protocol command was touched, spawn `bsl-protocol-auditor` for four-place sync.
5. Update `.agent/runs/powered-ready-console/Status.md` with screenshot paths and build result.

## The Rule

If a UI choice feels like a default AI move, ban it and pick the harder, cleaner option.
