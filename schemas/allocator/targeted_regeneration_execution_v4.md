# Targeted Regeneration Execution v4

> Superseded for production execution by Targeted Regeneration Execution v5.

Targeted Regeneration Execution v4 is a narrow bound-and-identity
supersession of
[`targeted_regeneration_execution_v3.md`](targeted_regeneration_execution_v3.md).
It preserves v3 input ordering, preflight equations, atomic publication,
failure observations, counters, terminal semantics, and deliberate boundary.

Production accepts schema version `4`; versions `1`, `2`, and `3` are rejected.
The maximum aggregate policy-resource-entry configuration is raised from
`100,000,000` to `250,000,000`. Every other scalar hard limit is unchanged.
The raised bound admits the frozen Phase 4 case-3000 conservative projection
of `134,218,752` entries without weakening the per-policy
`1,000,000`-resource limit or any CandidateStore byte/work authority.

Because the accepted configuration domain and schema version change, v4 uses
the identity domains:

- `APGAR-TARGETED-REGENERATION-CPU-BATCH-V4`;
- `APGAR-TARGETED-REGENERATION-EXECUTION-V4`; and
- `APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V4`.

All fields retain the v3 declaration order and widths. Any further field,
order, width, limit, or decision change requires another version.

The v4 representation fixture hashes to `162220596377167553`; its failed
observation fixture hashes to `10718127143049464138`.
