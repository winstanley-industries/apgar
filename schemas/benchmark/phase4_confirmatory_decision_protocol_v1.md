# Phase 4 Confirmatory Decision Protocol v1

This is the pre-observation protocol for the Representative Corpus v2
confirmatory campaign. It preserves the authentic negative v1 matrix and does
not supersede or reinterpret its decision publication.

The protocol incorporates Statistical Decision Protocol v4 by checksum and
inherits its board-level lexicographic outcome, exact family sign test, Holm
correction, guardrail thresholds, timing statistics, and nine completion
requirements without change. It replaces only the corpus identities, logical
case identities, and evidence authority namespace needed for a fresh campaign.

## Frozen matrix

The 104 logical cells are:

- exact cases `10100`-`10102` at pool `4`;
- calibration cases `10200/10201`, `10210/10211`, and `10220/10221` at pools
  `4/8/16`;
- heldout cases `11000`-`11007`, `11100`-`11107`, and `11200`-`11207` at
  pools `4/8/16`;
- fixed-query controls `(12000,1024)`, `(12001,1)`, `(12002,4)`,
  `(12003,8)`, and `(12004,16)`;
- stress cases `13000`-`13002` at pool `4`; and
- imported case `14000` at pools `4/8/16`.

There are 100 successful evidence cells, 86 noncalibration closure cells, and
82 successful noncalibration cells. The 22 calibration/fixed-query/stress raw
cells use the ordinary confirmatory raw authority. The 78
exact/heldout/imported cells use a distinct same-run raw authority whose
outcome/timing and exact-rejection telemetry are authenticated from one Wire
v2 invocation. Their report and operational joins consume that same-run raw
authority and cannot be paired with the ordinary raw path. The representative
manifest authenticates 102 canonical budget cells.

## Observation firewall

Only exact and calibration cases are development surfaces. At freeze time no
v2 heldout allocation outcome has been executed or inspected. Heldout
execution requires the commit containing this frozen protocol, a clean source
tree, and the explicit confirmatory corpus authority. Premature heldout
observation invalidates the roster and requires a fresh versioned corpus.

The manifest/protocol commit alone does not infer corpus authority from case
IDs and does not authorize a legacy v1 publisher to accept v2. Ordinary
raw/report/operational and same-run raw/telemetry/report/operational paths are
separate authorities. Exact-oracle, fixed-query, stress, runner, and
decision-publication paths must join the applicable authority explicitly and
remain fail-closed.

## Serialization

The compact canonical JSON ends in exactly one LF.
`artifact_checksum` uses Board IR v1 FNV-1a domain
`APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V1` over all preceding root
fields. The protocol contains no v2 outcome, family result, guardrail result,
hardware observation, source commit, or completion decision.
