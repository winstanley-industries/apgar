# Deterministic One-World Allocation Contract v2

One-World Allocation v2 preserves the v1 scoring, selection, resource
accounting, bounds, error, and canonical-order contracts and adds an authentic
Multi-Net Workload v1 replay binding. Implementations continue to accept v1
requests without a workload; a v1 request that supplies a workload is rejected.

## Workload input and validation

A v2 request may bind one immutable Multi-Net Workload v1 over the same exact
Board content hash, compiler-profile fingerprint, and geometry-compiler
version. When bound, the canonical pool roster must exactly equal the
workload's canonical net roster. Every nonempty pool must match that net
context's routing-profile fingerprint and rule-bucket identity; every exhausted
net remains present as an explicit empty pool. A v2 request without a workload
records workload checksum zero and remains useful for source-compatible
single-slice execution, but it is not authentic multi-net evidence.

For each nonempty workload pool, the canonical immutable candidate geometry
must also begin at the prepared request's exact start coordinate/layer and end
at its exact goal coordinate/layer. Routing-profile and rule-bucket association
alone do not bind endpoint layers. A candidate admitted for a different exact
request on the same authentic net is an association mismatch.

## Output and checksum

The v2 world adds `workload_checksum` immediately after the shared association
header. It is zero without a workload and otherwise equals the immutable
workload checksum. All other fields and tags retain their v1 meanings and
canonical order.

The checksum is 64-bit FNV-1a with offset basis `14695981039346656037` and
prime `1099511628211`. It uses fixed-width little-endian integers,
two's-complement unsigned encoding for signed coordinates, no padding, and an
LE `u64` byte length before every UTF-8 string. Vectors start with an LE `u64`
count; booleans and optional-presence bytes use zero/one. Starting at the
offset basis, encode:

1. length-prefixed domain string `APGAR-ONE-WORLD-V2`;
2. world schema `u32`, Board hash `u64`, compiler-profile fingerprint `u64`,
   compiler version `u32`, workload checksum `u64`, price iteration `u32`,
   default capacity `u32`, and intrinsic weight `u64`;
3. the v1 selection vector encoding;
4. the v1 resource vector encoding; and
5. the v1 trailing selected/no-candidate/overuse/cost/work counters.

The representation-level golden fixture uses the exact field values declared
by the v1 fixture plus workload checksum `67` and schema `2`. Its checksum is
`6143441878869362651`. Any further externally visible field or byte-order
change requires another schema version.
