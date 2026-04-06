# Repo-Local Skills

This directory holds project-specific skills only.

Approved external skill path for this project:

- `~/.agents/skills/`

Current skills:

- [Uncodixfy](./Uncodixfy/SKILL.md)
  Mandatory first read for GUI-related work.
- [gpio-config](./gpio-config/SKILL.md)
  Mandatory firmware-reference skill for GPIO, buses, pin constraints, and electrical compatibility.
- [espdl-operator](./espdl-operator/SKILL.md)
  Mandatory firmware-reference skill with higher priority than `gpio-config` if the two disagree.
- [hardware-safety](./hardware-safety/SKILL.md)
  Required for hardware-facing design or implementation.
- [firmware-validation](./firmware-validation/SKILL.md)
  Required for firmware validation planning and execution.
- [gui-validation](./gui-validation/SKILL.md)
  Required for GUI validation and rendered-page inspection.
- [context-handoff](./context-handoff/SKILL.md)
  Required when context is tight or a session is ending mid-initiative.
- [review-loop](./review-loop/SKILL.md)
  Required for independent milestone review/evaluation.

Rules:

- Read `.agent/AGENT.md` first.
- If GUI-related, read `Uncodixfy` first.
- If firmware-related, read `gpio-config` and `espdl-operator` first. If they conflict, `espdl-operator` wins.
- Check `.agent/skills/` first, then `~/.agents/skills/` for approved external skills when a needed skill is not repo-local.
- Keep skills project-specific. Do not mirror global Codex/plugin skills into this repo.
