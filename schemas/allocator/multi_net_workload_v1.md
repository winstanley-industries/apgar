# Multi-Net Workload v1

Multi-Net Workload v1 is the canonical Phase 4 roster of authentic routable
nets from one immutable Board IR snapshot. It prevents repeated policies for
one net from being presented as board-level evidence.

## Input and preparation

A build request contains:

- schema version `1`;
- one exact `BoardSnapshot`;
- one compiler profile;
- a nonempty bounded list of unique per-net routing specifications; and
- positive net, cumulative compiled-node, and cumulative compiled-host-byte
  bounds no larger than 1,000,000 nets, 100,000,000 nodes, and 8 GiB.

Each specification names an actual Board IR net with exactly two actual
terminals, a routing profile validated by `PrepareRoutingProfile`, and start and
goal layers. The specification list is not semantic input order. Duplicate net
references fail before compilation.

Board IR v1 retains its original `routing_profile` field as the default profile
for source compatibility. Phase 4 does not relabel that profile. Instead, every
additional net profile is independently normalized and validated against the
same immutable BoardSnapshot. The workload owns those prepared profiles as
versioned rule context.

## Prepared net context

Every canonical net context owns:

- the normalized routing profile;
- a CompiledBoard built for that exact profile and source snapshot;
- the exact two-terminal route request; and
- the routing-profile fingerprint.

The CompiledBoard retains the profile, including net identity, because static
obstacles owned by the routed net are treated differently from foreign static
obstacles. Its rule bucket remains the net-independent width/clearance/layer/
heading identity. A request must match the profile's exact net and terminal
centers and must pass the normal compiled-board/request association validators.
The explicit-profile exact movement seam is source-private and consumes only a
profile already prepared into this trusted context.

All contexts share one Board content hash, compiler-profile fingerprint, and
geometry-compiler version. Contexts are sorted by `(net entity ID,
generation)`. Lookup and serialized identity never depend on object addresses.

## Checksum

The workload checksum is 64-bit FNV-1a with offset basis
`14695981039346656037`, prime `1099511628211`, and length-prefixed domain
`APGAR-MULTI-NET-WORKLOAD-V1`. It uses fixed-width little-endian unsigned
integers, two's-complement unsigned encoding for signed coordinates, no
padding, and an LE `u64` byte length before the domain bytes. Starting from the
offset basis, encode:

1. the length-prefixed domain string;
2. schema `u32`, Board content hash `u64`, compiler-profile fingerprint `u64`,
   geometry-compiler version `u32`, and canonical net count `u64`; and
3. for every canonical net: net ID `u64`, generation `u32`, routing-profile
   fingerprint `u64`, rule-bucket identity `u64`, start x/y `i64`, goal x/y
   `i64`, start layer `u32`, and goal layer `u32`.

The representation-level golden fixture uses header values `(1, 2, 3, 5)` and
two records with pairwise-distinct prime values from `11` through `89`, with
negative start/goal y coordinates. Its checksum is
`15373136221856123448`. The checksum is a replay identity, not a cryptographic
integrity proof. Any encoding change requires a schema version change.

## One-world binding

A one-world request may bind this immutable workload. When bound, it must carry
exactly one candidate pool for every workload net; an unroutable or exhausted
net is represented by an explicit empty pool. Every candidate must match its
net context's routing-profile fingerprint and rule-bucket identity. The world
records the workload checksum.

CandidateStore binds the common Board/compiler association once and the
routing-profile/rule-bucket association independently for each exact net pool.
An unrelated authentic pool cannot change publication inspection or duplicate
comparison work for a touched pool.

## Bounds and failures

Schema/count/profile/request/compile/association failures are structured.
Owned copies and per-net compilation occur inside a `bad_alloc`/`length_error`
failure envelope with static allocation-free fallback diagnostics. Cumulative
compiled nodes and retained compiled host bytes are checked after each bounded
per-net compilation and before its context is published. No partial workload
is published. This schema defines the authentic workload boundary; negotiated
prices, regeneration schedules, multi-world state, and Phase 4 benchmark
claims are separate later slices.
