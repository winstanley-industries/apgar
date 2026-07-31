# Phase 4 Confirmatory Session-v5 H=4096 Same-Run Per-Net Report Publication Join v3

This inactive authority binds three independently read Session-v5/H=4096
Corpus-v2 artifacts:

1. one complete Same-Run Raw Evidence schema-2/Wire-2 exact cell;
2. its complete Same-Run Decision Telemetry schema-1/Telemetry-Wire-2
   companion; and
3. one independently generated Per-Net Report Artifact schema 1 with
   `raw_wire_schema_version=2`.

It directly supersedes only
`phase4_confirmatory_same_run_per_net_report_publication_join_v2`, which remains
the historical Protocol-v2/roster-v3/Session-v4 authority. The join is reserved
by Confirmatory Decision Protocol v3 under
`phase4_confirmatory_same_run_per_net_report_publication_join_v3`. This contract
and its strict offline validator authorize no report production, diagnostic
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
- same-run Raw authority `phase4_confirmatory_same_run_raw_evidence_v3`,
  superseding `phase4_confirmatory_same_run_raw_evidence_v2`;
- same-run telemetry authority
  `phase4_confirmatory_same_run_decision_telemetry_v3`, superseding
  `phase4_confirmatory_same_run_decision_telemetry_v2`;
- same-run report authority
  `phase4_confirmatory_same_run_per_net_report_publication_join_v3`,
  superseding
  `phase4_confirmatory_same_run_per_net_report_publication_join_v2`;
- Representative Corpus v2 checksum `4182833841936446798`;
- Representative Manifest v2 checksum `9613362670139358355`;
- Workload-Net Roster Manifest v2 checksum `14986327048461036142`; and
- exact cell `(10100,4)`, Raw Evidence schema 2 over Raw Wire 2, Same-Run
  Decision Telemetry schema 1 over Telemetry Wire 2, four preparation workers,
  20 repetitions, six ordered workload `EntityRef` values, and roster checksum
  `12521697377381992336`.

The complete canonical specification must use Candidate-Allocation Session v5,
Targeted Regeneration Plan v3, Targeted Regeneration Execution v6, and
equal-arm `present_step_per_overuse_unit=1` and
`history_step_per_overuse_unit=4096`.

Canonical algorithm-budget checksum `13645569624513409309` and independently
reconstructed paired semantic-budget checksum `12493092620111240227` are both
required. Predecessor canonical checksum `8829615204625848656` and predecessor
paired checksum `5851813264366095594` remain foreign even when every dependent
checksum is recomputed.

This is a same-run join. Ordinary Raw Wire 1, calibration cell `(10200,8)`, an
absent telemetry companion, a caller-selected telemetry authority, and the
ordinary per-net report authority are out of scope.

## Unchanged payload contracts

Same-Run Raw Evidence schema 2, Raw Wire 2, Same-Run Decision Telemetry schema
1, Telemetry Wire 2, and Per-Net Report Artifact schema 1 remain byte-contract
compatible. Their complete canonical key orders, artifact and source-envelope
hash domains, arm-semantic associations, telemetry and per-net checksum
domains, canonical serializer contracts, complete ordered six-net diagnostics,
and literal `decision_eligible=false` report value are unchanged.

Cardinality is exact: Raw contains 20 paired attempts and 40 arm attempts; the
sidecar contains 20 pair captures, 40 arm captures, and 240 ordered per-net
rows; and the report binds the Raw repetition-zero baseline-first pair through
two ordered arms and 12 per-net rows.

The authority suffix `v3` does not select or create a report, telemetry, Raw,
wire, checksum, or serializer schema. Raw remains allocation outcome and timing
authority. Same-run telemetry remains exact-rejection guardrail authority. A
structurally valid nonzero exact-rejection count is failed guardrail evidence,
not malformed evidence, and the report cannot offset or reinterpret it. The
three-way validator emits authentication status only; it does not compute or
emit the guardrail result, a statistic, a completion result, or a decision.

## Strict offline three-way publication join

`phase4_confirmatory_h4096_session_v5_same_run_per_net_report_validator`
enforces this open and validation order:

