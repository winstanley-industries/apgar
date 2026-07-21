# Phase 4 Representative Global-Allocation Corpus v1

This schema freezes the descriptor roster and one-case-at-a-time construction
contract for Phase 4 global-allocation evidence. Its canonical corpus checksum
is `7311872938254494931`. The checksum covers every descriptor field and the
raw byte/FNV identities of the imported seed.

`phase4_representative_manifest_v1.json` is the machine-readable evidence
projection of this contract for every descriptor whose declared pool intersects
`4/8/16`. Successful entries pin descriptor, Board, workload, capacity, and
built-case identities. A parallel per-case/pool roster pins the canonical
algorithm-configuration budget checksum used by the raw evidence validator.
Each successful entry also freezes the exact compiled-node, logical-host-byte,
active-region, Board-entity, and workload-net requirements that its caller
limits must independently cover.
Stress entries `3001` and `3002` instead pin their
deterministic compiled-work-bound totals and carry zero built-object identities;
they cannot be accepted as successful raw cells.

The corpus declaration is metadata only. A builder materializes exactly one
requested case, Board snapshot, Multi-Net Workload, binary physical-edge
capacity model, and contested-resource roster. Large cases are never
materialized as a side effect of enumerating or building another case.

## Descriptor roster

Every descriptor binds a case ID, source, family, role, integer seed, requested
and declared-reachable net counts, ordered candidate-pool schedule, feature
mask, exact-oracle product bound, stress target, and known-unmapped-conflict
declaration. It also declares whether the case has one conflict graph coupling
all routable nets rather than a disjoint union of local motifs.

The 42 cases are:

- exact-oracle cases `100`, `101`, and `102`: one six-net case for each
  synthetic family, pool size `4`, and at most `4096` candidate products;
- calibration cases `200`-`201`, `210`-`211`, and `220`-`221`: two 64-net
  development seeds per family, with pool sizes `4`, `8`, and `16`;
- held-out cases `1000`-`1007`, `1100`-`1107`, and `1200`-`1207`: eight
  independently seeded instances per family. Portal-channel and pin-field
  cases contain 256 distinct nets; fragmented-maze cases contain 384;
- fixed-query controls `2000`-`2004`: `1x1024`, `1024x1`, `256x4`, `128x8`,
  and `64x16`, where the first coordinate is distinct nets and the second is
  requested candidates per net;
- stress cases `3000`, `3001`, and `3002`: requested tiers of `1024`, `2048`,
  and `4096` distinct nets at pool size `4`, with a declared target of `4096`;
  and
- imported guardrail `4000`: the exact authenticated two-net fixture from
  `phase4_imported_multi_net_corpus_v1`, with pool sizes `4`, `8`, and `16`.

The primary decision pool size is `8`. Pool sizes `4` and `16` are mandatory
sensitivity regimes, not optional tuning rows. Calibration seeds may be used
for implementation and parameter development. Held-out seeds must not be
relabelled or replaced after their outcomes are observed.

## Synthetic Board and workload construction

Every synthetic Board has two routable signal layers but uses front-layer
planar requests. Each net has a unique EntityRef, name, pair of terminal
EntityRefs, ordered point connection regions, coordinates, and route request.
Synthetic terminals represent abstract connection points rather than physical
pad copper, so the generator does not emit fictitious pad obstacles. Imported
cases retain the owner-aware pad obstacles required by their source schema.

Portal-channel and fragmented-maze instances repeat isolated two-net
bottleneck motifs:

1. A flexible net has a base route through channel zero and alternative
   channels connected by private backbones.
2. A constrained net approaches the channel-zero portal from disjoint point
   chains. Its base route shares the single canonical portal edge with the
   flexible net.
3. Motifs are separated in the sparse active-region union. The common capacity
   model assigns binary capacity one to every physical edge. The portal list is
   an authenticated diagnostic roster; it does not weaken ordinary edges or
   introduce a nonphysical aggregate resource.

`portal_channels_v1` uses horizontal channel motifs.
`pin_field_crossbar_v1` retains its frozen family identifier but uses one
board-wide portal ladder rather than unavoidable common buses. Each flexible
net's shorter base path enters a shared rail through portal `i` and leaves
through portal `i+1`; the constrained net for rung `i` terminates immediately
across portal `i`. This incidence chain makes the base-route conflict graph
connected across every routable net. Every flexible net also has a longer
private rail whose vertical interval is separated from its neighbors. Banning
portal `i` therefore gives an independently checked edge-disjoint assignment:
all constrained nets keep their base paths and all flexible nets take their
private rails. The exact integer rotation `(x,y)->(y,-x)` gives the pin-field
family its vertical orientation without floating-point input; rotation is not
claimed as turn-complexity variation.
`fragmented_maze_v1` replaces each channel with a multi-turn orthogonal detour
and disconnects exactly `floor(net_count/32)` flexible starts. Point-only
disconnected starts remain represented so route-request admission succeeds and
CPU A* returns the structured disconnected outcome.

