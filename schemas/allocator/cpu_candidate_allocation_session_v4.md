# CPU Candidate-Allocation Session v4

> Superseded for production allocation by CPU Candidate-Allocation Session v5.

CPU Candidate-Allocation Session v4 is a child-authority supersession of
[`cpu_candidate_allocation_session_v3.md`](cpu_candidate_allocation_session_v3.md).
It preserves v3 ownership, configuration ordering, aggregate preflights,
atomicity, epoch records, fixed-point rules, terminal precedence, Multi-World
boundary, field order, and field widths.

Historical production accepted schema version `4`. Production Session v5
rejects versions `1` through `4` before routing or input consumption. V4
composes Targeted Regeneration Plan v2 and Targeted Regeneration Execution v5.
Its successful session identity domain is
`APGAR-CPU-CANDIDATE-ALLOCATION-SESSION-V4`.

The v3 runtime and prime fixtures remain `16234194786355953118` and
`1096655002616802593`. The v4 runtime fixture hashes to
`17242808134009288068`; its prime direct session-checksum fixture hashes to
`5346529622056530635`.

Any further child-authority, field, bound, order, width, limit, or decision
change requires another version.
