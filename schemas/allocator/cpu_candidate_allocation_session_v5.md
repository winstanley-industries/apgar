# CPU Candidate-Allocation Session v5

**Status:** Reserved successor; not executable until its implementation is
separately reviewed and activated.

CPU Candidate-Allocation Session v5 is a child-authority supersession of
[`cpu_candidate_allocation_session_v4.md`](cpu_candidate_allocation_session_v4.md).
It preserves v4 ownership, configuration ordering, aggregate preflights,
atomicity, epoch records, fixed-point rules, terminal precedence, Multi-World
boundary, field order, and field widths.

At this contract freeze, production still accepts schema version `4`.
Conforming activation advances production to schema version `5`, composes
Targeted Regeneration Plan v3 and Targeted Regeneration Execution v6, and
rejects versions `1` through `4` before routing, mutation, or input
consumption.

The successful session identity domain is
`APGAR-CPU-CANDIDATE-ALLOCATION-SESSION-V5`. Existing epoch plan and execution
checksums transitively bind the ordered coverage-seed prefix; no pool-size
priority, cross-epoch attempt ledger, or new session field is introduced.

The v4 runtime and prime fixtures remain `17242808134009288068` and
`5346529622056530635`. With schema version `5`, the v5 domain, and otherwise
the same typed preimages, the v5 runtime and prime fixtures are
`5545254251279532370` and `3982072683822198045`. Activation MUST preserve
sensitivity to the child plan and execution identities.

Any further child authority, field, bound, order, width, limit, or decision
change requires another version.
