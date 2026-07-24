# Targeted Regeneration Execution v5

Targeted Regeneration Execution v5 is a child-authority supersession of
[`targeted_regeneration_execution_v4.md`](targeted_regeneration_execution_v4.md).
It preserves v4 input ordering, hard limits, preflight equations, atomic
publication, failure observations, counters, terminal semantics, and field
encoding.

Production accepts execution schema version `5` and requires Targeted
Regeneration Plan schema version `2` before routing or publication. Execution
versions `1` through `4`, and plans from any other schema, are rejected.

The new plan authority is bound through the plan checksum under three new
domains:

- `APGAR-TARGETED-REGENERATION-CPU-BATCH-V5`;
- `APGAR-TARGETED-REGENERATION-EXECUTION-V5`; and
- `APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V5`.

The v4 success and failed-observation fixtures remain
`162220596377167553` and `10718127143049464138`. With execution schema `5`
and the v5 domains, the same fixtures hash to `17041031294214445527` and
`4030485784013825900`.

V5 retains v4's `250,000,000` aggregate policy-resource-entry hard limit. Any
further child-authority, field, bound, order, width, or decision change
requires another schema version.
