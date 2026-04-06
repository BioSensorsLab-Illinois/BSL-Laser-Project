# V2 Rewrite Seed Prompt

## Purpose

This run folder is the seed durable-memory stack for the future firmware and GUI rewrite. It exists so the rewrite can start from written intent and continue across multiple agents or sessions without depending on hidden chat context.

## Goals

- establish a stable firmware v2 and GUI v2 rewrite initiative
- preserve safety-first behavior while simplifying ownership and workflow
- replace ad hoc session memory with durable repo-local run documents

## Non-Goals

- no firmware or GUI implementation in this seed by itself
- no protocol rewrite in this seed by itself
- no cutover in this seed by itself

## Hard Constraints

- stability first
- ask rather than guess on high-impact ambiguity
- firmware validation assumes real serial access
- GUI validation requires rendered-page inspection
- every major task must use an ExecPlan
- GUI work must consult `.agent/skills/Uncodixfy/SKILL.md` first

## Deliverables

- a maintained `ExecPlan.md`
- an execution runbook in `Implement.md`
- a living audit/status record in `Status.md`

## Done When

- another agent can start the rewrite using only the files in this run folder plus `.agent/AGENT.md`
- the next step and current assumptions are explicit
