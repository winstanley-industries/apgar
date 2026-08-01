# APGAR Epics

Epics track delivery: why a body of work is active now, which pull-request-sized
outcome comes next, what evidence accepts it, and when the work stops. They are
not architecture or data-contract documents.

## Authority boundary

| Document | Owns |
| --- | --- |
| Architecture specification | Normative requirements, system boundaries, milestone scope, and phase exit criteria |
| ADR | An accepted architectural choice, rationale, alternatives, and consequences |
| Schema | A persistent serialized data contract and compatibility boundary |
| Evidence | An immutable observation from an identified source and configuration |
| Epic | Delivery sequence, current status, dependencies, stop gates, and links to the authorities above |

If an epic conflicts with an authoritative document, the epic is wrong. Fix the
epic or deliberately revise the authoritative document; do not let delivery
tracking silently redefine behavior.

## File and lifecycle policy

- Use a stable, repository-wide identifier and filename:
  `EPIC-NNN-short-description.md`.
- Keep one file per epic, not one file per task. Each task row represents one
  independently reviewable and revertible pull-request-sized outcome.
- Keep completed, cancelled, and superseded epics at their original paths so
  links remain stable.
- Keep the current-state section short and overwrite it as the state changes.
  Git and pull-request history are the activity log; do not add a daily diary.
- Do not report percent complete. Report completed outcomes, the active task,
  blockers, and the next decision gate.
- At most one task in an epic may be `active`. Do not start conditional campaign
  work before its prerequisite decision gate passes.

## Status vocabulary

Epic status is one of:

- `proposed`: sequenced but not yet approved;
- `active`: approved and currently progressing;
- `paused`: intentionally stopped with a named resumption condition;
- `complete`: reached a recorded terminal outcome, which may be positive or
  negative;
- `cancelled`: stopped without the planned decision because its premise or
  priority was withdrawn; or
- `superseded`: replaced by another linked epic.

Task status is one of:

- `planned`, `ready`, `active`, `blocked`, `done`, or `dropped`.

Outcome is one of:

- `pending`: no terminal decision yet;
- `passed`: the epic's positive definition of done was satisfied;
- `negative`: valid evidence reached a declared negative stop;
- `cancelled`: the epic stopped without the planned decision; or
- `superseded`: a linked epic replaced it.

## Update and review policy

- The pull request that delivers a task keeps it `active` until its immutable PR
  or commit identity and acceptance evidence exist. Before final approval, the
  same pull request updates that task to `done`, refreshes current state, and
  marks at most one next task `ready`.
- A material change to goal, exit criteria, sequencing, scope, or stop gates is
  reviewed before dependent implementation. Add or revise an ADR when the
  change is architectural.
- Every implementation task must expose runnable behavior, validated evidence,
  or a mechanically derived decision. A preflight that intentionally cannot
  reach the behavior is not a complete implementation task.
- Target at most 12 hand-edited files and 1,500 hand-authored changed lines per
  task. A projected change above 2,500 lines must be re-sequenced before review.
- Each task receives a frozen APGAR adversarial review, including an explicit
  complexity-and-delivery verdict, and the repository gates required by
  `AGENTS.md` before commit.
- Preserve failed or negative observations. A complete negative experiment may
  complete an engineering epic without satisfying a positive phase exit
  criterion.

## Required epic contents

Each epic records:

1. metadata: status, outcome, owner, dates, baseline, and authority links;
2. one measurable goal and its definition of done;
3. scope, non-goals, inherited invariants, and complexity budget;
4. a short current-state summary;
5. an ordered task table with dependency and acceptance evidence;
6. decision and stop gates;
7. blockers and durable decisions as links rather than duplicated prose; and
8. a final closeout result when the epic terminates.

The task table uses this shape:

| Task | PR-sized outcome | Depends on | Acceptance evidence | Status | PR / commit / evidence |
| --- | --- | --- | --- | --- | --- |

## Epic index

| Epic | Status | Goal |
| --- | --- | --- |
| [EPIC-001: Phase 4 reboot](EPIC-001-phase4-reboot.md) | `active` | Reach an honest board-level global-allocation decision from a clean Phase 3 baseline through bounded, observable slices. |
