# BSL Project Operating Manual

This is the single canonical operating document for this repository.

Every prompt, every agent, and every operation in this repo must treat this file as the first required read. Root `AGENT.md` and root `README.md` are only entrypoint pointers. If anything here conflicts with older docs or prior session memory, this file wins.

This project controls a life-critical surgical laser controller. Stability is the top priority. If a workflow mistake, permission conflict, stale assumption, or hidden control path could plausibly create unsafe behavior, stop and resolve it before continuing.

## Most Important Rule

Do not stop after implementation alone.

Every powered-ready recovery pass must end with:

- 3 independent UI/UX review agents
- 3 independent code/hardware review agents
- 1 independent on-device validation agent running the required powered-bench passes

If any one of those 7 review or validation passes says no, the work is not done. Restart the polish / repair / validation loop and keep going until every pass clears or an explicit written blocker is recorded.

## Canonical Rule

- Read this file first for every session.
- Then read `.agent/SOP.md` (one-page cheat sheet) and `.agent/INDEX.md` (master navigation) if this is your first time in the repo.
- If the work touches GUI, read `.agent/skills/gui-change/SKILL.md` and `.agent/skills/Uncodixfy/SKILL.md` before making any GUI decision.
- If the work touches firmware, read `.agent/skills/firmware-change/SKILL.md` before making firmware decisions. This skill encodes project-specific bugs and control-flow knowledge that prevents regressions.
- If the work is long-horizon or complex, create or enter an initiative folder under `.agent/runs/` and work from its docs, not from chat memory.
- If anything important is unclear, ask. Do not guess.
- If anything hardware-facing is in doubt, check schematics and datasheets first, then ask if still unclear.

# ExecPlans
When writing complex features or significant refactors, use an ExecPlan (as described in .agent/PLANS.md) from design to implementation.

## Mission

Use a long-horizon, documentation-first workflow that makes the repo restartable from disk, not from chat context.

Implementation is only the beginning. Every single feature must be independently tested and validated. Firmware work assumes real serial access and real hardware debugging. GUI work requires real rendered-page inspection before it is accepted. If a page cannot be rendered in the current session, the agent must obtain the needed tool, access, or verification path before making UI claims.

## Firmware-Wide Logic Audit Rule (Non-Negotiable)

Before committing ANY firmware change, run a full firmware-wide logic audit for EVERY line of code added or modified. "Firmware-wide" means tracing how that one line interacts with every other line in the codebase. Specifically:

1. **Threading**: The control task (priority 20, 5ms tick) runs on either core. Any function called from a different task (comms, main) that writes to `s_context` fields is a data race unless it uses the SAME lock the control task uses. The control task currently writes fault fields WITHOUT any lock. Do not write fault fields from another task.

2. **State machine**: For every `derive_state` return value, verify the transition from the CURRENT state is in the allowed table in `laser_controller_state.c`. Power can drop at any time — every state must be able to reach `PROGRAMMING_ONLY`.

3. **GPIO6 LED (TPS61169)**: Two control paths share `s_tof_illumination`. The service path (`set_tof_illumination`) and runtime path (`set_runtime_tof_illumination`) must never fight. The runtime path must bail out when service mode owns the sideband. The `apply_outputs` sideband call must NEVER be removed.

4. **Deployment and faults**: The deployment checklist is what enables rails. Faults that fire because rails are OFF must not block deployment entry/run. But fault clearing must go through the control task's own timing, not by directly zeroing fields from another task.

5. **Command gating**: Trace from the comms handler through the app function to the hardware output. Verify every gate (deployment.active, deployment.running, service_mode, fault_latched) is checked at the right level.

This rule exists because a previous session made incremental firmware changes without full audits, causing a data race that led to a real human injury from an unexpected LED activation.

## Cross-Module Audit Fan-Out (Non-Negotiable, 2026-04-14)

Every firmware/software/protocol change — especially anything hardware-facing — MUST be cross-validated against EVERY OTHER MODULE in the project before it is declared done. A change to one file is never an island.

