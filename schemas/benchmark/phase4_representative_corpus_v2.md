# Phase 4 Representative Global-Allocation Corpus v2

This schema freezes the post-v1-remediation representative corpus. Its
canonical corpus checksum is `4182833841936446798`. It is additive: every v1
descriptor, builder result, checksum domain, and preserved negative-matrix
artifact remains unchanged.

Corpus v2 exists because the first complete matrix proved that v1 synthetic
routes could pass compiled-route reconstruction while failing immutable
candidate admission. The v2 construction makes exact-admission legality part
of the corpus contract. It does not weaken the exact validator or reinterpret
the v1 result.

## Frozen descriptor roster and observation boundary

The v2 roster has the same 42-role shape and requested work as v1, but uses
disjoint IDs and fresh synthetic seeds:

- exact-oracle cases `10100`-`10102`, one six-net case per synthetic family,
  pool size `4`, and at most `4096` candidate products;
- calibration cases `10200`-`10201`, `10210`-`10211`, and
  `10220`-`10221`, two 64-net development seeds per family at pool sizes
  `4/8/16`;
- held-out cases `11000`-`11007`, `11100`-`11107`, and
  `11200`-`11207`, eight fresh instances per family, with 256 nets for
  portal-channel and pin-field cases and 384 nets for fragmented-maze cases;
- fixed-query controls `12000`-`12004`, retaining `1x1024`, `1024x1`,
  `256x4`, `128x8`, and `64x16`;
- stress cases `13000`-`13002`, retaining the `1024/2048/4096` distinct-net
  ladder at pool size `4`; and
- imported guardrail `14000`, reusing the authenticated imported fixture
  ancestry under a fresh descriptor and case identity.

The primary decision pool size is `8`; `4` and `16` remain mandatory
sensitivity regimes. Exact-oracle and calibration cases are the only
development surfaces. The v2 held-out descriptors, seeds, and schedules are
frozen before outcome observation. Executing or inspecting a v2 held-out
allocation outcome before the confirmatory acquisition invalidates the held-out
set and requires another fresh, versioned set.

The forthcoming v2 representative and workload-roster manifests are separate
authorities. Neither v1 manifest may be overwritten or treated as accepting
this roster.

## Exact-safe synthetic geometry

V2 keeps nominal width in `[1,3]` database units and clearance in `[0,1]`, but
maps every abstract coordinate onto an `8`-DBU compiler lattice. One distinct
track is therefore farther apart than the maximum swept self-clearance and
foreign-terminal clearance envelopes. Scaling both exact coordinates and
`CompilerProfile.lattice_step` preserves the abstract graph, represented-node
scale, route costs, and canonical lattice resource identities.

Every reachable terminal is a private degree-one leaf in its intended
topology, while a declared-disconnected fragmented start remains an isolated
point. Terminal leaves are separated by at least one scaled track from every
foreign transit segment. The corpus continues to model synthetic terminals as
abstract connection points rather than fictitious physical pad obstacles;
production exact candidate admission remains authoritative for unintended
terminal checks.

Portal-channel motifs retain the v1 private backbones, alternative channels,
and one canonical contested portal per two-net motif. Scaling separates the
constrained route's nonconsecutive arms without changing portal incidence.

Pin-field motifs retain one board-wide portal ladder and the connected
base-route conflict graph. Each constrained goal moves from the portal itself
to a private downward spur. Flexible terminals move off their base/private
branch points onto short leaf spurs. Flexible routes still use portals `i` and
`i+1`; constrained route `i` still uses portal `i`; banning the flexible
route's contested portal still exposes its private rail.

Fragmented-maze channels use a shortcut-proof serpentine. From the private
backbones, the channel follows:

`(-8,y)->(-5,y)->(-5,y+4)->(-2,y+4)->(-2,y)->(3,y)`

`(3,y)->(3,y-4)->(6,y-4)->(6,y)->(8,y)`.

The contested portal remains `(0,y)->(1,y)`. The constrained start and goal
are leaves on `(0,y-4)->(0,y)` and `(1,y)->(1,y+4)`. Gaps between
nonconsecutive same-row pieces prevent active-region endpoints from creating
implicit shortcut edges. Fragmented flexible base routes therefore retain a
multi-bend turn-complexity witness, and exactly `floor(net_count/32)` flexible
starts remain declared disconnected.

The synthetic Board adapter version is `"2"`. Its entity count is
`2 layers + N nets + 2N terminals = 3N+2`. The builder preflights that exact
formula before materialization. Keeping terminal exclusions topological avoids
turning the per-net compilation of an `O(N)` graph into cubic all-obstacle
clearance work. All other public hard and default resource bounds remain the
v1 structural limits.

## Identity domains and acceptance

Descriptor fingerprints use `APGAR-PHASE4-CASE-DESCRIPTOR-V2`; the corpus uses
`APGAR-PHASE4-REPRESENTATIVE-CORPUS-V2`; deterministic counters use
`APGAR-PHASE4-CORPUS-COUNTER-V2`; and built cases use
`APGAR-PHASE4-REPRESENTATIVE-CASE-V2`. Corpus-owned failure invariants use
`.v2`. Nested subsystem failures retain the originating subsystem's versioned
invariant.

Required corpus tests pin:

- the unique 42-case roster, checksum, disjoint v1/v2 IDs, and fresh
  non-imported seeds;
- the exact v1 corpus and exact-case goldens unchanged;
- v1 lookup rejection of v2 IDs and v2 lookup rejection of v1 IDs;
- degree-one reachable terminal topology, adapter version, lattice step, and
  `3N+2` preflight;
- deterministic exact-case checksums;
- full generated-candidate construction and exact admission for every base
  route in all three exact families;
- preservation of the fragmented multi-bend witness;
- an exact-admissible portal alternative when the contested portal is banned;
  and
- zero exact-validation rejection columns when production CPU pool preparation
  runs at pool `4` on all exact cases and pool `16` on one calibration case per
  family.

These tests deliberately do not execute held-out allocation outcomes. Passing
this corpus contract fixes the exact-invalid-input defect but does not by itself
prove allocator improvement or complete Phase 4. Completion still requires a
fresh, clean-commit matrix whose independently validated decision publication
reports both `phase4_exit_status=passed` and `phase4_complete=true`.
