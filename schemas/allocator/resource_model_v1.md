# Allocator Resource Capacity Model v1

Resource Capacity Model v1 is the first Phase 4 capacity and price vocabulary.
It deliberately reuses RouteCandidate v1's exact-admitted physical-edge
footprints so the allocator does not depend on CAD-editor objects or trust a
generator-supplied congestion map.

## Identity and units

A physical resource key is the collision-free tuple:

| Field | Type |
| --- | --- |
| `layer` | unsigned 32-bit routing layer ID |
| `lattice_x` | signed 64-bit canonical edge-source x |
| `lattice_y` | signed 64-bit canonical edge-source y |
| `direction` | stable one-byte tag: east `0`, north-east `1`, north `2`, or north-west `3` |

Reverse movement names the same positive-direction physical edge. Unknown and
noncanonical directions are incompatible. The implied positive-direction
endpoint must also remain inside signed 64-bit lattice coordinates; a source at
the arithmetic boundary that points outside the domain is not a physical edge.

Capacity and usage are dimensionless unsigned routing units. Candidate v1
physical-edge spans consume their declared positive `usage_units` on each edge
in the span. Current exact-admitted planar candidates use one unit. Because a
v1 key is one atomic centerline edge rather than a multi-track corridor, v1
capacity is binary: zero or one. Capacity zero records an allocator-unavailable
resource but does not retroactively make an individually exact-legal candidate
invalid. Wider or fractional-sharing resources require a later tagged schema.

## Capacity model

The schema contains:

- schema version `1`;
- the nonzero Board IR content hash, compiler-profile fingerprint, and geometry
  compiler version of the resource lattice;
- one binary unsigned 32-bit default capacity applied to every admitted candidate
  resource not named by an override; and
- zero or more unique `(resource, binary unsigned 32-bit capacity)` overrides.

Override input order is not semantic. Consumers sort by the complete resource
key and reject duplicate keys rather than selecting one duplicate by arrival
order. The default is part of replay identity even when no selected candidate
uses an unlisted resource. Construction crosses a versioned creation-factory boundary
that accepts the actual Board Snapshot and its Compiled Board, verifies their
content-hash, compiler-version, and compiler-profile association, and derives
the model identity from those artifacts. The resulting associations and
records are immutable and cannot be restamped by changing fields on an
existing model.

## Price snapshot

A Price Snapshot v1 contains:

- schema version `1`;
- the same Board IR/compiler association as the capacity model and candidates;
- an unsigned 32-bit allocator iteration; and
- zero or more unique `(resource, unsigned 64-bit price_per_usage_unit)`
  records.

Unlisted resources have price zero. Price zero records are permitted and
remain externally visible. Snapshot input order is not semantic. Prices are
immutable for one selection phase; candidate generators and selectors receive
the snapshot by const reference and never update occupancy through it. A new
iteration creates a new immutable snapshot through the same boundary. The
price factory inherits the exact association from its immutable capacity model;
it does not accept caller-selected association labels.

## Bounds and arithmetic

One-world configuration declares positive bounds for net pools, candidates,
capacity/price records, and expanded candidate-resource uses. V1 hard maxima
are:

| Quantity | Maximum |
| --- | ---: |
| net pools | 1,000,000 |
| candidates | 1,000,000 |
| capacity plus price records | 1,000,000 |
| expanded candidate-resource uses | 100,000,000 |

Configuration cannot raise those maxima. Cost multiplication, usage
accumulation, and world totals are checked in wider arithmetic and fail rather
than wrap an unsigned 64-bit externally visible value. Capacity and price
factories reject an individually oversized record vector before canonical
sorting or ownership transfer. Lvalue factory calls make their bounded owned
copy inside the structured allocation-failure envelope; rvalue calls transfer
ownership under the same result contract. Allocation additionally enforces the
configured combined capacity and price record budget.

## Output accounting

Resource Usage v1 is canonically ordered by resource key and records whether a
capacity override was explicit, effective capacity, selected usage, positive
overuse `max(usage - capacity, 0)`, whether a price was explicit, and effective
price. The explicitness bits preserve zero-price and default-equal override
records in replay identity. The output contains the union of:

- every explicit capacity override;
- every explicit price; and
- every resource used by a selected candidate.

This permits an independent CPU recomputation from immutable selected
candidate footprints. Signature or candidate-ID equality is never substituted
for expansion and addition of canonical usage. The reference implementation
sweeps compressed-span boundary events per canonical resource line, so a shared
long corridor is expanded once per unique output edge rather than once per
selected candidate. Scratch scales with span boundaries plus unique output
resources, not with the total expanded-use count.

## Deliberate boundary

V1 models physical planar edges only. It does not define portals, via sites,
escape channels, fractional width sharing, exact combined-route collision,
resource refinement, historical price updates, or legalization. Later schemas
may add tagged resource families without reinterpreting a v1 physical key.
