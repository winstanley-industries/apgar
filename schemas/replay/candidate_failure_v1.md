# Candidate Failure Replay Contract v1

Candidate failure replay v1 is a canonical UTF-8 line record with each field
exactly once in schema order, LF line endings, rejection of unknown fields, and
a final `checksum_fnv1a64` over all preceding bytes. Referenced fixture and
candidate payload bytes have independent checksums.

The artifact fixes fixture identity, Board/compiler/routing/rule associations,
candidate and policy schema versions, candidate/query identity, deterministic
seed and ordinal, generator/backend, canonical test-only fault, expected
rejection stage/code/invariant, and payload checksum. V1 supports candidate
resource-footprint corruption and batched query-ownership corruption.

Faults are applied only by replay/test decorators after a valid production
candidate or untrusted batch result exists. Production generator policy and
CUDA kernels contain no fault-injection control. Reproduction must execute the
normal exact candidate admission or untrusted batch validator and match the
stable rejection code and invariant identifier, not merely return any error.

