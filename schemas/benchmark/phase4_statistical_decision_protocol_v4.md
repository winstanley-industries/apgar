# Phase 4 Statistical Decision Protocol v4

Protocol v4 is an authority-only supersession of Statistical Decision Protocol
v3. It incorporates the exact canonical v3 protocol by schema and artifact
checksum. The 104-cell matrix, evidence dispositions, families, outcome
ordering, inference, guardrail thresholds, timing summaries, completion
requirements, Raw/report authorities, and Exact-Small Oracle authority are
unchanged. This document contains no observed result or decision.

The supersession closes two pre-observation authority gaps:

- each of the 100 successful cells replaces its derived Operational Projection
  v1 or v2 requirement with the complete
  `phase4_operational_measurement_publication_v1` authority; and
- the exact 102-entry canonical algorithm-budget roster in Representative
  Manifest v1 is authenticated before any execution result is collected.

The two descriptor-only fixed-query cells and two compiled-work-bound stress
cells do not require an operational publication. Their dispositions and
artifacts remain byte-for-byte inherited from v3.

## Canonical budget-roster checksum

The roster checksum uses Board IR v1 FNV-1a with domain
`APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V1`, followed by:

1. roster schema version `1` as u32;
2. representative corpus checksum as u64;
3. entry count as u64; and
4. every `(case_id u32, pool u32, algorithm_budget_checksum u64)` tuple in
   increasing case/pool order.

The frozen authority is corpus checksum `7311872938254494931`, 102 entries, and
roster checksum `13115713216042861392`. Validation reconstructs that checksum
from the live manifest, so manifest drift cannot be hidden by recomputing only
the protocol artifact checksum.

The compact JSON is authenticated with byte-stable FNV-1a domain
`APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V4` over the canonical JSON of all
preceding root fields. The validator first validates incorporated protocol v3,
then proves the operational-authority substitutions, canonical roster, and
unchanged logical matrix.
