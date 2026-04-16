---
description: Run the full firmware-wide logic audit on the current diff. Delegates to the bsl-firmware-auditor sub-agent, plus bsl-gpio-auditor if pins are touched. Records completion via mark-audit-done.py.
---

## Step 1 — Preflight

Show the current working-tree diff scope:

    git diff --stat components/laser_controller/src/
    git diff components/laser_controller/src/ | head -200

## Step 2 — Spawn The Firmware Auditor (sub-agent)

Use the `Agent` tool with `subagent_type: "bsl-firmware-auditor"`. Give it:

- The commit range or note "uncommitted working tree"
- The specific files changed
- Any relevant initiative context

The agent reviews cold per its definition at `.claude/agents/bsl-firmware-auditor.md`.

## Step 3 — If Any GPIO Pin Was Touched, Also Spawn bsl-gpio-auditor

Check the diff for references to any of these pins: GPIO6, GPIO13 (SBDN), GPIO15 (TEC_EN), GPIO17 (LD_EN), GPIO21 (PCN), GPIO37, GPIO4/5 (I2C), GPIO16/18 (PGOOD), ADC1 inputs (GPIO1/2/8/9/10). Any hit → spawn `bsl-gpio-auditor` in parallel with B1.

## Step 4 — If A Protocol Command Was Touched, Also Spawn bsl-protocol-auditor

If the diff touches `laser_controller_comms.c` or changes any `cmd` string, spawn `bsl-protocol-auditor`.

## Step 5 — Consolidate And Record

Consolidate the results into a single PASS/FAIL report. If PASS, run:

    .claude/hooks/mark-audit-done.py firmware

to clear the `firmware.dirty` sentinel and stamp the last-audit timestamp.

## Step 6 — Update Status.md

Regardless of PASS/FAIL, write a short entry under `Validation → Firmware Audit` in `.agent/runs/powered-ready-console/Status.md` with the verdict and any findings.

## Do Not

- Do NOT close a milestone on B1 alone — firmware audit is one of seven reviewers. Use `/review-loop` for full milestone close.
- Do NOT mark the audit done if FAIL. Fix first, re-run.
- Do NOT edit the diff while the auditor is running. Let it review the current state cold.
