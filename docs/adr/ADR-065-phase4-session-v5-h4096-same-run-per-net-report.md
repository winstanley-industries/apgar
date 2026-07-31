# ADR-065: Phase 4 Session-v5 H=4096 Same-Run Per-Net Report Join

**Status:** Accepted as an inactive successor contract; report execution and
acquisition remain closed
**Date:** July 30, 2026
**Applies to:** Development-only Session-v5/H=4096 exact-cell diagnostics
before the complete consuming-publication chain exists

## Context

ADR-063 freezes the inactive Session-v5/H=4096 Raw boundary, including atomic
exact-cell Same-Run Raw and Decision Telemetry validation. ADR-064 separately
freezes the smaller ordinary Raw/report successor. Confirmatory Decision
Protocol v3 reserves
`phase4_confirmatory_same_run_per_net_report_publication_join_v3` as the
one-to-one successor of the Protocol-v2 same-run per-net authority.

The historical H=4096 same-run report runner, builder, validator, and captured
artifacts remain Protocol-v2/roster-v3/Session-v4 authorities. Their Same-Run
Raw Evidence schema 2, Same-Run Decision Telemetry schema 1, and Per-Net Report
Artifact schema 1 payloads remain authoritative for that historical
configuration, but neither the artifacts nor the compiled producer surfaces
can be reused for Session v5.

The same-run join is the next smallest downstream successor because it consumes
one exact Raw artifact, its same-invocation telemetry sidecar, and one report.
It preserves three distinct authorities: Raw owns allocation outcome and
timing, the sidecar owns the exact-rejection guardrail, and the report is
diagnostic only. Freezing and validating that three-way trust boundary need not
execute an allocator or observe a new allocation outcome.

This contract does not satisfy ADR-063's complete-chain condition, activate the
Raw runner, or authorize telemetry or report production.

## Decision

- Preserve every Protocol-v2/roster-v3/Session-v4 same-run Raw, telemetry,
  report runner, builder, validator, artifact, checksum, golden, and source
  association unchanged. Existing V1, Corpus-v2/H=2250, H=4096/Session-v4, and
  ordinary entry points must keep their exact authority scopes and reject
  Session-v5 same-run semantics.
- Reserve
  `phase4_confirmatory_same_run_per_net_report_publication_join_v3` as the only
  Session-v5 same-run report authority. It directly supersedes
  `phase4_confirmatory_same_run_per_net_report_publication_join_v2`, which
  remains the successor of
  `phase4_confirmatory_same_run_per_net_report_publication_join_v1`. Authority
  selection is fixed out of band by a separately named compiled or validator
  boundary, never by a caller flag, case ID, source commit, matching checksum,
  carrier field, or payload field.
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
  - same-run Raw authority `phase4_confirmatory_same_run_raw_evidence_v3`,
    superseding `phase4_confirmatory_same_run_raw_evidence_v2`;
  - same-run telemetry authority
    `phase4_confirmatory_same_run_decision_telemetry_v3`, superseding
    `phase4_confirmatory_same_run_decision_telemetry_v2`;
  - same-run report authority
    `phase4_confirmatory_same_run_per_net_report_publication_join_v3`,
    superseding
    `phase4_confirmatory_same_run_per_net_report_publication_join_v2`;
  - Representative Corpus v2 checksum `4182833841936446798`,
    Representative Manifest v2 checksum `9613362670139358355`, and
    Workload-Net Roster Manifest v2 checksum `14986327048461036142`; and
  - exact cell `(10100,4)`, Raw Evidence schema 2 over Raw Wire 2, Same-Run
    Decision Telemetry schema 1 over Telemetry Wire 2, Per-Net Report Artifact
    schema 1 with `raw_wire_schema_version=2`, four preparation workers, 20
    repetitions, six ordered workload `EntityRef` values, and roster checksum
    `12521697377381992336`.
- Reconstruct and positively require the complete Session-v5 canonical
  specification: Candidate-Allocation Session v5, Targeted Regeneration Plan
  v3, Targeted Regeneration Execution v6, and equal-arm
  `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`.
- Require canonical algorithm-budget checksum `13645569624513409309` and
  independently reconstructed paired semantic-budget checksum
  `12493092620111240227`. The successor must explicitly reject predecessor
  canonical checksum `8829615204625848656` and predecessor paired checksum
  `5851813264366095594`, including a self-consistent artifact for which every
  dependent checksum has been recomputed.
