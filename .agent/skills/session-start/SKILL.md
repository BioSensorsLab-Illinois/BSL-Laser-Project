---
name: session-start
description: Mandatory session-start ritual for EVERY new session or task in this repo. Grounds the agent in AGENT.md, the active initiative's Status.md, and loads the correct change-type skill BEFORE proposing any changes. Trigger on any non-trivial first request, "start work", "begin", "new session", "what's going on", or when the agent has no prior conversation context. Skip only for pure documentation questions that require no repo state.
---

# session-start

This is the first skill to run in any new session before touching real work. It exists because skipping context grounding is the single most common cause of regressions in this safety-critical repo.

## Required Reads (in this order)

1. **`.agent/AGENT.md`** — the canonical operating document. Overrides everything else.
2. **`.agent/SOP.md`** — one-page cheat sheet (session-start, change-type, validation, review-loop, handoff).
3. **`.agent/runs/powered-ready-console/Status.md`** — live state of the active initiative. Always read this before assuming anything about current work.

If the task clearly belongs to a different initiative, also read that initiative's `Prompt.md`, `ExecPlan.md`, and `Status.md`.

## Classify the Task

Before writing any code or proposing changes, place the task into exactly one of these buckets (or "none" for pure reading/research):

| Bucket | Trigger Signals | Load These Skills |
|---|---|---|
| **firmware** | Any change under `components/laser_controller/src/**`, fault logic, state machine, GPIO output, rail enable, deployment supervision, ADC telemetry validity | `firmware-change` (mandatory) + `hardware-safety` if GPIO or rail ownership changes + `firmware-validation` before declaring done |
| **GUI** | Any change under `host-console/src/**`, React components, transport, CSS/styling, workspace layout | `gui-change` (mandatory) + `Uncodixfy` (mandatory) + `gui-validation` before declaring done |
| **hardware** | Schematic edits, pinmap edits, board-level design, power topology decisions, new IC selection | `hardware-safety` (mandatory) + consult `docs/hardware-recon.md`, `docs/firmware-pinmap.md`, `docs/datasheet-programming-notes.md` |
| **protocol** | New or changed JSON command, new command family, contract rename, deprecation | `protocol-evolution` (mandatory) + `firmware-change` + `gui-change` (both — protocol touches both sides) |
| **calibration** | TEC LUT edits, wavelength LUT edits, DAC codes, IMU transform matrix, calibration storage/retrieval | `calibration-provisioning` (mandatory) + `firmware-change` |
| **flashing** | Need to put firmware on the device (serial or Wi-Fi discussion) | `device-flashing` |
| **validation** | Running scenarios, bench tests, scope captures, powered-ready recovery wrap-up | `powered-bench-validation` + `review-loop` at close |
| **handoff** | Ending a session mid-initiative, context compaction, preparing for a fresh agent | `context-handoff` |
| **docs-only** | Pure documentation edits with no code impact | none — but still read AGENT.md first |

## Decline To Guess

If, after reading AGENT.md + the active Status.md, the task is still unclear (for example, which initiative it belongs to or which change-type bucket), **ask**. Do not guess. The user has explicitly requested this in `feedback_workflow.md`: "If anything is unclear, ask. Do not guess."

## After Context Is Loaded

Only now propose an approach. Reference file paths by full repository-relative path (never shorthand like "the deployment file"). Cite specific lines where the project already has the answer. Use `EnterPlanMode` before any non-trivial multi-file change.

## Output (in the first assistant turn)

Briefly state:

- what active initiative is in flight
- what bucket the current task falls into
- which skills have been loaded
- any ambiguity that still blocks progress (raise now, not later)

Keep it under ~8 lines. The user does not need a prose re-read of AGENT.md — they need confirmation you actually read it.
