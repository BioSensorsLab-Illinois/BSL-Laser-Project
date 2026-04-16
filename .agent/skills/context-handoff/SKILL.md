---
name: context-handoff
description: Required when context is tight or a session is ending mid-initiative. Updates Status.md + ExecPlan.md + AGENT.md so a fresh agent can resume from disk alone, without chat memory. Trigger on session-end signals, context compaction, approaching context limits, handoff requests, or mid-initiative stopping points. The docs are the memory — never panic-summarize into chat.
---

# context-handoff

Use this skill when context is getting tight or the session must hand work to a fresh agent.

Required steps:

1. Update the active `Status.md` with:
   - current progress
   - validation state
   - blockers
   - exact next steps
   - unfinished implementation details
2. Update the active `ExecPlan.md`:
   - `Progress`
   - `Decision Log`
   - `Surprises & Discoveries`
   - `Outcomes & Retrospective` if needed
3. Update `.agent/AGENT.md` if repo-level truth changed.
4. Hand off from those files, not from chat memory.

Never panic-wrap-up. The docs are the memory.
