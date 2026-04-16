# Repo-Local Skills — Index

This directory holds project-specific skills for the BSL Laser Controller. Every skill encodes hard-won, safety-critical knowledge that a generic external skill does not know about.

The authoritative operating manual is [`.agent/AGENT.md`](../AGENT.md). The task → skill map is in [`.agent/SOP.md`](../SOP.md).

## Read Before Any Change

| Skill | Must load for | Why |
|---|---|---|
| [cross-module-audit](./cross-module-audit/SKILL.md) | EVERY firmware / host / protocol change, no exceptions | **Mandatory per-change multi-agent fan-out**. Every diff that lands MUST spawn the relevant subset of B1/B2/B3 + A1/A2/A3 + hardware-safety + docs-guardian and record verdicts in Status.md before declaring the change complete. Codifies the cross-module audit policy from AGENT.md. |
| [firmware-change](./firmware-change/SKILL.md) | Any change under `components/laser_controller/src/**`, fault logic, state machine, GPIO output, rail enable, deployment supervision | Encodes GPIO6 LED dual-driver architecture, data-race threading rules, state-machine invariants, telemetry validity, and the specific pattern that caused real human injury in a prior session. |
| [gui-change](./gui-change/SKILL.md) | Any change under `host-console/src/**` | Dead-code map (domain/*, platform/*, styles.css, App.css), canonical `types.ts` surface, transport class contract, common GUI mistakes. |
| [Uncodixfy](./Uncodixfy/SKILL.md) | Any GUI / CSS / layout / component work | Mandatory design language. Bans AI-default UI patterns (glass, hero, pills, gradients, dramatic shadows) in favour of a restrained light instrument aesthetic. |

## Session Flow

| Skill | Trigger |
|---|---|
| [session-start](./session-start/SKILL.md) | Every new session. Grounds the agent, classifies the task, loads the matching skill. Or run `/session-start`. |
| [context-handoff](./context-handoff/SKILL.md) | Context tightens, a session ends mid-initiative, or work stops at a checkpoint. Or run `/handoff`. |

## Validation & Review

| Skill | Trigger |
|---|---|
| [firmware-validation](./firmware-validation/SKILL.md) | Validating firmware behavior / logic / protocol changes. Separates USB Phase 1 from Powered Phase 2. |
| [gui-validation](./gui-validation/SKILL.md) | Validating GUI changes. Requires real rendered-page inspection. |
| [powered-bench-validation](./powered-bench-validation/SKILL.md) | Any claim that deployment / runtime readiness has been proven. Codifies the three mandatory Powered Phase 2 passes. |
| [review-loop](./review-loop/SKILL.md) | Every major milestone close, PR pre-merge, or powered-ready recovery wrap-up. The 7-agent independent review. |

## Hardware, Protocol, Device

| Skill | Trigger |
|---|---|
| [hardware-safety](./hardware-safety/SKILL.md) | Schematic / pinmap / rail sequencing edits, GPIO ownership changes, ADC telemetry paths, fault gating, any change touching SBDN / PCN / PWR_TEC_EN / PWR_LD_EN. |
| [device-flashing](./device-flashing/SKILL.md) | Putting firmware on the controller. Covers the three supported paths and the recurring Wi-Fi-no-serial friction. |
| [protocol-evolution](./protocol-evolution/SKILL.md) | Adding / renaming / deprecating any JSON command. Four-place sync rule + alias discipline. |
| [calibration-provisioning](./calibration-provisioning/SKILL.md) | TEC / wavelength LUTs, DAC80502 codes, IMU transform matrices. Write-then-read-back-with-CRC rule. |

## Reference

| Skill | Notes |
|---|---|
| [gpio-config](./gpio-config/SKILL.md) | Generic GPIO / bus reference with a platform-agnostic test suite. Not BSL-specific — `firmware-change` and `hardware-safety` override for this project. |

## External Plugin Skills

[EXTERNAL.md](./EXTERNAL.md) maps globally-available plugin skills (engineering/design/data/productivity/etc.) to BSL use cases. External skills never replace BSL-local skills for safety-critical work.

## Deprecated — Do Not Use For BSL

| Path | Why it's deprecated |
|---|---|
| `espdl-operator` (if it reappears under `.agent/skills/` or at `~/.agents/skills/espdl-operator/`) | This is an ESP-DL (neural network operator) skill, not a laser-controller skill. It was removed from this repo in the workflow refactor. Ignore it if encountered — it will confuse firmware decisions. |

## Rules

- Repo-local skills (`.agent/skills/`) are checked first.
- External plugin skills are consulted only when no BSL-local skill covers the task. See [EXTERNAL.md](./EXTERNAL.md).
- Every firmware change routes through `firmware-change` first, then `hardware-safety` if GPIO / rails are involved, then `firmware-validation` before closing.
- Every GUI change routes through `gui-change` + `Uncodixfy` first, then `gui-validation` before closing.
- Every milestone closes with `review-loop` (the 7-agent independent review).
- Every context handoff writes to `Status.md` + `ExecPlan.md` per `context-handoff` — chat is not the memory.

## Adding A New Skill

Per `.agent/AGENT.md` Skills Policy:

1. Each skill must state: when it must be used, what docs must be consulted, what validations are mandatory, what must be written back into `Status.md` or `.agent/AGENT.md`.
2. Place the skill under `.agent/skills/<name>/SKILL.md` with YAML frontmatter (`name`, `description`).
3. Update this README.md with the new entry.
4. Update [`.agent/INDEX.md`](../INDEX.md) so the master navigation includes it.
5. If the skill warrants a slash command, add `.claude/commands/<name>.md` as a thin wrapper.
