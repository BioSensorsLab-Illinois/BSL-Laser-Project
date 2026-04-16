---
name: gui-validation
description: Required for GUI validation. Does NOT accept GUI claims without real rendered-page inspection. Depends on Uncodixfy for design critique. Trigger on validating host-console changes, reviewing rendered pages, checking disabled-state logic, OFF/INVALID/IN-DEPLOYMENT telemetry rendering, interaction flows, cross-page consistency. If render visibility is missing, obtain the tool first instead of guessing. Capture screenshots and write evidence to Status.md.
---

# gui-validation

Use this skill for any GUI-related validation.

Mandatory first dependency:

- read `.agent/skills/Uncodixfy/SKILL.md` before making or judging GUI changes

Validation rules:

- do not accept GUI changes without real rendered-page inspection
- if render visibility is missing, obtain the tool/access path first instead of guessing
- validate:
  - page rendering
  - spacing and readability
  - disabled-state logic
  - hover help
  - interaction flow
  - cross-page consistency
  - explicit OFF / INVALID / IN-DEPLOYMENT state rendering for hardware-backed telemetry
  - Control-page explanation of why the inline deployment checklist cannot complete on the USB-only Phase 1 bench

Required evidence:

- screenshots or equivalent rendered-page proof
- interaction results written into the active `Status.md`
