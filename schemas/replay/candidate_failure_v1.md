# Candidate Failure Replay Contract v1

Candidate failure replay v1 is a canonical UTF-8 line record with each field
exactly once in schema order, LF line endings, rejection of unknown fields, and
a final `checksum_fnv1a64` over all preceding bytes. Referenced fixture and
candidate payload bytes have independent checksums.

The artifact fixes fixture identity, Board/compiler/routing/rule associations,
candidate and policy schema versions, candidate/query identity, deterministic
seed and ordinal, generator/backend, canonical test-only fault, expected
rejection stage/code/invariant, and payload checksum. V1 supports candidate
resource-footprint corruption. Batched query ownership, workspace, telemetry,
memory-accounting, identity, and false-disconnection faults use the separate
GPU Candidate-Batch Replay v1 contract in `schemas/gpu_batch_replay/v1.md`.

The canonical candidate-admission record uses this exact field order:

1. format and replay schema version;
2. fixture path and fixture checksum;
3. Board IR, compiler-profile, geometry-compiler, RouteCandidate,
   candidate-geometry, candidate-resource, and policy schema versions;
4. generator kind/version, backend kind, and supported device class;
5. fault kind, planar layer, deterministic seed, candidate ordinal, batch ID,
   and query ID;
6. expected Board content hash, compiler-profile fingerprint, routing-profile
   fingerprint, rule-bucket identity, and policy identity;
7. expected 128-bit candidate ID and corrupted candidate payload checksum; and
8. expected lifecycle stage, rejection code, and invariant ID.

Integer values use canonical unsigned decimal with no sign or leading padding.
Names use the lowercase underscore spellings fixed by this schema. Unknown,
missing, duplicated, reordered, or trailing fields are rejected. The final
`checksum_fnv1a64` is not part of the checksummed payload and must be last.

Faults are applied only by replay/test decorators after a valid production
candidate or untrusted batch result exists. Production generator policy and
CUDA kernels contain no fault-injection control. Reproduction must execute the
normal exact candidate admission or untrusted batch validator and match the
stable rejection code and invariant identifier, not merely return any error.