An integer-only counter hash keyed by schema tag, descriptor seed, and motif or
net counter varies channel spacing, private-route length, detour height, and
rule bucket. Nominal width is in `[1,3]` database units and clearance in
`[0,1]`. Synthetic cases deliberately allow only horizontal and vertical
headings, preventing crossing-diagonal conflicts from escaping the v1
physical-edge capacity vocabulary. Layers and headings stay within the
supported M1 planar contract. No standard-library random distribution or
completion order affects the generated Board.

The family feature mask collectively and explicitly covers route length,
occupancy, run fragmentation, turn complexity, reachability, rule bucket, and
ROI size. Fragmented-maze geometry is the turn-complexity witness. Each case
declares `known_unmapped_exact_conflicts=false`; later
combined-route evidence must fail with resource-refinement-required if that
declaration is disproved.

Only pin-field descriptors declare `globally_coupled_conflict_graph=true`.
Portal-channel and fragmented-maze cases intentionally disclose their repeated
local components. The frozen evidence decision must include a qualified
pin-field result among any two families used for a Phase 4 success claim;
repeated local motifs alone cannot establish board-wide global allocation.

## Bounded lazy construction

The public v1 hard bounds are 4096 nets, 100 million cumulative compiled
nodes, 8 GiB of deterministic compiled-host payload, one million active
regions, and one million Board entities. Default case limits further cap active
regions at 250,000 and Board entities at 100,000. Callers may only lower these
bounds.

The builder checks the case ID, all limits, the requested net count, and the
Board-entity count before materialization. It then builds the sparse geometry,
validates Board IR, prepares every authentic per-net compiled context through
Multi-Net Workload v1, verifies every declared portal exists in the compiled
lattice, and constructs Resource Capacity Model v1. Allocation and container
failures become stable resource-exhausted results.

Synthetic cases preflight one compiled view and require uniform per-net
compiled node and logical-host-byte ownership. A work-bound rejection records
which limit or limits prevent the first unpreparable net, both required and
configured full-case totals, the maximum net prefix that fits both bounds, and
that first unpreparable authentic net. A full-case total may exceed a secondary
limit without labelling it as the limiting bound when an earlier net already
hits the primary limit. The first-unpreparable EntityRef must be present and
nonzero; every named limiting bound must have `required > configured`.
Imported cases derive the same witness by walking their already authenticated
per-net contexts. A generic workload-build error may not launder caller-selected
work-bound exhaustion.

Because Multi-Net Workload v1 owns one complete compiled view per net, the
2048- or 4096-net tiers may hit the cumulative node, logical-host-byte, process
memory, or wall guard before completion. Evidence must retain the requested
tier, achieved tier or count, exact limiting bound, and structured failure. It
must not replace distinct nets with more alternatives for one net.

## Identity and acceptance

The descriptor fingerprint covers every descriptor field. The built-case
checksum covers the descriptor fingerprint, Board hash, workload checksum,
compiled nodes and logical bytes, capacity schema and associations, capacity
records, and declared contested resources. Case order, net order, terminal
order, or policy completion order is nonsemantic after canonical Board and
workload normalization.

Required tests pin:

- the complete unique 42-case roster and corpus checksum;
- eight held-out instances for each of three families;
- the exact `4/8/16` schedules, fixed-query controls, and stress ladder;
- field sensitivity of descriptor fingerprints;
- exact CPU reachability and reconstruction for all three six-net cases;
- exact base-route incidence on every declared contested portal;
- a connected pin-field base-route conflict graph and an independently
  reconstructed zero-overuse assignment using production CPU A*;
- the declared disconnected count for a fragmented calibration case;
- authentic construction of a 256-net held-out case;
- imported Board/workload ancestry through case `4000`; and
- preflight bounds plus allocation/container failure conversion.

This corpus slice does not create candidate pools, prove candidate diversity,
execute an allocator workflow, demonstrate the 2048/4096 tiers, compare equal
budgets, establish family-level improvement, or complete Phase 4.
