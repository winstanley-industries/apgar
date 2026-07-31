# Phase 4 Confirmatory Session-v5 H=4096 Ordinary Per-Net Report Publication Join v3

This inactive authority binds one complete ordinary Session-v5/H=4096
Corpus-v2 Raw Evidence schema-1/Wire-1 calibration cell to one independently
generated Per-Net Report Artifact schema 1. It directly supersedes only
`phase4_confirmatory_per_net_report_publication_join_v2`, which remains the
historical Protocol-v2/roster-v3/Session-v4 authority.

The join is reserved by Confirmatory Decision Protocol v3 under
`phase4_confirmatory_per_net_report_publication_join_v3`. This contract and
its strict offline validator authorize no report production, diagnostic
execution, allocation observation, or acquisition.

## Authority and configuration identity

The validator must authenticate the complete one-to-one transition:

- Confirmatory Decision Protocol v3 checksum `4963299999381388941`,
  superseding Protocol v2 checksum `11520586171987743043`;
- canonical algorithm-budget roster v4 checksum `12316700735749461907`,
  superseding
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3` checksum
  `18429170436700418962`;
- configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`, superseding
  `phase4_confirmatory_corpus_v2_h4096`;
- ordinary Raw authority `phase4_confirmatory_raw_evidence_v3`, superseding
  `phase4_confirmatory_raw_evidence_v2`;
- ordinary report authority
  `phase4_confirmatory_per_net_report_publication_join_v3`, superseding
  `phase4_confirmatory_per_net_report_publication_join_v2`;
- Representative Corpus v2 checksum `4182833841936446798`;
- Representative Manifest v2 checksum `9613362670139358355`;
- Workload-Net Roster Manifest v2 checksum `14986327048461036142`; and
- exact calibration cell `(10200,8)`, Raw Evidence schema 1 over Raw Wire 1,
  four preparation workers, and 20 repetitions.

The complete canonical specification must use Candidate-Allocation Session v5,
Targeted Regeneration Plan v3, Targeted Regeneration Execution v6, and
equal-arm `present_step_per_overuse_unit=1` and
`history_step_per_overuse_unit=4096`.

Canonical algorithm-budget checksum `7657176792159702821` and independently
reconstructed paired semantic-budget checksum `13340538727848385478` are both
required. Predecessor canonical checksum `8230401457668518004` and predecessor
paired checksum `12108149041077564710` remain foreign even when every dependent
checksum is recomputed.

This is an ordinary join. It has no Same-Run Decision Telemetry authority,
input, sidecar, or companion. Raw Wire 2, exact cell `(10100,4)`, and the
same-run per-net authority are out of scope.

## Unchanged payload contract

Per-Net Report Artifact schema 1 remains byte-contract compatible. Its complete
canonical key order, artifact and source-envelope hash domains, arm-semantic
association, telemetry and per-net checksum domains, canonical serializer
contract, complete ordered 64-net diagnostics, and literal
`decision_eligible=false` value are unchanged.

The authority suffix `v3` does not select or create a report, telemetry, Raw,
wire, checksum, or serializer schema. Raw remains outcome and timing authority;
the report remains diagnostic only.

## Strict offline publication join

`phase4_confirmatory_h4096_session_v5_per_net_report_validator` enforces this
open and validation order:

1. authenticate Protocol v3, roster v4, complete ancestry and manifests, and
   the exact ordinary v3 authority substitution;
2. read Raw as bounded canonical JSON from a regular file;
3. completely authenticate Session-v5 ordinary Raw `(10200,8)`, including its
   successful-arm witness, both independent budget identities, and one
   independently supplied expected clean source commit;
4. only after complete Raw authentication, read the report as bounded
   canonical JSON from a regular file; and
5. require exact Raw/report equality for source envelope, configuration,
   corpus, cell plan, Raw artifact and source envelope, repetition-zero
   baseline-first pair and arm references, both complete semantics, both
   telemetry checksums, every per-net closure, and the ordered 64-net
   `EntityRef` roster.

Raw must name one clean, stamped, 40-character lowercase commit equal to the
independently supplied expected commit. A dirty, unstamped, malformed,
abbreviated, duplicated-key, or mismatched Raw/expected identity rejects before
report access. Immediately after the bounded canonical report read, the report
must name that same clean source; a dirty, unstamped, malformed, abbreviated,
duplicated-key, or cross-commit report identity rejects before semantic join or
output. FIFO, device, oversized, noncanonical, deeply nested, corrupt,
rechecksummed-alias, relaxed-cardinality, foreign-authority, and all-failure
inputs reject.

A canonical synthetic Session-v5 Raw/report pair must validate successfully.
Positive-path liveness must be established before negative mutations can count
as evidence, and a valid Raw plus poisoned report sentinel must prove the
Raw-first open order.

## Inactive implementation boundary

This contract permits a separately reviewed strict offline validator using
only synthetic artifacts, fixed canonical authority data, pure validation,
canonical reserialization, bounded inputs, and bounded success/failure output.
The validator target and runfiles contain no evidence producer, board fixture,
case construction, diagnostic execution, preparer, worker, allocator, report
builder, artifact installer, durable-output, or acquisition capability.

The future
`phase4_confirmatory_h4096_session_v5_per_net_report_runner` remains absent.
A later separately reviewed producer-preflight slice must use a new
capability-minimal target and fully validate a separately named immutable
report-producer identity before invoking the shared
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` closure. Every production-shaped and
test preflight binary must fail there before diagnostic execution, case
construction, preparation, allocation, report construction, evidence
serialization, or durable output, and its final link/runfiles surface must
exclude those capabilities.

This ordinary report successor does not activate Raw, satisfy the complete
consuming-publication chain, or open the same-run report, either operational
publication, exact-small snapshot/Oracle chain, any other development cell,
heldout, imported, fixed-query, stress, aggregation, matrix, decision,
campaign, or acquisition path.