The mechanism is **mandatory multi-agent fan-out per change**, codified in [`.agent/skills/cross-module-audit/SKILL.md`](./skills/cross-module-audit/SKILL.md). For every diff that touches firmware (`components/laser_controller/src/**` or `components/laser_controller/include/**`) OR host (`host-console/src/**`) OR protocol (`docs/protocol-spec.md`), the implementing agent MUST spawn (in parallel where possible) the relevant subset of:

- **B1 — `bsl-firmware-auditor`**: full firmware-wide logic audit. Required for ANY firmware change.
- **B2 — `bsl-gpio-auditor`**: GPIO ownership audit. Required for any change that touches `gpio_*`, ADC, PWM, I2C, SPI, or sideband control.
- **B3 — `bsl-protocol-auditor`**: four-place protocol sync audit. Required for any add/rename/deprecate of a JSON command, snapshot field, or response shape.
- **A1 — `bsl-uncodixfy-reviewer`**: rendered-page critique. Required for any GUI change.
- **A2 — `bsl-interaction-reviewer`**: interaction-logic + forbidden-path review. Required for any GUI change that changes a button, gate, or commit path.
- **A3 — `bsl-consistency-reviewer`**: cross-page consistency. Required when GUI shared primitives (`index.css`, `types.ts`, `panel-section`, `field`, `status-badge`, etc.) are touched.
- **`bsl-hardware-safety`**: required whenever the change references a pin, rail, ADC, datasheet, or schematic. Read-only.
- **`bsl-docs-guardian`**: required when any `.agent/` doc, `docs/*.md`, or root `CLAUDE.md` is touched.

Verdicts MUST be recorded in `.agent/runs/<active>/Status.md` under a per-change `Cross-Module Audit Evidence` block. **Any single FAIL** restarts the implement → audit loop. The work is not done until every spawned agent reports PASS or BLOCKED with explicit acceptance recorded in writing.

This is in addition to (not a replacement for) the milestone-close 7-agent review loop.

The session-start `firmware tree: DIRTY` / `GUI tree: DIRTY` banners come from `.claude/state/` sentinels that are only cleared by `mark-audit-done.py`. The mark script MUST be run after each successful audit fan-out.

## USB-Only Debug Power and the USB-Debug Mock Layer

When the controller is connected over USB-CDC for development, **USB power alone CANNOT enable the TEC or LD MPM3530 rails**. The USB host typically negotiates a programming-only PD tier (5 V at <=1.5 A), which is below the threshold required to bring up either power supply. As a hardware consequence:

- `PWR_TEC_GOOD` (GPIO16) stays LOW; `PWR_LD_PGOOD` (GPIO18) stays LOW.
- `tec_temp_good`, `tec_telemetry_valid`, `ld_telemetry_valid` all read FALSE.
- `decision.allow_nir` is unconditionally false because `power_tier != FULL`.
- The deployment checklist cannot reach READY_POSTURE because rails never assert PGOOD.

This is correct, datasheet-honest behavior — but it also means **all online testing of the deployment, ready-idle, runtime, and fault paths is impossible from a USB-only session unless we synthesize the missing telemetry**.

The repo therefore provides a **USB-Debug Mock Layer** (`components/laser_controller/src/laser_controller_usb_debug_mock.c`) with hard isolation guarantees:

1. **Explicit opt-in only.** The mock is OFF by default. It engages only when (a) service mode is active AND (b) `power_tier == programming_only` AND (c) the operator explicitly issues `service.usb_debug_mock_enable`. Auto-engage is forbidden.
2. **Hard auto-disable + LATCHED fault on real-power conflict.** If the firmware detects any PD power tier above `programming_only` while the mock is active, the mock immediately disengages AND latches the new fault `LASER_CONTROLLER_FAULT_USB_DEBUG_MOCK_PD_CONFLICT`. The fault must be cleared manually before the mock can be re-enabled. This makes accidental "mock during real bench" impossible to ignore.
3. **Auto-disable on any other fault, on service-mode exit, or on explicit `service.usb_debug_mock_disable`.**
4. **The mock NEVER drives any GPIO.** It only substitutes input readback values inside `laser_controller_board_read_inputs` AFTER the safe-default outputs have been computed. The control task remains the only writer of `s_context` fields.
5. **Closed-loop simulation.** The mock tracks the commanded TEC target and synthesizes a realistic settling profile; it mirrors commanded LD current to LIO; it asserts PGOOD with realistic delays; it follows SBDN state for `driver_loop_good`. The synthesized values are deterministic so the GUI and state machine can be exercised end-to-end.
6. **Visible status.** The snapshot exposes `bench.usbDebugMock = { active, pdConflictLatched, activatedAtMs }`. The host MUST render a clear app-wide banner whenever `active` is true, and every TEC/LD telemetry display MUST visually flag itself as synthesized.
7. **No effect on normal function.** When `active` is false (the default), the mock module is a pure no-op — no branches taken, no static state mutated. The cost in the safe path is a single `if (!active)` early-return.

