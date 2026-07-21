# Phase 4 Imported Multi-Net Corpus v1

This schema pins the supported-rule imported seed used by the Phase 4 global
allocator evidence workflow. It establishes authentic Board IR and Multi-Net
Workload ancestry; it is not the representative evidence corpus by itself.

## Fixture source

- repository path:
  `tests/fixtures/phase4_supported_multinet_v1.kicad_pcb`
- SHA-256:
  `8c34f22a88debc213118b70d799e484d5a316104df0e0f68147f7f074f196075`
- exact byte length: `1781`
- canonical in-process FNV-1a identity: `8729721683359945012`
- strict KiCad schema: `20240108`
- imported signal layers: `F.Cu` routing ID `0`, `B.Cu` routing ID `31`

The adapter configuration is:

- canonical routable roster: `ROUTE_A`, `ROUTE_B`;
- default routing net: `ROUTE_A`;
- nominal width: `500000` database units;
- clearance: `500000` database units; and
- fixed adapter scale: `2000000` database units per millimeter.

Both routable nets use two rectangular through-hole pads. `BLOCKER` is a
non-routable net with one front-layer rectangular SMD pad. The normalized Board
contains three nets, four terminals, and nine owner-aware obstacles: eight from
the four through-hole pads on two copper layers and one from `BLOCKER`. Its
Board content hash is `229027575659763193`.

## Compiler and workload configuration

The compiler profile is:

- schema version `1`, geometry compiler version `1`;
- lattice origin `(0, 0)` and step `500000` database units;
- tile size `8` by `8` nodes;
- compilation ROI `[0, 0]` through `[40000000, 40000000]`, inclusive;
- one full-ROI active region on layer `0`;
- H/V/45-degree heading mask; and
- deterministic orthogonal, diagonal, and bend costs `1000`, `1414`, and
  `100`.

The compiler-profile fingerprint is `14092128556178973098`. The shared rule
bucket identity is `16006352534979059238`.

Both nets use explicit front-to-front requests. Canonical workload order is by
stable net reference, not the textual roster:

| Net | Net ID | Ordered start | Ordered goal | Routing-profile fingerprint |
| --- | ---: | --- | --- | ---: |
| `ROUTE_B` | `3033425279953999715` | `(36000000, 28000000, 0)` | `(4000000, 28000000, 0)` | `9653149663899239486` |
| `ROUTE_A` | `3033426379465627926` | `(4000000, 12000000, 0)` | `(36000000, 12000000, 0)` | `10553003216368397518` |

Workload limits are exactly two nets, `13122` cumulative compiled nodes, and
`67108864` retained compiled host bytes. Each context represents `6561` nodes.
The Multi-Net Workload v1 checksum is `13031419002947588674`. Spec and roster
input order are nonsemantic. Fixture-coordinate or terminal-UUID changes must
change the relevant Board/workload replay identities.

## Required acceptance

- Every roster pad is both a terminal and owner-aware per-layer obstacle.
- The version-1 builder checks the exact byte length and in-process fixture
  identity before parsing. Semantic and normalization-preserving source drift
  both fail with a source-identity mismatch.
- Each prepared net ignores only its own pad obstacles and retains foreign pad
  copper as blocking geometry.
- Both independently prepared front-layer contexts are reachable with CPU A*,
  and every reconstruction passes exact validation.
- The fixture SHA-256, exact byte length, in-process fixture identity, Board hash, compiler
  fingerprint, rule bucket, ordered requests, routing-profile fingerprints,
  compiled-node total, and workload checksum match the literals above.

This seed does not satisfy the hundreds/thousands-net, 4/8/16 pool,
family-level improvement, imported-case non-regression, equal-budget timing,
memory telemetry, or uncertainty requirements of Section 29.2.