- Retain Same-Run Raw Evidence schema 2, Raw Wire 2, Same-Run Decision Telemetry
  schema 1, Telemetry Wire 2, Per-Net Report Artifact schema 1, their complete
  canonical key orders, artifact and source-envelope hash domains, arm-semantic
  associations, telemetry and per-net checksum domains, canonical serializer
  contracts, complete ordered six-net diagnostics, and permanent
  `decision_eligible=false`. The `v3` suffix changes only the
  configuration-specific publication authorities. It does not mint a report,
  telemetry, Raw, wire, checksum, or serializer schema.
- Preserve exact cardinality: Raw has 20 paired attempts and 40 arm attempts;
  the sidecar has 20 pair captures, 40 arm captures, and 240 ordered per-net
  rows; and the report binds the Raw repetition-zero baseline-first pair
  through two ordered arms and 12 per-net rows.
- Keep the join same-run. It rejects ordinary Raw Wire 1, calibration cell
  `(10200,8)`, the ordinary report authority, a missing telemetry companion,
  any caller-selected telemetry authority, and any sidecar that does not
  completely associate with the same Raw invocation.
- Reserve the strict offline validator name
  `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_validator`. It
  must:
  1. authenticate Protocol v3, roster v4, complete ancestry and manifests, and
     the exact same-run Raw, telemetry, and report v3 authority substitutions;
  2. read Raw as bounded canonical JSON from a regular file and completely
     authenticate Session-v5 same-run Raw `(10100,4)`, including a complete
     successful-arm witness, both budget identities, and one independently
     supplied expected clean source commit;
  3. only after complete Raw authentication, read the sidecar as bounded
     canonical JSON from a regular file, immediately authenticate its clean
     expected source envelope, and completely join its Raw cell, environment,
     controller, process, dispatch, pair, arm, outcome, ordered six-net roster,
     column partition, artifact, and source-envelope identities;
  4. only after complete sidecar authentication and Raw association, read the
     report as bounded canonical JSON from a regular file and immediately
     authenticate its clean expected source envelope; and
  5. require exact Raw/report equality for configuration, corpus, cell plan, Raw
     artifact and source envelope, repetition-zero baseline-first pair and arm
     references, both complete semantics, both telemetry checksums, every
     per-net closure, and the ordered six-net `EntityRef` roster.
- Raw, sidecar, report, and one independently supplied expected commit must name
  the same clean, stamped, 40-character lowercase commit. A dirty, unstamped,
  malformed, abbreviated, duplicated-key, or mismatched Raw/expected identity
  rejects before sidecar access. Immediately after the bounded canonical
  sidecar read, the sidecar must name and authenticate that same clean source;
  any source defect rejects before its deep join and before report access.
  Immediately after the bounded canonical report read, the report must name and
  authenticate that same clean source; any source defect rejects before
  semantic join or output.
- Keep the sidecar and report independently joined to completely authenticated
  Raw. They gain no new direct shared field. Raw remains allocation outcome and
  timing authority. Same-run telemetry remains exact-rejection guardrail
  authority. A structurally valid nonzero exact-rejection count is failed
  guardrail evidence, not malformed evidence, and the report cannot repair,
  offset, or reinterpret it. The three-way validator emits authentication
  status only; it does not compute or emit the guardrail result, a statistic, a
  completion result, or a decision.
- Authorize only a contract-only slice followed by a separately reviewed
  validator-only implementation using synthetic artifacts. Those stages add no
  telemetry or report runner, report builder, evidence serializer, diagnostic
  execution, representative-case construction, board fixture, fixture or
  producer runfile, preparer, worker, allocator, output artifact, or acquisition
  capability. Fixed canonical authority JSON and pure validator-library
  runfiles remain required for hermetic authentication.
- Keep the future production runner
  `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_runner` absent.
  A later separately reviewed producer-preflight slice must use a new
  capability-minimal target and a separately named immutable same-run
  report-producer identity. That identity must bind the v3/v2 report, Raw, and
  telemetry authorities, exact cell/carrier/payload, complete
  Protocol/roster/configuration ancestry, and both budget identities before
  invoking the shared `P4PAIR-H4096-SESSION-V5-ACTIVATION-001` barrier.
- Any later production-shaped or test preflight binary must fail at that shared
  barrier before any diagnostic execution, case construction, preparation,
  allocation, report construction, evidence serialization, or durable output.
  Its final binary and runfiles tree must exclude board fixtures, case builders,
  diagnostic executors, preparers, workers, allocators, report builders,
  evidence serializers, artifact installers, durable-output paths, and
  acquisition-capable child executables.
