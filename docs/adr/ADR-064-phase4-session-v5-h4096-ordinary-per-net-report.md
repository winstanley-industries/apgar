# ADR-064: Phase 4 Session-v5 H=4096 Ordinary Per-Net Report Join

**Status:** Accepted as an inactive successor contract; report execution and
acquisition remain closed
**Date:** July 30, 2026
**Applies to:** Development-only Session-v5/H=4096 calibration diagnostics
before the complete consuming-publication chain exists

## Context

ADR-063 freezes the inactive Session-v5/H=4096 Raw boundary and permits
acquisition-free preflight and strict-validator work behind the immutable
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` closure. Confirmatory Decision
Protocol v3 separately reserves
`phase4_confirmatory_per_net_report_publication_join_v3` as the one-to-one
successor of the Protocol-v2 ordinary per-net authority.

The historical H=4096 ordinary report runner, builder, validator, and captured
artifacts remain Protocol-v2/roster-v3/Session-v4 authorities. Their Per-Net
Report Artifact schema 1 payload is still authoritative for that historical
configuration, but neither its compiled entry points nor its artifacts can be
reused for Session v5. In particular, the predecessor C++ report path
reconstructs a Session-v4 canonical specification and has diagnostic,
preparer, report-building, serialization, and output capability.

The ordinary calibration join is the smallest downstream successor because it
consumes one ordinary Raw artifact and one report. It has no Same-Run Decision
Telemetry sidecar. Choosing it before the same-run join follows the reviewed
historical progression and limits the first consuming-publication trust
surface; Protocol v3 does not make that order a hard dependency.

This contract may freeze and validate the successor join without observing a
new allocation outcome. It does not satisfy ADR-063's complete-chain
condition, activate the Raw runner, or authorize report production.

## Decision

- Preserve every Protocol-v2/roster-v3/Session-v4 report runner, builder,
  validator, artifact, checksum, golden, and source association unchanged.
  Existing V1, Corpus-v2/H=2250, and H=4096/Session-v4 entry points must keep
  their exact authority scopes and reject Session-v5 semantics.
- Reserve
  `phase4_confirmatory_per_net_report_publication_join_v3` as the only
  Session-v5 ordinary report authority. It directly supersedes
  `phase4_confirmatory_per_net_report_publication_join_v2`, which remains the
  successor of `phase4_confirmatory_per_net_report_publication_join_v1`.
  Authority selection is fixed out of band by a separately named compiled or
  validator boundary, never by a caller flag, case ID, source commit, matching
  checksum, or payload field.
- Bind the complete authority transition:
  - Confirmatory Decision Protocol v3 checksum
    `4963299999381388941`, directly superseding Protocol v2 checksum
    `11520586171987743043`;
  - canonical algorithm-budget roster v4 checksum
    `12316700735749461907`, directly superseding
    `phase4_confirmatory_canonical_algorithm_budget_roster_v3` checksum
    `18429170436700418962`;
  - configuration authority
    `phase4_confirmatory_corpus_v2_h4096_session_v5`, superseding
    `phase4_confirmatory_corpus_v2_h4096`;
  - ordinary Raw authority `phase4_confirmatory_raw_evidence_v3`,
    superseding `phase4_confirmatory_raw_evidence_v2`;
  - Representative Corpus v2 checksum `4182833841936446798`,
    Representative Manifest v2 checksum `9613362670139358355`, and
    Workload-Net Roster Manifest v2 checksum `14986327048461036142`; and
  - exact ordinary calibration cell `(10200,8)`, Raw Evidence schema 1 over
    Raw Wire 1, four preparation workers, and 20 repetitions.
- Reconstruct and positively require the complete Session-v5 canonical
  specification: Candidate-Allocation Session v5, Targeted Regeneration Plan
  v3, Targeted Regeneration Execution v6, and equal-arm
  `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`.
- Require canonical algorithm-budget checksum `7657176792159702821` and
  independently reconstructed paired semantic-budget checksum
  `13340538727848385478`. The successor must explicitly reject predecessor
  canonical checksum `8230401457668518004` and predecessor paired checksum
  `12108149041077564710`, including a self-consistent artifact for which every
  dependent checksum has been recomputed.
- Retain Per-Net Report Artifact schema 1, Raw Evidence schema 1, Raw Wire 1,
  the complete canonical key order, the artifact and source-envelope hash
  domains, arm-semantic association, telemetry and per-net checksum domains,
  the canonical serializer contract, complete ordered 64-net diagnostics, and
  permanent `decision_eligible=false`. The `v3` suffix changes only the
  configuration-specific publication authority. It does not mint a report,
  telemetry, Raw, wire, checksum, or serializer schema.
- Keep the join ordinary. It accepts no Same-Run Decision Telemetry authority,
  input, sidecar, or companion; Raw Wire 2, exact cell `(10100,4)`, the
  same-run report authority, and a caller-supplied telemetry path are foreign
  scopes.
- Reserve the strict offline validator name
  `phase4_confirmatory_h4096_session_v5_per_net_report_validator`. It must:
  1. authenticate Protocol v3, roster v4, complete ancestry and manifests, and
     the exact ordinary v3 authority substitution;
  2. read Raw as bounded canonical JSON from a regular file and completely
     authenticate Session-v5 ordinary Raw `(10200,8)`, including a complete
     successful-arm witness, both budget identities, and one independently
     supplied expected clean source commit;
  3. only after complete Raw authentication, read the report as bounded
     canonical JSON from a regular file; and
  4. require exact Raw/report equality for source envelope, configuration,
     corpus, cell plan, Raw artifact and source envelope, repetition-zero
     baseline-first pair and arm references, both complete semantics, both
     telemetry checksums, every per-net closure, and the ordered 64-net
     `EntityRef` roster.
- Raw must name one clean, stamped, 40-character lowercase commit equal to the
  independently supplied expected commit. A dirty, unstamped, malformed,
  abbreviated, duplicated-key, or mismatched Raw/expected identity rejects
  before report access. Immediately after the bounded canonical report read,
  the report must name that same clean source; a dirty, unstamped, malformed,
  abbreviated, duplicated-key, or cross-commit report identity rejects before
  semantic join or output. All-failure Raw remains unauthenticatable. A report
  cannot repair Raw, change outcome or timing authority, offset a failed
  guardrail, or become decision eligible.
- Authorize only a contract-only slice followed by a separately reviewed
  validator-only implementation using synthetic artifacts. Those stages add
  no report runner, report builder, evidence serializer, diagnostic execution,
  representative-case construction, board fixture, fixture or producer
  runfile, preparer, worker, allocator, output artifact, or acquisition
  capability. Fixed canonical authority JSON and pure validator-library
  runfiles remain required for hermetic authentication.
- Keep the future production runner
  `phase4_confirmatory_h4096_session_v5_per_net_report_runner` absent. A later
  separately reviewed producer-preflight slice must use a new
  capability-minimal target and a separately named immutable report-producer
  identity. That identity must bind the v3/v2 report authorities, exact
  ordinary cell/carrier/payload, complete Protocol/roster/configuration
  ancestry, both budget identities, and the absence of a telemetry companion
  before invoking the shared
  `P4PAIR-H4096-SESSION-V5-ACTIVATION-001` barrier.
- Any later production-shaped or test preflight binary must fail at that
  shared barrier before any diagnostic execution, case construction,
  preparation, allocation, report construction, evidence serialization, or
  durable output. Its final binary and runfiles tree must exclude board
  fixtures, case builders, diagnostic executors, preparers, workers,
  allocators, report builders, evidence serializers, artifact installers,
  durable-output paths, and acquisition-capable child executables.
- Require discriminating synthetic and process tests for the validator-only
  slice:
  - one complete canonical Session-v5 ordinary Raw/report pair must validate
    successfully;
  - valid Raw plus a poisoned report sentinel must prove that report access is
    attempted only after complete Raw authentication;
  - single-field deep-join mutations must reject after positive-path liveness
    is established;
  - bounded-file, canonical-JSON, depth, duplicate-key, corruption,
    rechecksummed-alias, cardinality, expected-commit, source-association, and
    all-failure rejection must hold;
  - every predecessor and successor authority, configuration, Session, Plan,
    Execution, cell, carrier, wire, canonical-budget, paired-budget, Raw, and
    report substitution must cross-reject in both directions; and
  - the final validator binary and runfiles must contain no evidence producer,
    board fixture, case construction, diagnostic, preparer, worker, allocator,
    report builder, artifact installer, durable-output, or acquisition
    capability. Pure validation, canonical reserialization, bounded inputs,
    fixed authority data, and bounded success/failure output remain allowed.
- Keep the same-run report, both operational publications, exact-small
  snapshot/Oracle chain, every other development cell, heldout, imported,
  fixed-query, stress, aggregation, matrix, decision, campaign, and acquisition
  path closed. The ordinary report successor alone does not satisfy the
  complete consuming-publication chain or weaken the Raw activation barrier.

## Consequences

The repository can implement and adversarially test one strict Session-v5
ordinary Raw/report join without adding a report producer or observing an
allocation result. The successor preserves the historical payload and makes
the configuration transition explicit rather than inferring it from content.

This contract does not publish a report, activate either development cell,
establish calibration non-regression, prove route-pool completeness, open the
confirmatory campaign, decide the matrix, complete Phase 4, or complete M1.

## Rejected alternatives

- **Implement both report joins together.** The ordinary join is a smaller
  two-artifact trust surface; the three-way same-run join receives its own
  adversarial review.
- **Reuse or widen the Protocol-v2 report validator or runner.** Those
  surfaces reconstruct Session v4 and must remain authentic historical
  authorities.
- **Treat fixtureless generated case 10200 as acquisition-free.** Diagnostic
  execution can observe allocator behavior without a board fixture; it remains
  closed until the complete successor chain is present.
- **Use only the shared Raw activation identity.** Reaching the global barrier
  does not prove a report successor. A future producer must first consume its
  own complete immutable report identity.
- **Add a production-shaped binary with dormant producer capabilities.** An
  always-taken barrier is insufficient if the final link or runfiles surface
  contains an unreviewed acquisition path.
- **Mint Per-Net Report Artifact v3.** Protocol v3 reserves an authority
  substitution, not a payload or checksum-domain change.
- **Accept an all-failure Raw capture or a cross-commit join.** Neither has the
  successful witness and same-source ancestry required by the publication
  contract.
