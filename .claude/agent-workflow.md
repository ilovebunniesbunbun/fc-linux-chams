# Agent Workflow

## Overview

This project uses a hierarchical multi-agent architecture for AI-assisted refactoring. All agent orchestration artifacts live in `.agents/`.

## Roles

| Role | Responsibility |
|------|---------------|
| **Sentinel** | Top-level monitor; spawns orchestrator, audits victory, makes no technical decisions |
| **Orchestrator** | Decomposes work into milestones, dispatches sub-orchestrators, tracks progress |
| **Explorer** | Read-only codebase investigator; produces analysis documents |
| **Worker** | Implements changes (CMake, test harness, code) |
| **Reviewer** | Reviews worker output for correctness, completeness |
| **Auditor** | Forensic integrity verification; checks for hardcoded values, mock logic, facades |

## Workflow Pattern

```
Explorer → Worker → Reviewer → Auditor (gate) → next milestone
```

### Escalation Ladder
On failure: Retry → Replace → Skip → Redistribute → Redesign → Escalate

### Self-Succession
Orchestrators self-succeed after 16 spawns, write `handoff.md`, spawn successor.

## File Conventions

Each subdirectory under `.agents/` contains:
- `BRIEFING.md` — Agent identity, mission, constraints, team roster
- `original_prompt.md` — Exact user request copy
- `progress.md` — Milestone tracker with completion status
- `handoff.md` — Written before succession; summarizes delivered work
- `SCOPE.md` — Scope document for sub-orchestrators

## Constraints

- Sentinels NEVER make technical decisions
- Workers write/modify source code; orchestrators do not
- Build/test commands are run by workers, not orchestrators
- Integrity mode: `development` — no cheating, hardcoding, or facades
- All milestones require a CLEAN forensic auditor verdict
