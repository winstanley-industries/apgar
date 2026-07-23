# Phase 4 Statistical Decision Protocol v2

Protocol v2 is a narrow authority-only supersession of the frozen Statistical
Decision Protocol v1. It incorporates the exact canonical v1 protocol by schema
and artifact checksum. The representative manifest, 104-cell logical matrix,
families, outcome ordering, inference, guardrail thresholds, timing statistics,
and completion requirements are unchanged.

The supersession repairs one authority contradiction discovered while adding
same-execution rejection telemetry. The exact, held-out, and imported groups
contain 78 cells whose outcome, timing, and rejection partition must originate
from the same telemetry-aware Wire-v2 execution. Their effective requirements
therefore replace:

- Raw Evidence v1 with Same-Run Raw Evidence v2;
- Per-Net Report Publication Join v1 with its future v2 join; and
- Operational Projection v1 with its future v2 projection.

Same-Run Decision Telemetry v1 remains required. Calibration, fixed-query, and
stress cells retain their frozen Raw-v1 authority. The effective successful
matrix is therefore 78 same-run Raw-v2 cells plus 22 legacy Raw-v1 cells; the
noncalibration closure contains 78 and 4 respectively.

The compact JSON is authenticated with byte-stable FNV-1a domain
`APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V2` over the canonical JSON of all
preceding root fields. The validator must first validate the incorporated v1
protocol, then apply only the exact group substitutions above and reconstruct
all counts. Because this supersession changes authority only, it cannot alter
an outcome threshold or rescue a failed guardrail.