1. authenticate Protocol v3, roster v4, complete ancestry and manifests, and
   the exact same-run Raw, telemetry, and report v3 authority substitutions;
2. read Raw as bounded canonical JSON from a regular file;
3. completely authenticate Session-v5 same-run Raw `(10100,4)`, including its
   successful-arm witness, both independent budget identities, and one
   independently supplied expected clean source commit;
4. only after complete Raw authentication, read the same-run telemetry sidecar
   as bounded canonical JSON from a regular file;
5. immediately authenticate the sidecar's clean expected source envelope, then
   completely join its Raw cell, environment, controller, process, dispatch,
   pair, arm, outcome, ordered six-net roster, column partition, artifact, and
   source-envelope identities;
6. only after complete sidecar authentication and Raw association, read the
   report as bounded canonical JSON from a regular file;
7. immediately authenticate the report's clean expected source envelope; and
8. require exact Raw/report equality for configuration, corpus, cell plan, Raw
   artifact and source envelope, repetition-zero baseline-first pair and arm
   references, both complete semantics, both telemetry checksums, every per-net
   closure, and the ordered six-net `EntityRef` roster.

Raw, sidecar, report, and the independently supplied expected commit must name
one clean, stamped, 40-character lowercase commit. A dirty, unstamped,
malformed, abbreviated, duplicated-key, or mismatched Raw/expected identity
rejects before sidecar access. Immediately after the bounded canonical sidecar
read, a dirty, unstamped, malformed, abbreviated, duplicated-key, cross-commit,
or unauthenticated sidecar source envelope rejects before its deep join and
before report access. Immediately after the bounded canonical report read, the
same report defects reject before semantic join or output.

The sidecar and report remain independent companions of completely
authenticated Raw. No new direct sidecar/report checksum field is added. FIFO,
device, oversized, noncanonical, deeply nested, corrupt, rechecksummed-alias,
relaxed-cardinality, foreign-authority, and all-failure inputs reject. The Raw
input is bounded to 64 MiB; sidecar and report inputs are each bounded to
32 MiB; and every JSON input is bounded to depth 64.

Canonical synthetic Session-v5 Raw/sidecar/report triples must validate both
passing and structurally valid failing exact-rejection guardrail content.
Positive-path liveness must be established before negative mutations can count
as evidence. Poisoned sidecar and report sentinels must separately prove
Raw-first and sidecar-before-report open order. Independent artifact- and
source-envelope-checksum forgeries must reject. Combined source-envelope and
semantic corruption must report the source defect before deep joining that
companion, and an invalid sidecar source must reject before report access.
Unknown and abbreviated command-line options must reject.

## Inactive implementation boundary

This contract permits a separately reviewed strict offline validator using
only synthetic artifacts, fixed canonical authority data, pure validation,
canonical reserialization, bounded inputs, and bounded authentication-status
output.
The validator target and runfiles contain no evidence producer, board fixture,
case construction, diagnostic execution, preparer, worker, allocator, report
builder, artifact installer, durable-output, or acquisition capability.
Executable identity, declared outputs, `DefaultInfo.files`, default runfiles,
and data runfiles must match an exact pure-validator/fixed-authority-data
allowlist plus generated launcher files and the checksum-pinned Python runtime.
Workspace symlinks, root symlinks, empty filenames, and non-allowlisted entries
reject, with an analysis-time negative contract proving the symlink detectors.

The future
`phase4_confirmatory_h4096_session_v5_same_run_per_net_report_runner` remains
absent. A later separately reviewed producer-preflight slice must use a new
capability-minimal target and fully validate a separately named immutable
same-run report-producer identity before invoking the shared
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` closure. Every production-shaped and
test preflight binary must fail there before diagnostic execution, case
construction, preparation, allocation, report construction, evidence
serialization, or durable output, and its final link/runfiles surface must
exclude those capabilities.

This same-run report successor does not activate Raw, satisfy the complete
consuming-publication chain, or open either report producer, either operational
publication, exact-small snapshot/Oracle chain, any other development cell,
heldout, imported, fixed-query, stress, aggregation, matrix, decision,
campaign, or acquisition path.
