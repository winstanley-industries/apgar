# EPIC-001: Phase 4 Reboot

| Field | Value |
| --- | --- |
| Status | `active` |
| Outcome | `pending` |
| Owner | APGAR maintainers |
| Started | August 1, 2026 |
| Last reviewed | August 1, 2026 ([PR #5](https://github.com/winstanley-industries/apgar/pull/5)) |
| Active baseline | `1a0d6600111c44e7f915b039190f9125d37d4a06` |
| Archived donor | `1f68ded7ff36547c3ffb8a0629482ad425786106` on `archive/phase4-pre-reset-2026-08-01` |
| Governing architecture | [Global Allocator](../APGAR_Architecture_Specification_v0.1.md#14-global-allocator), [Benchmark Plan](../APGAR_Architecture_Specification_v0.1.md#26-benchmark-and-evaluation-plan), [Roadmap](../APGAR_Architecture_Specification_v0.1.md#29-implementation-roadmap) |
| Governing decision | [ADR-068](../adr/ADR-068-phase4-clean-reboot.md) |

## Goal

Reach an honest, reproducible board-level decision about candidate-based global
allocation from the clean Phase 3 baseline through bounded implementation
slices with observable behavior.

The epic is not required to manufacture a positive result. It is required to
make the result trustworthy and terminal.

## Definition of done

The epic terminates in one of these recorded states:

- **complete / passed:** a frozen representative campaign satisfies the
  architecture's Phase 4 comparison against the named sequential baseline;
- **complete / negative:** campaign evidence fails its frozen rule, or readiness
  fails and the one permitted remediation is declined or also fails; Phase 4
  remains incomplete and further algorithm research moves to a newly approved
  epic; or
- **cancelled:** the candidate-allocation premise or project priority is
  withdrawn before a complete decision, with the reason recorded.

Missing, corrupt, or infrastructure-invalid evidence is not an allocator loss
and cannot complete the epic.

## Scope

- Authentic distinct-net workloads and explicit capacity-bearing resources.
- Deterministic CPU reference selection, usage accumulation, overuse, prices,
  and targeted regeneration over immutable candidates.
- A named sequential negotiated-routing baseline.
- Bounded candidate preparation and a minimal reusable allocation contender.
- Fixed-pool multi-world behavior only after the single-world reference is
  correct and observable.
- One compact, checksummed development/campaign evidence path with independent
  exact-small validation and board-level outcomes.

## Non-goals

- Porting or finishing the archived evidence publication graph.
- Guaranteeing that candidate allocation beats the baseline.
- GPU allocation before the deterministic CPU evidence path establishes need.
- Phase 5 legalization, combined-board DRC, host-CAD commit, shove, tuning,
  differential pairs, buses, or other deferred specialty routing.
- Deleting the archived branch or rewriting `main`.

## Inherited invariants

- Exact Board IR geometry and exact candidate admission remain authoritative.
- Route generators produce immutable candidates and never mutate global
  occupancy while searching.
- Every correctness-critical optimized primitive retains an independent CPU
  reference suitable for differential testing.
- Identical supported inputs produce deterministic externally visible results.
- Unsupported rules fail closed and remain delegated explicitly.
- Evidence names its corpus, configuration, seed, hardware/backend, toolchain,
  source commit, and comparison baseline.
- An archived schema path and major version is restored only with a compatible
  contract; an incompatible reboot contract receives a new identity.

These are summaries only; the architecture and accepted ADRs remain normative.

## Complexity budget

- One task is active at a time; campaign tasks remain conditional on readiness.
- A task targets at most 12 hand-edited files and 1,500 hand-authored changed
  lines. A projection above 2,500 lines triggers re-sequencing.
- Every implementation task produces runnable behavior, validated evidence, or
  a mechanical decision.
- One focused ADR or one serialized schema change may accompany a task, not an
  expanding family of parallel authorities.
- Every task receives an APGAR adversarial review before commit and the gates
  required by `AGENTS.md`.

## Current state

- The active branch started from the complete Phase 3 baseline; P4R-02A1 is the
  first Phase 4 implementation slice after the governance-only P4R-01.
- The former 71-commit Phase 4 lineage is recoverable from the remote archive
  and is not an active backlog.
- ADR-068 establishes the clean restart and behavior-by-behavior salvage rule.
- P4R-01 is approved and durably identified by PR #3.
- The approved P4R-02 split separates the prerequisite per-net routing contract
  from the authentic workload and accounting slice. Scope tracing then showed
  that the prerequisite itself crosses more than the 12-file task budget, so it
  is split again without changing its outcome: P4R-02A1 establishes prepared,
  retained, fail-closed compilation contexts; P4R-02A2 authenticates those
  contexts through route production and exact candidate admission.
- P4R-02A1 is active. Selection and resource accounting remain closed.

## Salvage ledger

| Archived area | Disposition | Reboot rule |
| --- | --- | --- |
| Phase 3 Candidate Store, exact admission, CPU A*, replay | Keep baseline | Already authoritative at `1a0d660`; extend only through focused tests and APIs. |
| Multi-net workload and resource capacities | Reimplement from behavior | Begin with exact accounting and one generated microcase; do not import the broad first allocator commit. |
| One-World, prices, pool preparation, baseline, regeneration | Reimplement in sequence | Each behavior gets an independent observable slice and reference checks. |
| Multi-World and reusable allocation session | Reassess, then reimplement if needed | Keep off the critical path until the One-World contender is correct and the epic reaches its scheduled task. |
| Primary-conflict coverage and H=4096 | Development hypotheses | Consider only after a reproduced readiness failure identifies the same mechanism. |
| Raw, telemetry, report, operational, Oracle-publication, and preflight graph | Drop | Replace with one compact evidence path after readiness; do not port authority parity. |
| Historical negative matrices and development observations | Context only | Preserve on the archive; never relabel them as reboot evidence. |

## Sequenced tasks

| Task | PR-sized outcome | Depends on | Acceptance evidence | Status | PR / commit / evidence |
| --- | --- | --- | --- | --- | --- |
| P4R-01 | Establish the clean branch, archived donor, ADR-068, epic governance, and APGAR complexity gate. | Phase 3 baseline | Exact archive/base identities; docs links; APGAR review; repository gates. | `done` | [PR #3](https://github.com/winstanley-industries/apgar/pull/3) |
| P4R-02A1 | Add prepared per-net routing profiles retained by exact CompiledBoard contexts. | P4R-01 | Two distinct nets compile from one immutable snapshot with net-specific exact obstacle ownership; invalid profiles fail closed; downstream route/candidate consumers reject non-default contexts pending P4R-02A2. | `active` | [PR #5](https://github.com/winstanley-industries/apgar/pull/5) |
| P4R-02A2 | Bind prepared-context identity through CPU/GPU producer evidence, route admission, and exact immutable-candidate admission. | P4R-02A1 | Distinct-net requests produce and exactly admit correctly attributed immutable candidates; relabeled or mismatched context evidence fails closed with a typed routing-profile mismatch rather than the temporary rule-bucket diagnostic. | `planned` | — |
| P4R-02B | Add an authentic small multi-net workload and explicit resource-capacity/accounting reference. | P4R-02A2 | Generated distinct-net microcase; canonical resources; independent usage and overuse checks. | `planned` | — |
| P4R-03 | Add deterministic One-World selection over immutable prebuilt candidate pools. | P4R-02B | One candidate or structured absence per net; lexicographic selection; deterministic repeats; independent accumulation. | `planned` | — |
| P4R-04 | Add bounded deterministic CPU candidate-pool preparation. | P4R-03 | Exact admission, stable publication order, bounded failure behavior, and worker-count invariance. | `planned` | — |
| P4R-05 | Add the named sequential negotiated-routing baseline. | P4R-04 | Deterministic board-level outcome and independently recomputed resource usage under declared bounds. | `planned` | — |
| P4R-06 | Add bounded negotiated prices and a deterministic targeted-regeneration plan. | P4R-05 | Replayable price updates, stable hotset/targets, declared caps, and no generator-side global mutation. | `planned` | — |
| P4R-07 | Execute one targeted-regeneration epoch through exact admission and refreshed One-World selection. | P4R-06 | Authentic generated columns, atomic publication, rejection diagnostics, and independently reproduced outcome. | `planned` | — |
| P4R-08 | Compose bounded epochs into the minimal reusable CPU allocation contender. | P4R-07 | Declared whole-session bounds, deterministic fixed point or typed stop, stable replay, and retained authoritative pools. | `planned` | — |
| P4R-09 | Add fixed-pool Multi-World as a reference extension, without interleaved world-dependent publication. | P4R-08 | Common starting pool/state, deterministic worlds, bounded retention, and stable preferred outcome. | `planned` | — |
| P4R-10 | Add one compact evidence bundle and validator for paired development observations. | P4R-09 | Minimal versioned fields, atomic serialization, provenance binding, round-trip tests, and pass/fail/incomplete golden cases. | `planned` | — |
| P4R-11 | Add one direct paired development runner that emits the compact bundle. | P4R-10 | Process-isolated equal-budget arms, exact-small independent Oracle, typed failures, and one local end-to-end fixture. | `planned` | — |
| P4R-12 | Freeze the bounded readiness manifest and mechanical rule before observing its runs. | P4R-11 | Named exact and calibration cells, pool sizes, configurations, budgets, Oracle rule, lexicographic non-regression rule, and checksum. | `planned` | — |
| P4R-13 | Acquire exact and calibration readiness evidence from one clean reviewed commit. | P4R-12 | Complete manifest-bound production/Oracle equality and calibration decision, including a valid negative result. | `planned` | — |
| P4R-13R | Conditional: implement one focused allocator remediation and reacquire the same readiness manifest. | Failed P4R-13 | One reproduced root-cause failure, one discriminating fix, unchanged evidence rule, and complete reacquisition. | `planned` | — |
| P4R-14 | Conditional on readiness: freeze the representative campaign and mechanical evaluator. | Passed P4R-13 or P4R-13R | Pre-observation manifest, fixed decision/guardrail tests, and pass/failed/incomplete golden cases. | `planned` | — |
| P4R-15 | Add resumable manifest orchestration without adding artifact kinds. | P4R-14 | Mini-manifest interruption/resume, no overwrite or duplicate identity, canonical inventory, and source consistency. | `planned` | — |
| P4R-16 | Acquire the frozen campaign and publish its mechanical decision. | P4R-15 | Complete validated bundle and reproducible pass or negative report from the exact frozen source commit. | `planned` | — |

## Decision and stop gates

- Do not begin selection until P4R-02B independently validates resource usage
  and overuse semantics. P4R-02A1 contexts are intentionally fail-closed to
  downstream routing and candidate admission until P4R-02A2 authenticates the
  retained per-net profile identity end to end.
- Do not acquire readiness until P4R-12 freezes its workload identities,
  configurations, budgets, and rule. Do not begin campaign tooling or inspect
  heldout outcomes until readiness passes and P4R-14 freezes the campaign and
  evaluator.
- Exact-Oracle disagreement, nondeterminism, false-free candidate acceptance, or
  unequal budget enforcement is a correctness stop, not an allocator loss.
- After the first complete readiness failure, either decline remediation and
  terminate negatively or permit only P4R-13R. A failed reacquisition
  terminates the epic negatively.
- A complete negative campaign terminates the epic. Post-heldout tuning requires
  a new epic and fresh heldout authority.
- Any proposed task exceeding the complexity budget is split before review; it
  does not receive a scope exception merely because archived code already
  exists.

## Durable decisions and evidence

- [ADR-068: Phase 4 Clean Reboot](../adr/ADR-068-phase4-clean-reboot.md)
- Archived donor: `archive/phase4-pre-reset-2026-08-01` at
  `1f68ded7ff36547c3ffb8a0629482ad425786106`
- Active baseline: `1a0d6600111c44e7f915b039190f9125d37d4a06`

## Closeout

Pending. When the epic terminates, replace this paragraph with the terminal
outcome, decision artifact or cancellation rationale, final source commit, and
the next authorized epic or phase.
