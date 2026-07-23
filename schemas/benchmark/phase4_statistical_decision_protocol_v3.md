# Phase 4 Statistical Decision Protocol v3

Protocol v3 is an authority-only supersession of Statistical Decision Protocol
v2. It incorporates the exact canonical v2 protocol by schema and artifact
checksum. The 104-cell matrix, evidence dispositions, families, outcome
ordering, inference, guardrail thresholds, timing summaries, and completion
requirements are unchanged.

The supersession closes the exact-small authority chain. Protocol v2 correctly
requires Same-Run Raw Evidence v2 for exact cells, but inherited Exact-Small
Oracle Artifact v1, whose envelope cannot durably bind the mandatory same-run
companion. The three exact cells therefore replace
`phase4_exact_small_oracle_v1` with `phase4_exact_small_oracle_v2`. No other
group or artifact requirement changes.

The compact JSON is authenticated with byte-stable FNV-1a domain
`APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V3` over the canonical JSON of all
preceding root fields. The validator first validates incorporated protocol v2,
then reconstructs the one exact-group substitution and proves that the
expanded logical cells and dispositions remain identical.
