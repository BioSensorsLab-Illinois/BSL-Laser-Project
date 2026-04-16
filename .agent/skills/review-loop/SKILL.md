---
name: review-loop
description: Required after ANY major milestone or before claiming a workstream complete. Codifies the mandatory 7-agent review — 3 independent UI/UX reviewers + 3 independent code/hardware reviewers + 1 on-device validation agent running the three Powered Phase 2 passes. Any NO from any agent restarts the loop. Trigger on milestone close, PR pre-merge, powered-ready recovery wrap-up, or "is this done" questions.
---

# review-loop

**Every powered-ready recovery pass must end with 7 independent review passes. If any one returns NO, the work is NOT done — restart the polish/repair/validation loop until every pass clears OR an explicit written blocker is recorded in `Status.md`.** (Source: `.agent/AGENT.md` "Most Important Rule" section.)

## When To Run The 7-Agent Loop

- After every major milestone in the active initiative (`.agent/runs/<initiative>/`).
- Before claiming any firmware, GUI, protocol, or calibration workstream "complete."
- Before declaring a powered-ready recovery pass finished.
- Before any PR that touches safety-critical code (firmware, deployment, fault handling, GPIO ownership).

Do NOT run it for trivial doc-only edits or internal scaffolding that has no operator-visible effect.

## The 7 Reviewers

Each agent must be spawned as an **independent** `Agent` tool call (preferably `subagent_type: "Explore"` for read-only reviews). Independent = the reviewer does not see the implementation agent's chain of thought; it reviews cold against the current working tree. Run all seven in parallel when possible.

### Group A — UI/UX Reviews (3 agents)

Goal: catch regressions in rendered output, interaction logic, and design-language compliance.

**Agent A1 — Rendered-page critique against Uncodixfy.** Prompt template:

> Review the rendered host console page at `<path to screenshot>` (or by running `cd host-console && npm run dev` and capturing). Audit against `/Users/zz4/BSL/BSL-Laser/.agent/skills/Uncodixfy/SKILL.md`. List every banned-pattern violation you find (glass surfaces, gradient text, oversized radii, hero sections, pill overload, ornamental labels, etc.) with its specific selector or component file. Return a pass/fail verdict and the concrete violation list.

**Agent A2 — Interaction logic review.** Prompt template:

> Inspect `host-console/src/` for the workspace affected by this change (System / Operate / Integrate / Update / History). Trace the user-visible flow from button click through `sendCommand` to the transport and back. Verify: disabled-state reasons are explicit, hover help is present where disabled, deployment-active lockouts are visible, OFF/INVALID/IN-DEPLOYMENT telemetry renders correctly. Return pass/fail plus the specific interaction gaps.

**Agent A3 — Cross-page consistency review.** Prompt template:

> Compare this change's rendered workspace against the sibling workspaces (System, Operate, Integrate, Update, History) for consistency: sidebar nav behavior, typography scale, spacing scale (4/8/12/16/24/32), color usage, button styles, form control styles. Flag any drift. Return pass/fail plus the specific inconsistencies.

### Group B — Code/Hardware Reviews (3 agents)

Goal: catch firmware logic, GPIO ownership, and safety-invariant regressions.

**Agent B1 — Firmware logic audit.** Prompt template:

> Perform the full firmware-wide logic audit per `/Users/zz4/BSL/BSL-Laser/.agent/skills/firmware-change/SKILL.md`. For every line changed in `components/laser_controller/src/*.c` in the current diff: (a) identify every reader and writer of every field touched, (b) identify which task/core each reader/writer runs on, (c) verify locking consistency (same lock for all writers), (d) verify state-machine transitions are in the allowed table in `laser_controller_state.c`, (e) trace every path from the change to any GPIO output. Flag any data race, unguarded state transition, or broken GPIO-ownership handoff. Return pass/fail plus concrete findings with file:line citations.

**Agent B2 — GPIO / safety-critical net audit.** Prompt template:

> Verify the GPIO Ownership Protocol in `.agent/AGENT.md` is honored by this change. For any pin listed in `docs/firmware-pinmap.md` or the Safety-Critical Pin table in memory (GPIO6, GPIO13 SBDN, GPIO15 TEC_EN, GPIO17 LD_EN, GPIO21 PCN, GPIO37 hazardous shared net): does the change (a) preserve single-owner, (b) restore baseline safe mode on handoff, (c) keep ADC telemetry gated on the powered-rail prerequisite? Specifically confirm `apply_tof_sideband_state` is still called from `apply_outputs()` (the GPIO6 lesson). Return pass/fail with citations.

**Agent B3 — Protocol / host-contract audit.** Prompt template:

> For the current diff, verify that any protocol surface change is reflected in all four places: `components/laser_controller/src/laser_controller_comms.c` (dispatcher), `host-console/src/types.ts` (type surface), `host-console/src/hooks/use-device-session.ts` (parser/handler), and `host-console/scripts/live_controller_validation.py` (validation scenarios). Confirm legacy command names still work as aliases during migration. Return pass/fail plus the files where the contract is out of sync.

### Group C — On-Device Validation (1 agent)

Goal: compile-only and USB-only passes are not enough; the three Powered Phase 2 passes must actually run on a powered bench.

**Agent C1 — Powered bench validation.** Prompt template:

> Load `/Users/zz4/BSL/BSL-Laser/.agent/skills/powered-bench-validation/SKILL.md`. Run the three mandatory Powered Phase 2 passes in order on the live hardware:
>
>   1. `aux-control-pass`
>   2. `ready-runtime-pass`
>   3. `fault-edge-pass`
>
> For each, capture the exact command used, the observed result (pass/fail + raw output), and scope-equivalent evidence for `SBDN`, `PCN`, `PWR_TEC_EN`, `PWR_LD_EN` where applicable. Return a consolidated pass/fail matrix. If the powered bench is unavailable in this session, return `BLOCKED` with the specific hardware prerequisite missing — do NOT substitute USB Phase 1 results.

## Decision Gate

- **All 7 → PASS**: Work can close. Update `Status.md` with the seven evidence entries and close the milestone.
- **Any agent → FAIL**: Do NOT close. Restart the implementation/validation loop with the specific issues. Repeat until all 7 pass.
- **Agent C1 → BLOCKED (hardware unavailable)**: Record the blocker explicitly in `Status.md` under `Blockers`. Work stays `In Progress` until the powered bench validates it. Compile + USB Phase 1 is NOT a substitute.

## Outputs (Write Into Active `Status.md`)

Under a `Review Loop Evidence` subsection, record exactly:

    - Agent A1 (Uncodixfy): PASS | FAIL — <one-line summary + evidence path>
    - Agent A2 (Interaction): PASS | FAIL — <one-line summary>
    - Agent A3 (Consistency): PASS | FAIL — <one-line summary>
    - Agent B1 (Firmware logic): PASS | FAIL — <one-line summary + file:line>
    - Agent B2 (GPIO ownership): PASS | FAIL — <one-line summary>
    - Agent B3 (Protocol contract): PASS | FAIL — <one-line summary>
    - Agent C1 (Powered bench): PASS | FAIL | BLOCKED — <bench evidence or blocker>

## Rules

- Keep every review **independent** from the implementation pass — a fresh agent, read-only context, no access to the implementation agent's notes.
- Do NOT treat the review as a formality. If a reviewer says NO, restart.
- Do NOT close work just because the code compiles or the host builds.
- Phase 1 USB results can never satisfy Agent C1.
- The 7-agent loop is mandatory, not advisory — AGENT.md "Most Important Rule".
