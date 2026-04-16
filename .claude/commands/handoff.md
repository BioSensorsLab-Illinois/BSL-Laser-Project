---
description: Update Status.md / ExecPlan.md / AGENT.md so a fresh agent can resume from disk alone. Use when context tightens, a session is ending mid-initiative, or work must stop at a meaningful checkpoint. The docs are the memory.
---

Load `.agent/skills/context-handoff/SKILL.md`.

## Step 1 — Survey The Session Changelog

Read `.claude/state/session-changelog.txt` if it exists — it lists every file edited this session, grouped by firmware / gui / docs / agent. Use it to ensure your Status.md update covers every change.

## Step 2 — Update Active Initiative's Status.md

Edit `.agent/runs/powered-ready-console/Status.md`:

- **Current State** — one paragraph truth
- **Completed** — append to the list; do not overwrite prior history
- **In Progress** — what is open mid-flight, with file paths of partial edits
- **Next Step** — the very next concrete action a fresh agent should take (filenames, commands, expected observations)
- **Blockers** — every reason work cannot proceed, with specifics
- **Validation** — every command you actually ran, with pass/fail and output paths

## Step 3 — Update ExecPlan.md

Edit `.agent/runs/<active>/ExecPlan.md`. Keep per `.agent/PLANS.md`:

- **Progress** — mark completed boxes with timestamps; split partial tasks
- **Surprises & Discoveries** — unexpected behaviors with concise evidence
- **Decision Log** — every decision this session (Decision / Rationale / Date/Author)
- **Outcomes & Retrospective** — update at major milestone close

## Step 4 — Update `.agent/AGENT.md` ONLY If Repo-Level Truth Changed

Only if:
- A new safety rule was discovered and must bind future sessions
- A new mandatory skill / sub-agent was added
- The active initiative changed (new folder under `.agent/runs/`)
- A GPIO ownership fact became repo-level truth

Do NOT dump session-level detail into AGENT.md. That goes in Status.md.

## Step 5 — Check Dirty Sentinels

If `.claude/state/firmware.dirty` exists but `.claude/state/last-firmware-audit` is older, write under `In Progress`:

    - firmware tree DIRTY; /audit-firmware has not been run since last edit — next session must run it first

Same for GUI.

## Step 6 — Terse Summary Back To User

In chat, ≤ 12 lines:

- What landed
- What is still open
- Exact next command / file a fresh agent should hit
- Any blockers with specifics

## Do Not

- Do NOT truncate Status.md history. Append; split partial items.
- Do NOT claim more complete than it is. Underclaim, don't overclaim.
- Do NOT skip the Validation section.
- Do NOT rely on chat carry-over. The docs are the memory.
