---
description: Run the three mandatory Powered Phase 2 validation passes via the bsl-bench-validator sub-agent. Returns PASS / FAIL / BLOCKED. Records completion via mark-audit-done.py on PASS.
---

Spawn the `bsl-bench-validator` sub-agent (C1 from the review loop).

## Before Spawning

Confirm:

1. A powered bench is available (TEC + LD rails energized) OR explicitly note that it is not.
2. The firmware image on the device matches the current source (via `firmwareVersion` + `buildUtc` from `status.get`). A stale flashed image invalidates any result.
3. The transport to use (`serial` via `/dev/cu.usbmodem*` or `ws` via `ws://192.168.4.1/ws`).

## Spawn

Use the `Agent` tool with `subagent_type: "bsl-bench-validator"`. Pass it the transport, port, and any specific scope expectations (e.g. "diff touches SBDN — capture scope trace").

## After The Agent Returns

### If PASS (all three scenarios passed)

Write the three evidence lines into `.agent/runs/powered-ready-console/Status.md` under `Validation`:

    - aux-control-pass: PASS — <transport/port, raw one-line>
    - ready-runtime-pass: PASS — <same>
    - fault-edge-pass: PASS — <same>

Then record the completion:

    .claude/hooks/mark-audit-done.py powered

### If FAIL

Do NOT record completion. Record the failure under `Validation` with the specific scenario + raw output, and create a remediation entry under `In Progress`.

### If BLOCKED (no bench)

Record under `Blockers`:

    - Powered Phase 2 validation blocked — <specific prerequisite missing>. Next session with bench must run aux-control-pass / ready-runtime-pass / fault-edge-pass.

## Do Not

- Do NOT substitute USB Phase 1 results.
- Do NOT mark powered validation done on two-of-three. `fault-edge-pass` is the most likely place a regression hides.
- Do NOT paraphrase raw script output. Capture it verbatim.