The mock is for online testing and debugging only. No claim of `powered-ready` may cite mock evidence; only Powered Phase 2 with the TEC and LD rails physically up counts.

1. Stability is the most crucial property of this project. A bad workflow or hidden conflict can become a safety defect.
2. Every new feature, SoP, process, or control must be checked for:
   - permission conflicts
   - ownership conflicts
   - GPIO conflicts
   - stale-state conflicts
   - validation blind spots
3. Finishing implementation is only the beginning. A feature is not done until it is independently validated.
4. For firmware, validation must assume the agent always has serial access to the device and can use real hardware debugging.
5. For GUI, validation must include real rendered pages, interaction checks, and UI/UX inspection.
6. If rendered visibility is missing, do not guess. Solve the visibility problem first.
7. If anything is unclear, ask. Enter planning mode whenever needed.
8. If anything is in doubt, check schematic and datasheet first, then ask if still unsure.

## Long-Horizon Operating Model

This workflow adopts the strongest useful ideas from OpenAI’s long-horizon Codex guidance:

- durable project memory
- explicit spec file
- checkpointed milestones
- a runbook for execution
- continuous verification
- a live status and audit log
- externalized state so a fresh agent can continue without hidden memory

Reference:
- [Run long horizon tasks with Codex](https://developers.openai.com/blog/run-long-horizon-tasks-with-codex)

The required loop for major work is:

1. Plan
2. Implement
3. Run tools and validations
4. Observe the results
5. Repair failures immediately
6. Update docs and status
7. Repeat

Do not skip the validation-and-update half of the loop.

## `.agent` Layout

The canonical repo-local workflow layout is:

- `.agent/AGENT.md`
  Canonical operating document (this file)
- `.agent/SOP.md`
  One-page cheat sheet: task → skill map, audit rules, validation rules, slash commands
- `.agent/INDEX.md`
  Master navigation index covering every agentic resource in the repo
- `.agent/PLANS.md`
  Canonical ExecPlan instructions
- `.agent/skills/`
  Repo-local project skills only. See `.agent/skills/README.md` for the index and `.agent/skills/EXTERNAL.md` for external plugin skill mapping.
- `.agent/runs/<initiative>/`
  Durable long-horizon working memory for each major initiative

The harness layer is wired under `.claude/`:

- `.claude/settings.json`
  SessionStart hook, permissions, environment variables, statusline
- `.claude/commands/*.md`
  Slash-command definitions — thin wrappers over the repo-local skills above

Every major initiative should have:

- `Prompt.md`
  Frozen target, goals, non-goals, constraints, deliverables, and done-when
- `ExecPlan.md`
  Milestone plan maintained according to `.agent/PLANS.md`
- `Implement.md`
  Execution runbook for the agent
- `Status.md`
  Live progress, blockers, validation state, decisions, discoveries, and next steps

## Start Of Every Session

Before doing real work:

1. Read `.agent/AGENT.md`.
2. Read `.agent/SOP.md` (one-page cheat sheet). It has the task → skill map and the non-negotiable audit / validation rules.
3. Read `.agent/runs/powered-ready-console/Status.md` (the active initiative). If the task belongs to a different initiative, read that one's `Prompt.md`, `ExecPlan.md`, `Implement.md`, and `Status.md` instead.
4. If the task touches GUI, read `.agent/skills/gui-change/SKILL.md` and `.agent/skills/Uncodixfy/SKILL.md`.
5. If the task touches firmware, read `.agent/skills/firmware-change/SKILL.md`. This is the highest-priority firmware skill.
6. Ground yourself in the repo before asking questions.
7. Resolve discoverable facts from code, docs, schematics, and datasheets first.
8. Ask only when the remaining ambiguity materially changes the work.

Or run `/session-start` to automate the ritual.

## Skills Policy

Only project-specific skills live under `.agent/skills/`.

External skills may also exist under `~/.agents/skills/`. Treat that directory as the approved external skill source for this project when a needed skill is not repo-local.

Required initial skills:

- `.agent/skills/firmware-change/SKILL.md`
  **Read before any firmware change.** Encodes hard-won debugging knowledge: GPIO6 dual-driver bug, deployment fault-latch circular dependency, telemetry validity rules, state transition gaps.
- `.agent/skills/gui-change/SKILL.md`
  **Read before any GUI change.** Dead code map, correct type system, transport API shape.
- `.agent/skills/Uncodixfy/SKILL.md`
  GUI design language: ban AI-UI patterns, restrained instrument aesthetic.
- `.agent/skills/hardware-safety/SKILL.md`
  Required for hardware-facing design or implementation.
- `.agent/skills/firmware-validation/SKILL.md`
  Required for firmware validation on real hardware.
- `.agent/skills/gui-validation/SKILL.md`
  Required for GUI rendered-page validation.
- `.agent/skills/context-handoff/SKILL.md`
  Required when context is tight or a session is ending mid-initiative.
- `.agent/skills/review-loop/SKILL.md`
  Required for independent milestone review.
- `.agent/skills/gpio-config/SKILL.md`
  Generic GPIO reference for pin constraints and bus protocols.

New project-specific skills (added with the workflow refactor):

- `.agent/skills/session-start/SKILL.md`
  Mandatory session-start ritual. Grounds the agent in AGENT.md and the active Status.md, classifies the task, loads the matching change-type skill.
- `.agent/skills/device-flashing/SKILL.md`
  All three supported flashing paths — serial wrapper, `idf.py`, Web Serial. Documents the Wi-Fi-only-no-serial friction.
- `.agent/skills/powered-bench-validation/SKILL.md`
  Codifies USB Phase 1 vs Powered Phase 2 and the three mandatory powered passes.
- `.agent/skills/protocol-evolution/SKILL.md`
  Four-place sync rule for any protocol add/rename/deprecate.
- `.agent/skills/calibration-provisioning/SKILL.md`
  Write-then-read-back-with-CRC rule for LUTs, DAC codes, and IMU transform.

Rules:

- For any GUI-related work, consult `gui-change` and `Uncodixfy` first.
- For any firmware-related work, consult `firmware-change` first. This skill encodes project-specific debugging knowledge that overrides generic GPIO advice.
- Check repo-local skills in `.agent/skills/` first, then check external skills in `~/.agents/skills/` when needed.
- Do not mirror all global Codex/plugin skills into this repo.
- Each repo skill must say:
  - when it must be used
  - what docs/files must be consulted
  - what validations are mandatory
  - what must be written back into `Status.md` or `.agent/AGENT.md`
- Every firmware logic change requires both:
  - a full firmware logic audit covering state-machine effects, output ownership, rail behavior, GPIO ownership, PD ownership, and fault/invalidation behavior
  - a full GUI logic audit covering operator-visible control paths, disabled-state reasons, page ownership boundaries, and any UI path that could trigger forbidden hardware communication

## Validation Requirements

Implementation is never enough by itself.

Every feature must be independently validated.

Firmware validation rules:

- assume serial access is available
- use real hardware debugging when applicable
- validate actual logic, not only compilation
- write the observed result back into the initiative `Status.md`

GUI validation rules:

- inspect real rendered pages
- test the actual interaction logic, not just static markup
- if render access is missing, obtain it first
- verify every corner, not just the happy path

Validation must be milestone-based. Each milestone needs:

- explicit acceptance criteria
- exact validation commands or UI checks
- a stop-and-fix rule if validation fails

## Review Loop

After each major milestone:

- run a separate review/evaluation pass
- use another dedicated agent or a fresh review-only session for that pass
- keep the review objective and explicitly independent from the implementation pass

Review must cover:

- safety and permission conflicts
- firmware logic stability
- GUI interaction logic
- rendered page quality
- validation completeness

If the review finds meaningful issues:

- restart the implementation and validation cycle
- do not treat the review as a formality
- do not close the work until the issues are addressed or explicitly deferred in writing

## Context Exhaustion And Handoff

If context is getting tight:

- do not panic
- do not rush to summarize and stop
- do not pretend the work is more complete than it is

Instead:

1. Update the initiative `Status.md` with:
   - what is done
   - what is validated
   - what is still broken
   - blockers
   - exact next steps
   - missing implementation details
2. Update the initiative `ExecPlan.md`:
   - `Progress`
   - `Decision Log`
   - `Surprises & Discoveries`
   - `Outcomes & Retrospective` if appropriate
3. Update `.agent/AGENT.md` if repo-level truth changed.
4. Start the next fresh agent/session from the docs.

The handoff docs are the memory. Hidden context is not.

## Source Of Truth For Hardware And Safety

If anything hardware-facing is uncertain, check these first:

- [docs/hardware-recon.md](docs/hardware-recon.md)
- [docs/firmware-pinmap.md](docs/firmware-pinmap.md)
- [docs/datasheet-programming-notes.md](docs/datasheet-programming-notes.md)
- [docs/validation-plan.md](docs/validation-plan.md)
- the PDFs in [docs/Datasheets](docs/Datasheets)
- the PDFs and netlists in [docs/Schematics](docs/Schematics)

## Future Implementation Requirements To Preserve

The active host rewrite lives under `.agent/runs/powered-ready-console/`. The superseded USB-only rewrite seed has been archived to `.agent/runs/_archive/v2-rewrite-seed/` for historical context only.

Future rewrite work must preserve these tracked product requirements:

- the runtime workspace is `Operate`, with an inline pre-enable checklist instead of a standalone Deployment page
- deployment mode remains firmware-owned, but its user-facing controls live in the operator runtime workspace instead of a separate page
- checklist success must mean the laser can actually turn on once the required hardware is available
- TEC power must come on before LD power
- if TEC power is lost, LD power must stop immediately and `PCN` / `SBDN` must be pulled low
- TEC and LD telemetry must only be treated as valid when their power-good and control-state prerequisites are satisfied
- full GPIO permission and compatibility issues must be reworked as a core stability protocol
- during deployment-mode-owned monitoring, bring-up submodules should not look disconnected; disabled write controls must explain why
- monitored modules with deterministic readback must enter an error state if readback disappears for too long
- runtime control is split into:
  - `binary_trigger`
    Normal ON/OFF mode intended for the physical stage-1 / stage-2 trigger path only
  - `modulated_host`
    Host/runtime-controlled mode and the only mode allowed to use host-issued output enable and `PCN` modulation
- only one runtime mode may be active at a time
- `binary_trigger` is now source-backed as of 2026-04-15 (MCP23017 button-board driver in `components/laser_controller/src/laser_controller_buttons.c`); the firmware unblocks the mode dynamically when `buttonBoard.mcpReachable == true`. Switching to `binary_trigger` while the MCP23017 is unreachable is rejected at the comms dispatcher with an explicit reason. The host pre-disables the dropdown with the same reason in the tooltip.
- powered-ready validation must use a real powered bench and must not claim success from a USB-only safe-fail path

## Repo Orientation

Current major code areas:

- `components/laser_controller/`
  Current firmware
- `host-console/`
  Current browser GUI
- `docs/`
  Hardware, protocol, architecture, and validation references
- `.agent/`
  Canonical workflow, skills, plans, and run memory

## Update Rules

When changing workflow or SoP:

1. Update `.agent/AGENT.md` first.
2. Keep root `AGENT.md`, root `README.md`, and root `CLAUDE.md` as thin pointers that reference `.agent/AGENT.md` + `.agent/SOP.md` + `.agent/INDEX.md`.
3. Keep `.agent/SOP.md` current when the task → skill map, audit rules, or slash command list changes.
4. Keep `.agent/INDEX.md` current when new skills, initiatives, or commands are added.
5. Keep the repo-local skill list (`.agent/skills/README.md`) current.
6. Keep external skill mapping (`.agent/skills/EXTERNAL.md`) current when a new plugin skill becomes relevant or an existing one shifts scope.
7. Keep initiative docs self-contained enough for a fresh agent to resume from them alone.
8. If a new workflow rule affects all future work, put it here, not only in chat.
9. Harness-level changes (hooks, permissions, env vars, statusline) go in `.claude/settings.json`. Each slash command under `.claude/commands/` must remain a thin wrapper that invokes a skill — the skill is where the knowledge lives.

## GPIO Ownership Protocol

GPIO stability is a first-class safety requirement on this board.

Every firmware change that touches GPIO, ADC, PWM, I2C, SPI, or sideband control must satisfy these rules:

- each GPIO has exactly one active owner at a time
- baseline firmware ownership, service override ownership, PWM ownership, and bus-recovery ownership must be explicit
- ownership handoff must restore the pin to its baseline safe mode before another owner takes control
- no hidden pull-up, pull-down, open-drain, peripheral-matrix, or direction changes are allowed outside the owning path
- ADC telemetry on safety-facing analog nets must never be read as valid unless the related powered hardware path is actually on
- shared-bus recovery must restore `GPIO4/GPIO5` to the intended bus posture after recovery
- `PCN`, `SBDN`, `PWR_TEC_EN`, and `PWR_LD_EN` must be treated as scope-worthy safety pins during validation

### GPIO13 / LD_SBDN three-state (2026-04-14)

GPIO13 drives the ATLS6A214 SBDN pin, which is a **three-state input** per datasheet Table 1 (`docs/Datasheets/ATLS6A214D-3.pdf` p.2-5):

- **OFF** (drive LOW, 0..0.4 V) — shutdown; fast 20 us beam-off; idle current <2 uA.
- **STANDBY** (Hi-Z, 2.1..2.4 V from external R27/R28 divider at ~2.25 V) — driver idle, 8 mA, 20 ms re-entry to operate.
- **ON** (drive HIGH, 2.6..14 V) — driver operate.

Firmware invariants:

- `laser_controller_sbdn_state_t` in `components/laser_controller/include/laser_controller_board.h` is the canonical three-state enum. Zero = OFF (so any zero-init path lands in the safe posture).
- Apply-path in `laser_controller_board_drive_safe_gpio_levels` switches between `GPIO_MODE_INPUT_OUTPUT` + level (OFF or ON) and `GPIO_MODE_INPUT` (STANDBY).
- Pulls on GPIO13 MUST stay disabled. Enabling internal pulls would fight the R27/R28 divider and shift Hi-Z out of the standby band.
- **Fault paths force OFF, never STANDBY.** Drive-LOW is the datasheet 20 us fast-shutdown path. STANDBY is only used in the deliberate ready-idle-no-NIR path.
- `driver_operate_expected` is `sbdn_state == ON && !select_driver_low_current`. STANDBY does not count as "operate expected" — the `FAULT_UNEXPECTED_CURRENT` heuristic would fire incorrectly if it did.

### Green alignment laser — ungated at the software level (2026-04-14)

Per explicit user directive, the green alignment laser has **no software interlock**:

- `safety.c`: `decision->allow_alignment = true` unconditional.
- `comms.c`: `operate.set_alignment` / `enable_alignment` / `disable_alignment` no longer pass through the aux-control deployment gate. Only `operate.set_led` retains aux gating.
- `app.c derive_outputs`: `enable_alignment_laser` follows `decision->alignment_output_enable` on every path after `boot_complete`.
- The residual GPIO37 shared-net hazard (ERM_TRIG + GN_LD_EN) is bounded by existing `enable_haptic_driver` service-mode gating at app.c. Outside service mode, `ERM_EN` is LOW → DRV2605 is quiescent → toggling GPIO37 for green has no haptic side-effect.

Observed facts and enforcement changes related to GPIO ownership must be written back here when they become repo-level truth.
