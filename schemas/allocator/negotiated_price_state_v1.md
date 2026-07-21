# Negotiated Price State v1

Negotiated Price State v1 is the bounded CPU-reference update between one
immutable One-World Allocation v2 result and the next immutable Price Snapshot
v1. It records present congestion separately from accumulated historical
congestion and never mutates candidate pools, selected candidates, occupancy,
or a prior snapshot.

## Identity and configuration

State v1 binds:

- schema version `1`;
- one nonzero Board/compiler association;
- one nonzero Multi-Net Workload v1 checksum;
- one stable checksum of the exact immutable capacity model;
- positive present and history steps;
- one positive maximum price per resource;
- a positive iteration bound no larger than 1,000,000; and
- a positive sparse-record bound no larger than 1,000,000.

The initial state is iteration zero with no records, predecessor-state checksum
zero, and source-world checksum zero. Every later state records both the exact
predecessor-state checksum and the source-world checksum, so saturating price
paths cannot erase replay-chain identity. A state produces a Price Snapshot
with the same iteration and one explicit `total_price` entry for each state
record. Unlisted resources have zero price.

The capacity-model checksum uses the same fixed-width FNV-1a encoding under
domain `APGAR-RESOURCE-CAPACITY-MODEL-V1`: capacity schema `u32`; Board hash
`u64`; compiler fingerprint `u64`; compiler version `u32`; default capacity
`u32`; override count `u64`; then each canonical override as layer `u32`,
lattice x/y `i64`, direction `u8`, and capacity `u32`. The representation
fixture using distinct prime header/resource fields, mixed signed coordinates,
and capacities zero and one has checksum `8594357387633211926`.

## Update formula

For each canonical resource in the validated One-World resource accounting,
using widened unsigned arithmetic:

```text
raw_present = observed_overuse * present_step
raw_history = previous_history + observed_overuse * history_step
present     = min(raw_present, maximum_price)
history     = min(raw_history, maximum_price)
raw_total   = present + history
total       = min(raw_total, maximum_price)
```

Present price therefore resets to zero when current overuse disappears;
history persists and increases only while overused. A record whose total is
zero is omitted. Each retained record stores the observed overuse plus three
independent clamp bits. `clamped_resource_count` counts records for which any
bit is true. Saturation is never silent.

The update rejects a sparse roster beyond its configured bound and rejects
aggregate present, history, or total sums outside unsigned 64-bit replay
fields. It increments the unsigned 32-bit iteration exactly once and rejects
an update at the configured iteration bound.

## Input validation

The previous state must have a valid checksum, canonical unique resources,
bounded values, exact aggregate totals, the same association as the capacity
model, and the exact capacity-model checksum. Capacity overrides plus price
records must also fit the one-million-record Price Snapshot v1 hard bound. The
world must be One-World v2 for the supplied immutable workload and price
iteration and must reproduce its stored world checksum.

Selection and resource vector sizes are rejected before either vector is
hashed or traversed. The world default capacity must equal the exact capacity
model. World resources are then independently checked for canonical strict
order, capacity/default-override identity, exact `max(usage - capacity, 0)`
overuse, aggregate overuse totals, and exact explicit prices from the previous
state. Every prior price and every capacity override must remain present in the
world resource union.

Finally, the CPU One-World reference is rerun over the complete original
One-World request: every candidate pool and alternative, allocator limit,
intrinsic weight, supplied workload, exact capacity model, and reconstructed
prior price snapshot. Candidate admission/profile/rule associations, pool
membership, minimum-score selection, selection metadata and costs,
compressed-footprint occupancy, all work counters, and selected-world
aggregates must reconstruct exactly. Publicly mutable usage and selection
fields are not trusted merely because a caller can recompute the documented
replay checksum.

Nested capacity/price factories and the One-World reference already translate
allocation exceptions into `ResourceExhausted`. Price update preserves that
classification and its stable invariant/detail instead of relabeling resource
exhaustion as invalid state or world data.

## Replay encoding

The state checksum is 64-bit FNV-1a with offset basis
`14695981039346656037`, prime `1099511628211`, fixed-width little-endian
integers, two's-complement signed coordinates, booleans encoded as one byte,
and the length-prefixed domain `APGAR-NEGOTIATED-PRICE-STATE-V1`.

Encode the following header in order: schema `u32`; Board hash `u64`; compiler
fingerprint `u64`; compiler version `u32`; workload checksum `u64`;
capacity-model checksum `u64`; present step `u64`; history step `u64`; maximum
price `u64`; maximum iterations `u32`; maximum records `u64`; iteration `u32`;
predecessor-state checksum `u64`; source-world checksum `u64`; record count
`u64`; clamped-record count `u64`; and aggregate present, history, and total
prices as three `u64` values.

For every canonical record encode layer `u32`, lattice x/y `i64`, direction
tag `u8`, present/history/total/observed-overuse as four `u64` values, then the
present/history/total clamp bits.

The representation fixture using distinct prime header/record fields from `2`
through `127`, mixed signed coordinates, and mixed clamp bits has checksum
`2909744929431564223`. Any encoding or formula change requires a new schema
version.

## Deliberate boundary

State v1 consumes one completed CPU-reference world at a time. It does not
choose regeneration hotsets, mutate CandidateStore, retain multiple worlds,
model exact combined-route conflicts, or claim Phase 4 board-level
improvement. Those are subsequent versioned slices.
