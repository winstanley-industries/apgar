# CPU Candidate-Allocation Session v3

CPU Candidate-Allocation Session v3 is a narrow child-authority supersession
of
[`cpu_candidate_allocation_session_v2.md`](cpu_candidate_allocation_session_v2.md).
It preserves v2 ownership, configuration ordering, aggregate preflights,
atomicity, epoch records, fixed-point rules, terminal precedence, and
Multi-World boundary.

Production accepts schema version `3`; versions `1` and `2` are rejected.
The session composes Targeted Regeneration Execution v4 instead of v3, thereby
accepting its `250,000,000` aggregate policy-entry hard limit. The session's
one-epoch projection, CandidateStore byte/work checks, exact-plan rechecks, and
all other limits remain unchanged.

The successful session identity domain is
`APGAR-CPU-CANDIDATE-ALLOCATION-SESSION-V3`. It retains the v2 field order and
widths, including the complete execution configuration and every nested v4
execution checksum. Any further child-authority, field, order, width, limit, or
decision change requires another version.

The complete v3 runtime representation fixture hashes to
`16234194786355953118`; its prime direct session-checksum fixture hashes to
`1096655002616802593`.
