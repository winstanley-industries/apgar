# RouteCandidate Contract v1

RouteCandidate v1 is the immutable accepted two-terminal candidate consumed by
the Phase 3 store. This schema, rather than a C++ object layout, defines stable
identity, canonical serialization, signatures, bounds, and memory accounting.

## Header and associations

Every candidate contains:

- schema major/minor `1.0`;
- a nonzero stable 128-bit candidate ID and the exact Board IR net reference;
- the two intended terminal references in canonical net-terminal order;
- Board IR content hash, compiler-profile fingerprint, geometry-compiler
  version, routing-profile fingerprint, and rule-bucket identity;
- geometry schema/signature version `1` and resource schema/signature version
  `1`;
- complete CandidateGenerationPolicy v1 plus its verified identity;
- generator kind/version, backend kind, supported device-class identity,
  deterministic seed, batch identity, query identity, and candidate ordinal;
- canonical exact geometry, compressed resources, metrics, constraint
  assessment, 128-bit geometry and resource signatures, payload checksum, and
  declared deterministic logical bytes.

Association fields are independently compared with the supplied immutable
BoardSnapshot, CompiledBoard, request, and normalized policy. Two fields copied
from one untrusted candidate do not self-certify one another.

## Geometry v1

The ordered primitive union reserves these stable tags:

| Tag | Primitive | Phase 3 admission |
| ---: | --- | --- |
| 1 | exact line segment: layer, signed 64-bit endpoints | supported |
| 2 | through via: template ID, exact position, start/end layer | unsupported |

Unknown tags are incompatible. Arcs, branches, multipin trees, arbitrary
angles, and legalization degrees of freedom are not representable in v1.
Through-via encoding is reserved so a later exact via-template slice can add
validation without changing the primitive union; Phase 3 returns structured
`Unsupported` for every via primitive.

Canonical line geometry:

- is oriented from the first canonical terminal to the second;
- contains 1 through 1,000,000 primitives;
- uses valid Board IR signed 64-bit coordinates;
- is exactly contiguous and nondegenerate;
- uses only enabled H/V/45 headings;
- merges consecutive collinear same-layer segments; and
- has no layer change without a supported exact via primitive.

Canonicalization never repairs a disconnected route, invents geometry, or
weakens a rule. Each line's complete swept exact trace envelope is checked
against Board IR geometry and rules. Compiled legality and a generator/GPU
result are insufficient admission evidence.

## Resource footprint v1

The footprint is a sorted vector of canonical physical-edge spans. Each span
contains layer, signed 64-bit start lattice coordinate, canonical direction
`east`, `north-east`, `north`, or `north-west`, positive unsigned 32-bit edge
count, and positive unsigned 32-bit usage units. Adjacent compatible spans are
maximally coalesced. V1 usage is exactly one per traversed edge.

Admission expands the geometry against the associated CompiledBoard, derives
the collision-free atomic resource keys, recompresses them, and requires exact
equality with the supplied footprint. Hash equality is never accepted as
footprint equality.

## Metrics and constraints

Metrics v1 contains scalar policy cost, orthogonal step count, diagonal step
count, bend count, line-primitive count, via count, and exact axis-aligned DBU
length plus diagonal DBU projection. All arithmetic is checked. Scalar cost is
independently reconstructed from compiled costs and the complete policy.

ConstraintAssessment v1 records whether every supported hard constraint
passed, whether unsupported rules remain, the connected intended-terminal
count, and the exact-validation code. An accepted Phase 3 candidate must have
all supported constraints satisfied, no unsupported geometry primitive, and
exactly two intended terminals.

## Signatures, checksum, and collision behavior

Geometry and resource signatures are 128-bit pairs of independently
domain-separated FNV-1a64 hashes over their canonical encodings. Signature
version `1` fixes domains `APGAR-CANDIDATE-GEOMETRY-V1-A/B` and
`APGAR-CANDIDATE-RESOURCES-V1-A/B`. Signatures accelerate lookup only. Every
duplicate decision performs collision-safe equality over canonical geometry or
resources.

The payload checksum uses domain `APGAR-ROUTE-CANDIDATE-V1` over the entire
canonical candidate encoding except the checksum and declared-byte fields.
FNV-1a64 detects accidental drift; it is not cryptographic.

## Stable encoding and deterministic memory accounting

Canonical serialization uses fixed-width little-endian integers, one-byte
enums, explicit unsigned 64-bit vector/string byte counts, UTF-8 where text is
permitted, and the exact field order above. Unknown fields and incompatible
major versions are rejected. A reader may accept a newer minor only when all
added fields are explicitly length-delimited and declared ignorable; v1.0 has
no such extension fields.

`logical_bytes` equals the canonical serialized byte count, including vector
counts and every element, but excluding allocator metadata, capacities,
indices, mutexes, store metadata, and source Board/CompiledBoard storage. Count
and multiplication overflow is rejected before allocation or iteration.

