# ADR-062: Phase 4 Session-v5 H=4096 Protocol Authority

**Status:** Accepted and active as an acquisition-free protocol authority
**Date:** July 26, 2026
**Applies to:** Confirmatory protocol supersession after canonical roster v4
and before any Session-v5 Corpus-v2 execution or evidence acquisition

## Context

ADR-061 activated canonical algorithm-budget roster v4 as an acquisition-free
Session-v5/H=4096 configuration authority. It retains all 102 ordered
roster-v3 cells and changes exactly the nested candidate-allocation Session
schema from v4 to v5, with the Plan-v2-to-v3 and Execution-v5-to-v6 child
transition explicitly bound.

Confirmatory Decision Protocol v2 remains bound to roster v3 and the
Session-v4/H=4096 configuration. ADRs 053 through 059 used that authority
chain to acquire two authentic development bundles. Those artifacts remain
historical evidence for their exact authorities and source commits. Reusing
Protocol v2 or any of its artifact names for Session v5 would reinterpret
observed evidence and violate the configuration boundary.

Roster v4 cannot select an execution path or make an artifact publishable. A
separate protocol authority must bind the new configuration, preserve the
matrix and decision rules, reserve a fresh consuming namespace, and record
truthfully that predecessor development outcomes exist while no Session-v5
allocation outcome has been observed.

## Decision

- Preserve Confirmatory Decision Protocol v2 byte-for-byte and adopt the
  implemented acquisition-free Protocol-v3 authority in
  `schemas/benchmark/phase4_confirmatory_decision_protocol_v3.md`. It directly
  supersedes schema 2 artifact checksum `11520586171987743043`. Its canonical
  artifact checksum is `4963299999381388941`.
- Retain campaign `phase4_confirmatory_corpus_v2`, preserve the authentic V1
  negative matrix and both Protocol-v2 development observations, and bind
  configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`.
- Bind canonical algorithm-budget roster v4 authority, schema 4, all 102
  cells, checksum `12316700735749461907`, its roster-v3 ancestry, and its exact
  Session-v4-to-v5, Plan-v2-to-v3, and Execution-v5-to-v6 transition.
- Reconstruct the effective protocol from Protocol v2 and change only its
  roster/configuration binding, successor-scoped observation firewall, and
  artifact authority namespace. The 104 logical cells, 100 successful
  evidence cells, 86 noncalibration closure cells, 82 successful
  noncalibration cells, role and evidence dispositions, families, board
  outcome, inference, guardrails, timing, counts, and nine completion
  requirements remain unchanged.
- Record that Protocol-v2 development outcomes have been observed and are
  preserved, while Session-v5/H=4096 allocation outcomes and publishable
  evidence have not been created. Do not copy Protocol v2's now-historical
  global no-H4096-observation assertion into Protocol v3.
- Reserve fresh v3 authorities for all ten Protocol-v2 cell and shared
  artifact purposes, with an exact one-to-one v2-to-v3 substitution. Reserve
  `phase4_confirmatory_matrix_decision_publication_v3` as the only successor
  complete-decision publication authority.
- Treat authority suffixes as authority versions only. They do not implicitly
  change a Raw Evidence, paired-trial wire, telemetry, report, capture,
  snapshot, or other payload schema. Every later consuming contract must bind
  its carrier and payload versions explicitly.
- Retain the only initial development identities that future separately
  reviewed Session-v5 entry points may open: exact `(10100,4)` through the
  same-run Wire-2 chain and calibration `(10200,8)` through the ordinary
  Wire-1 chain. Their Raw authorities advance to the corresponding v3 names;
  Protocol-v2 artifacts cannot satisfy them.
- Keep every Session-v5 Corpus-v2 execution and acquisition path closed in
  this slice. Protocol v3 alone authorizes no fixture, case, preparer, worker,
  allocator, replay, Raw artifact, downstream publication, Oracle, matrix, or
  decision execution.
- Require separately reviewed Session-v5 development execution/acquisition and
  consuming-publication authorities before either initial cell may run.
  ADR-063 freezes an inactive Raw successor contract and permits only
  acquisition-free implementation behind a pre-fixture closure; Raw
  validation alone does not satisfy the complete consuming-publication chain.
  Heldout observation additionally requires one clean stamped source commit
  containing exact Protocol v3 and roster v4 plus the complete reviewed
  execution and publication chain. A protocol/roster-only commit is
  insufficient.
- Treat any premature Session-v5/H=4096 heldout observation as invalidating
  roster v4 and requiring a fresh versioned authority. Keep imported,
  fixed-query, stress, full-matrix, aggregation, and decision paths closed.

## Consequences

This implementation freezes the complete Session-v5/H=4096 decision contract
and consuming namespace without executing an allocator or creating evidence.
Later development execution and publication capabilities remain separate
narrow slices.

No performance, improvement, feasibility, fixed-pool optimality, calibration
non-regression, heldout, confirmatory, Phase 4, or M1 claim follows from this
contract. Protocol-v2 evidence remains valid only under its historical
Session-v4 authorities.

## Rejected alternatives

- **Reuse Protocol v2.** It checksum-binds roster v3 and Session v4 exactly;
  reinterpretation would mutate a historical authority in place.
- **Reuse v2 artifact names with a new source commit.** Authority identity
  must separate configuration semantics; a source stamp cannot repair an
  ambiguous or reused namespace.
- **Claim that no H=4096 outcome has been observed.** ADR-059 records two
  authentic Protocol-v2 development observations. Only the Session-v5
  successor state is unobserved.
- **Open execution while freezing the protocol.** The protocol cannot bind
  source, carrier, runner, validator, and publication capabilities that have
  not been separately reviewed.
- **Promote Protocol-v2 development evidence.** Its Session-v4 budget
  preimages differ from every roster-v4 checksum and cannot satisfy the
  Session-v5 chain.
