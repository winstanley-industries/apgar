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

Stable one-byte provenance tags are:

| Generator | Tag |
| --- | ---: |
| CPU A* | 0 |
| CUDA heuristic frontier | 1 |
| CUDA heading-aware sweep | 2 |

| Backend | Tag |
| --- | ---: |
| CPU | 0 |
| CUDA | 1 |

Unknown provenance tags are incompatible. A CPU A* generator must name the CPU
backend; both CUDA generators must name the CUDA backend.

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
- merges consecutive collinear same-layer segments;
- has no layer change without a supported exact via primitive;
- visits each same-layer path vertex at most once;
- keeps every nonconsecutive same-layer swept trace pair at exact centerline
  distance greater than or equal to the nominal trace width, so boundary
  equality is legal; and
- does not traverse the same canonical physical edge more than once.

Nonconsecutive centerline crossings, point self-touches, and swept trace
distance below the nominal width are rejected during exact validation with
`candidate.geometry.swept_self_overlap.v1`. Positive-length collinear overlap
is represented by repeated physical edges and retains the more specific
resource-stage `candidate.resources.reused_edge.v1` diagnostic.

The deterministic self-clearance broad phase inspects at most `1,000,000`
segment pairs per admission. A pair counts after the canonical x-bound sweep
selects it and before layer, adjacency, or y-bound filters. Reaching the next
pair rejects with `BudgetExhausted` and
`candidate.geometry.self_clearance_pair_budget.v1`; the diagnostic records the
fixed limit, attempted count, and current primitive witness. This bound is part
of candidate geometry schema v1 compatibility behavior.

Canonicalization never repairs a disconnected route, invents geometry, or
weakens a rule. Each line's complete swept exact trace envelope is checked
against Board IR geometry and rules. Compiled legality and a generator/GPU
result are insufficient admission evidence.

## Resource footprint v1

The footprint is a sorted vector of canonical physical-edge spans. Each span
contains layer, signed 64-bit start lattice coordinate, canonical direction
`east`, `north-east`, `north`, or `north-west`, positive unsigned 32-bit edge
count, and positive unsigned 32-bit usage units. Adjacent compatible spans are
maximally coalesced in ascending physical-resource-key order. East,
north-east, and north spans advance in their named direction; north-west spans
advance south-east between stored canonical edge sources so their keys remain
ascending. V1 usage is exactly one per traversed edge. A repeated
physical edge is rejected with `candidate.resources.reused_edge.v1` rather
than encoded with multiplicity: all Phase 3 transition costs are nonnegative,
so a shortest-path candidate containing such a loop can be strictly simplified
or replaced by an equal-cost loop-free candidate.

Admission expands the geometry against the associated CompiledBoard, derives
the collision-free atomic resource keys, recompresses them, and requires exact
equality with the supplied footprint. Hash equality is never accepted as
footprint equality.

## Metrics and constraints

Metrics v1 contains both intrinsic base cost and scalar policy cost,
orthogonal step count, diagonal step count, bend count, line-primitive count,
via count, and exact axis-aligned DBU length plus diagonal DBU projection. All
arithmetic is checked. Intrinsic base cost uses only the associated compiled
orthogonal, diagonal, and bend costs; it deliberately excludes request-local
surcharges and resource penalties so candidates generated under different
policies remain comparable. Scalar policy cost is independently reconstructed
from those compiled costs and the complete request-local policy.

ConstraintAssessment v1 records whether every supported hard constraint
passed, whether unsupported rules remain, the connected intended-terminal
count, and the exact-validation code. An accepted Phase 3 candidate must have
all supported constraints satisfied, no unsupported geometry primitive, and
exactly two intended terminals.

The exact-validation byte tag is `passed=0`, `unsupported_geometry=1`,
`invalid_geometry=2`, or `exact_rule_violation=3`. Unknown values are
incompatible.

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
enums and booleans, explicit unsigned 64-bit vector/string byte counts, and
UTF-8 where text is permitted. The payload field order is exactly:

1. schema major, schema minor, candidate-ID high, candidate-ID low;
2. net entity ID/generation, followed by both terminal entity ID/generation
   pairs in canonical net order;
3. Board content hash, compiler-profile fingerprint, geometry-compiler
   version, routing-profile fingerprint, and rule-bucket identity;
4. geometry schema version and resource schema version;
5. complete CandidateGenerationPolicy v1 in that schema's canonical order,
   then verified policy identity;
6. generator tag, generator version, backend tag, supported-device-class
   length/string, deterministic seed, batch ID, query ID, and candidate
   ordinal;
7. geometry element count and tagged primitives in order;
8. resource-span count and spans in order;
9. scalar policy cost, intrinsic base cost, orthogonal steps, diagonal steps,
   bends, line primitives, vias, axis-aligned DBU length, and diagonal DBU
   projection;
10. supported-hard-constraints flag, unsupported-rules flag, connected intended
    terminal count, and exact-validation tag; and
11. geometry-signature high/low and resource-signature high/low.

The payload checksum follows this payload, then `logical_bytes` follows the
checksum. Neither field participates in the payload checksum; both fixed-width
fields participate in logical-byte accounting. Unknown fields and incompatible
major versions are rejected. A reader may accept a newer minor only when all
added fields are explicitly length-delimited and declared ignorable; v1.0 has
no such extension fields.

`logical_bytes` equals the canonical serialized byte count, including vector
counts and every element, but excluding allocator metadata, capacities,
indices, mutexes, store metadata, and source Board/CompiledBoard storage. Count
and multiplication overflow is rejected before allocation or iteration.

`RouteCandidateTest.CanonicalV1IdentitySignaturesChecksumAndBytesHaveGoldenValues`
is the v1 conformance vector. It fixes candidate identity, both signatures,
payload checksum, and logical bytes over a payload containing policy resource
actions, UTF-8 text, a line, and the reserved through-via primitive. Changing
one of those values requires a compatible schema decision rather than an
implementation-only rewrite.
