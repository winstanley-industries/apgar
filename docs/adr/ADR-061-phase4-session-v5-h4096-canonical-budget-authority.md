# ADR-061: Phase 4 Session-v5 H=4096 Canonical Budget Authority

**Status:** Accepted and active as an acquisition-free configuration authority
**Date:** July 26, 2026
**Applies to:** The next canonical algorithm-budget roster after Session-v5
activation and before any successor protocol or evidence acquisition

## Context

ADR-060 activated Targeted Regeneration Plan v3, Targeted Regeneration
Execution v6, and CPU Candidate-Allocation Session v5. The implementation
remediates the reviewed primary-conflict coverage failure, but every frozen
Corpus-v2/H=2250 and H=4096 budget preimage remains bound to Session v4.
Existing Corpus-v2 execution entry points therefore fail closed before
fixture, preparer, worker, representative-case, or allocator access.

Canonical algorithm-budget roster v3 remains the complete H=4096 authority
for its 102 ordered cells. Confirmatory Decision Protocol v2, all previously
captured development artifacts, and all existing H=4096 consuming authorities
bind that exact roster and Session-v4 preimage. They cannot be reinterpreted
or relabeled after the Session-v5 policy change.

The paired-trial payload does not carry separate planner or executor schema
fields. Its canonical algorithm-budget checksum does hash the complete
candidate-session configuration, including the session schema. Session v5 in
turn normatively composes Plan v3 and Execution v6. A separately versioned
roster can therefore bind the new configuration without changing Paired Wire
v1, but it must make the transitive child transition explicit and must not
create an execution capability.

## Decision

- Preserve canonical algorithm-budget roster v3, its JSON bytes, checksum,
  generator, validator, and all 102 per-cell checksums.
- Implement the roster-v4 contract in
  `schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v4.md`.
  It directly supersedes roster v3 and retains Representative Corpus v2,
  Representative Manifest v2, Workload-Net Roster Manifest v2, all 102 ordered
  canonical cells, H=4096 equal-arm pricing, case and workload identities,
  root seeds, stopping depth, query/work opportunities, external budgets, and
  every non-session field. Its compact JSON has aggregate roster checksum
  `12316700735749461907`.
- Name the successor configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`. Do not reuse
  `phase4_confirmatory_corpus_v2_h4096`, which remains the frozen Session-v4
  configuration authority.
- Change exactly
  `candidate_session_config.schema_version` from Session v4 to Session v5 in
  every successor preimage. Every other candidate-session field must remain
  equal to roster v3.
- Bind the transition explicitly in roster metadata: Session v4 to v5,
  Targeted Regeneration Plan v2 to v3, and Targeted Regeneration Execution v5
  to v6. The per-cell
  `APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-V1` checksum directly binds Session
  v5 and the complete child configurations. The Session-v5 contract
  normatively binds Plan v3 and Execution v6; changing either child without a
  new Session authority is nonconforming.
- Add explicit fixed historical constants for Session v5 and Plan v3. The
  frozen builder must not depend on a moving current-version alias.
- Implement the successor preimage through a separately named private builder
  that first reconstructs the frozen H=4096/Session-v4 preimage, positively
  requires Session v4, and changes only the nested session schema to the
  explicit Session-v5 constant. It must not call a moving current Corpus-v2
  builder.
- Do not add, renumber, or reinterpret `Phase4TrialExecutionAuthority`,
  `Phase4RepresentativeCorpusAuthority`, or any serialized payload field in
  this slice. The new builder must remain absent from every execution switch
  and preflight allowlist.
- The no-argument generator emits all 102 ordered configuration preimages
  through the dead-section-eliminated preimage graph; its stdout SHA-256 is
  `1379050ceaf62bd5ff221827ab54565a9087b0c3fdbf04fa69fc81c7ea40e283`.
  Its exact final binary, including the sanitizer-reset audit artifact, must
  contain no fixture, case-construction, preparer, worker, candidate-store,
  routing, or allocator-execution capability.
- The implementation must retain v3 goldens and add full-field 102-cell
  differential coverage, deterministic live-roster reproduction, strict
  canonical JSON validation, exact ancestry checks, corruption tests, and
  ordinary/ASan/UBSan final-binary link inspection.
- Roster v4 is configuration authority only. It authorizes no execution,
  acquisition, Raw or same-run telemetry, report, operational measurement,
  exact snapshot or oracle, fixed-query, stress, imported, heldout, matrix,
  decision, aggregation, or publication path.
- Confirmatory Decision Protocol v2 remains byte-frozen and bound to roster
  v3. ADR-062 activates the separately reviewed acquisition-free Protocol-v3
  authority binding roster v4. Separately reviewed development acquisition
  and consuming-publication authorities remain required, and a
  protocol/roster commit alone is insufficient for execution.

## Consequences

This implementation freezes a complete Session-v5/H=4096 configuration
authority without observing an allocation result or weakening the Session-v4
evidence boundary. Roster-v3 artifacts remain reproducible and valid for their
historical authorities.

No performance, improvement, feasibility, fixed-pool optimality, calibration
non-regression, heldout, confirmatory, Phase 4, or M1 claim follows from this
contract. Fresh evidence remains prohibited until the complete separately
reviewed protocol, acquisition, execution, and publication chain exists in
one clean stamped source commit.

## Rejected alternatives

- **Build from a moving current Corpus-v2 constructor.** It could inherit
  later default or policy drift beyond the promised one-field transition.
- **Change the frozen roster-v3 builder in place.** That would rewrite
  historical preimages and invalidate their authority ancestry.
- **Add a Session-v5 execution enum now.** The existing gate rejects the two
  known Corpus-v2 execution identities; adding an unconsumed value in a
  configuration-only slice risks an accidental allow-through.
- **Freeze only the two development cells.** A sparse authority would not
  satisfy the complete-roster-before-observation contract and would create a
  weaker second budget namespace.
- **Reuse Protocol v2.** It binds roster v3 and the Session-v4 configuration
  authority exactly; reinterpreting it would be an in-place protocol change.
- **Acquire the development cells with only roster v4 present.** A budget
  roster authenticates configuration preimages, not execution, source,
  carrier, or publication authority.
