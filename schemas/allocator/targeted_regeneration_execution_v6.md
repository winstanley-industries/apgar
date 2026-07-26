# Targeted Regeneration Execution v6

**Status:** Current production execution authority.

Targeted Regeneration Execution v6 is a child-authority supersession of
[`targeted_regeneration_execution_v5.md`](targeted_regeneration_execution_v5.md).
It preserves v5 input ordering, hard limits, preflight equations, atomic
publication, failure observations, counters, terminal semantics, and field
encoding.

Production accepts execution schema version `6`, requires Targeted
Regeneration Plan v3, and rejects every earlier execution or plan version
before routing, publication, or input mutation.

Before the first CPU query, v6 additionally validates:

- `coverage_seed_target_count <= target_count`;
- the seed prefix and nonseed suffix each obey Plan v3 target order;
- target net identities are globally unique across both lanes;
- every retained target has a primary action and representable requested
  columns;
- every seed requests at least two columns and retains a primary action;
- seed action-zero resources are pairwise distinct;
- the complete plan target, action, column, conflict, and impact aggregates
  replay exactly;
- the complete Plan-v3 payload reproduces its authenticated plan checksum; and
- column zero remains the complete price-only policy while column one for each
  seed bans exactly its authenticated action-zero resource.

Plan-v3 provisional/full-rescan agreement is a planner construction invariant.
Execution validates the resulting authenticated prefix; it does not infer
coverage from pool size or from secondary actions.

The new plan authority is bound through the plan checksum under three new
domains:

- `APGAR-TARGETED-REGENERATION-CPU-BATCH-V6`;
- `APGAR-TARGETED-REGENERATION-EXECUTION-V6`; and
- `APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V6`.

The v5 success and failed-observation fixtures remain
`17041031294214445527` and `4030485784013825900`. With execution schema `6`,
the v6 domains, and otherwise the same typed fixtures, the v6 success and
failed-observation representations are `9384286314767353977` and
`15821825237325258190`. Activation MUST preserve field sensitivity to the plan
checksum and ordered column records.

V6 retains v5's `250,000,000` aggregate policy-resource-entry hard limit. Any
further child authority, field, bound, order, width, or decision change
requires another schema version.