- Require discriminating synthetic and process tests for the validator-only
  slice:
  - complete canonical Session-v5 Raw/sidecar/report triples must validate
    successfully for both passing and structurally valid failing
    exact-rejection guardrail content;
  - poisoned sidecar and report sentinels must separately prove Raw-first and
    sidecar-before-report access ordering;
  - independent artifact- and source-envelope-checksum forgeries must reject,
    while combined source-envelope and semantic corruption must report the
    source defect before deep joining that companion; an invalid sidecar source
    must also reject before report access;
  - single-field deep-join mutations must reject after positive-path liveness
    is established;
  - bounded-file, canonical-JSON, depth, duplicate-key, corruption,
    rechecksummed-alias, cardinality, expected-commit, source-association, and
    all-failure rejection must hold at each applicable boundary; Raw is bounded
    to 64 MiB, sidecar and report are each bounded to 32 MiB, and every input is
    bounded to JSON depth 64;
  - Raw pair/arm, sidecar pair/arm/per-net-row, and report arm/per-net-row
    cardinalities must be perturbed independently;
  - unknown and abbreviated command-line options must reject rather than select
    an input or testing mode;
  - every predecessor and successor authority, configuration, Session, Plan,
    Execution, cell, carrier, wire, canonical-budget, paired-budget, Raw,
    telemetry, and report substitution must cross-reject in both directions;
    and
  - the final validator binary and runfiles must contain no evidence producer,
    board fixture, case construction, diagnostic, preparer, worker, allocator,
    report builder, artifact installer, durable-output, or acquisition
    capability. Its executable path, owner, declared outputs,
    `DefaultInfo.files`, default runfiles, and data runfiles must match an exact
    allowlist containing only pure validation sources, fixed authority data,
    generated validator-launcher files, and the checksum-pinned Python runtime.
    Workspace symlinks, root symlinks, empty filenames, and all non-allowlisted
    entries reject; an analysis-time negative contract must prove the symlink
    detectors are live. Pure validation, canonical reserialization, bounded
    inputs, fixed authority data, and bounded authentication-status output
    remain allowed.
- Keep both report producers, both operational publications, exact-small
  snapshot/Oracle chain, every other development cell, heldout, imported,
  fixed-query, stress, aggregation, matrix, decision, campaign, and acquisition
  paths closed. The two validator-only report successors do not satisfy the
  complete consuming-publication chain or weaken the Raw activation barrier.

## Consequences

The repository can implement and adversarially test one strict Session-v5
Raw/telemetry/report join without adding a producer or observing an allocation
result. The successor preserves historical payloads and keeps outcome, guardrail,
and diagnostic authority separate.

This contract does not publish a report or telemetry sidecar, activate either
development cell, establish exact optimality, prove route-pool completeness,
open the confirmatory campaign, decide the matrix, complete Phase 4, or
complete M1.

## Rejected alternatives

- **Reuse or widen the Protocol-v2 same-run validator or runner.** Those
  surfaces reconstruct Session v4 and must remain authentic historical
  authorities.
- **Parameterize the ordinary Session-v5 validator.** Authority selection by
  caller input would make ordinary and same-run carriers substitutable and
  would hide the larger three-artifact trust surface.
- **Read all three inputs before authenticating Raw.** A foreign or malformed
  Raw artifact must not grant access to either downstream companion.
- **Open the report before completely joining the sidecar.** The report cannot
  repair an unauthenticated or failed exact-rejection guardrail.
- **Add a direct sidecar/report checksum association.** Both artifacts already
  bind completely authenticated Raw; a new field would change frozen payload
  and checksum domains without adding authority.
- **Treat fixtureless generated case 10100 as acquisition-free.** Diagnostic
  execution can observe allocator behavior without a board fixture; it remains
  closed until the complete successor chain is present.
- **Add a production-shaped binary with dormant producer capabilities.** An
  always-taken barrier is insufficient if the final link or runfiles surface
  contains an unreviewed acquisition path.
- **Mint new Raw, telemetry, or report payload schemas.** Protocol v3 reserves
  authority substitutions, not payload or checksum-domain changes.
- **Accept an all-failure Raw capture, sidecar lacking the same-invocation Raw
  associations, or cross-commit join.** They lack the successful witness and
  same-source atomic association required by the publication contract.
