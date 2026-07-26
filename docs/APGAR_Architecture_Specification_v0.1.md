# APGAR

**A Pretty Good Auto Router**

## Architecture Specification

*GPU-native candidate generation, global allocation, and exact PCB legalization*

> **Project name**
>
> APGAR is intentionally modest in name and ambitious in architecture: a router should earn a high score through measurable connectivity, legality, manufacturability, and route quality rather than claiming universal optimality.

| **Document** | APGAR Architecture Specification |
| --- | --- |
| **Version** | 0.1 - Project kickoff baseline |
| **Status** | Draft / living specification |
| **Date** | July 17, 2026 |
| **Primary audience** | Router, geometry, GPU, CAD integration, and verification engineers |
| **Normative language** | MUST, SHOULD, and MAY follow RFC-style meanings |

## 1. Executive Summary

APGAR is a CAD-neutral autorouting library designed to turn GPU throughput into higher-quality PCB layouts. Its primary architectural choice is to separate exact PCB geometry from disposable GPU search representations. The GPU is used to generate diverse route candidates, negotiate board-wide resource allocation, and accelerate geometric analysis; exact geometry and the host CAD engine remain the final authorities for legality.

> **Core thesis**
>
> APGAR should optimize combinations of reusable route candidates across the whole board, not merely accelerate one shortest-path query at a time.

The first release will focus on single-ended nets, through vias, H/V/45-degree routing, a small set of rule buckets, incremental compilation, exact candidate validation, and a CPU reference pathfinder. Specialty routing for differential pairs, buses, BGA escape, length tuning, and signal-integrity-aware objectives is explicitly designed into interfaces but deferred.

### 1.1 Architecture at a glance

```text
CAD Adapter -> Exact Board IR -> Geometry Compiler
                              |-> Sparse tile atlas
                              |-> Directional edge masks
                              |-> Distance / margin fields
                              |-> Topological portals
                              |-> Via-template feasibility

Compiled Board + Routing Request
        -> Candidate Generators
        -> Candidate Store
        -> Global Allocator / Negotiated Prices
        -> Exact Legalizer + Incremental DRC
        -> Host CAD Validation + Commit
```

### 1.2 Architectural priorities

1. Correctness and reproducibility before throughput.
1. Candidate diversity before raw candidate count.
1. Board-level optimization before per-net greediness.
1. Sparse and incremental representations before monolithic full-board graphs.
1. Exact geometric validation before CAD commit.
1. Benchmarkable subsystem boundaries before premature unification.

## 2. Scope and Non-Goals

### 2.1 Version 0.x scope

- A reusable C++ library with a CUDA-first compute backend.
- A neutral exact PCB Board IR with integer coordinates.
- Import/export adapters, initially targeting KiCad-compatible workflows.
- Conservative, tile-based GPU search-field compilation.
- CPU reference A*/Dijkstra and at least two GPU route generators.
- Multi-candidate storage, deduplication, scoring, and global selection.
- Exact route legalization and incremental DRC for supported primitives.
- Deterministic replay, checkpoints, telemetry, and benchmark tooling.

### 2.2 Deferred capabilities

- Full copper-pour repouring and plane synthesis.
- Native curved arbitrary-angle routing beyond line/arc legalization.
- Differential-pair, bus, BGA escape, and length-tuning production support.
- Electromagnetic field solving or sign-off signal-integrity analysis.
- Placement optimization.
- A standalone end-user PCB editor.

### 2.3 Explicit non-goals

- Claiming globally optimal routing for industrial boards.
- Treating a raster or lattice as the authoritative board representation.
- Depending on one GPU pathfinding algorithm for all geometries.
- Replacing the host CAD engine's final connectivity and DRC authority.
- Embedding KiCad-specific object semantics into the routing core.

## 3. Terminology and Normative Conventions

| **Term** | **Definition** |
| --- | --- |
| Exact Board IR | Canonical CAD-neutral representation of physical geometry, rules, stack-up, nets, and locked copper. |
| Compiled Board | Disposable GPU-oriented representation derived from the Exact Board IR for a specific compiler profile. |
| Rule bucket | A group of nets or obstacles that share equivalent routing width and clearance behavior. |
| Resource | A capacity-bearing routing abstraction, such as a tile corridor, directional edge span, portal, or via site. |
| Candidate | A reusable possible route or route tree for one net, with geometry, resource footprint, and quality metrics. |
| World | One concurrent global-allocation state with its own prices, selections, and objective weights. |
| Guide | A selected candidate or corridor that may still require exact geometric legalization. |
| Legalization | Conversion or adjustment of a guide into exact CAD primitives satisfying supported rules. |
| Authoritative validation | Final validation by APGAR exact DRC and, before commit, the host CAD engine. |

MUST identifies a mandatory requirement; SHOULD identifies a strong default that may be overridden with documented rationale; MAY identifies an optional capability.

## 4. System Requirements

### 4.1 Functional requirements

- **FR-001** The core library MUST accept a CAD-neutral Exact Board IR without requiring a live editor process. *Rationale: Enables headless testing, cloud execution, and multiple CAD adapters.*
- **FR-002** Every emitted route MUST be traceable to a routing request, configuration fingerprint, seed, compiler profile, and candidate provenance record. *Rationale: Required for reproducibility and debugging.*
- **FR-003** The compiler MUST conservatively represent supported geometry: it MUST NOT mark an exactly illegal movement as legal. *Rationale: False-free search space is a correctness defect.*
- **FR-004** The system MUST support partial boards containing locked and pre-routed copper. *Rationale: Interactive and incremental workflows are primary use cases.*
- **FR-005** The router MUST support multiple candidate generators behind one interface. *Rationale: No single routing primitive dominates all PCB geometries.*
- **FR-006** The allocator MUST be able to select candidates without rerunning geometric pathfinding. *Rationale: Candidate reuse is the central performance hypothesis.*
- **FR-007** The legalizer MUST validate exact geometry before a route can be committed. *Rationale: Search fields are approximate.*
- **FR-008** The session layer MUST support incremental invalidation and recompilation after localized edits. *Rationale: Full rebuilds would undermine interactive use.*
- **FR-009** A CPU reference implementation MUST exist for every correctness-critical GPU primitive. *Rationale: Provides oracle behavior on small and medium tests.*
- **FR-010** The library MUST expose machine-readable telemetry and benchmark results. *Rationale: Algorithm selection must be evidence-driven.*

### 4.2 Quality attributes

| **Attribute** | **Target / architectural response** |
| --- | --- |
| Correctness | Exact integer geometry, conservative compilation, dual DRC validation, invariant checks. |
| Determinism | Seeded randomness, fixed-point costs where practical, stable tie-breaking, recorded hardware/backend. |
| Scalability | Sparse tile atlas, ROI batching, procedural adjacency, compressed candidate resources. |
| Incrementality | Generational object IDs, dirty tile halos, dependency tracking, persistent sessions. |
| Portability | Backend abstraction; CUDA-first implementation with clean isolation from core semantics. |
| Observability | Per-stage timing, kernel counters, memory accounting, candidate-diversity metrics, replay bundles. |
| Extensibility | Versioned IR and plugin-like route-generator/specialty-router interfaces. |

## 5. Architecture Decisions

> **ADR-001 - ACCEPTED: Exact geometry is authoritative**
>
> The Exact Board IR and exact checker define correctness. GPU rasters, fields, graphs, and guides are compiled caches and may be regenerated or replaced.

> **ADR-002 - ACCEPTED: Candidate generation is separated from global allocation**
>
> Route generators produce reusable candidates. The allocator chooses combinations under shared resource prices and may request additional candidates around hotspots.

> **ADR-003 - ACCEPTED: Regular adjacency is procedural**
>
> APGAR will not materialize a CSR edge for every regular grid neighbor. Neighbor identities and costs are derived arithmetically; sparse structures are reserved for portals and exceptional transitions.

> **ADR-004 - PROPOSED: C++ core with CUDA-first backend**
>
> The core will target modern C++ and a CUDA backend first. GPU APIs are isolated behind runtime interfaces so HIP or other backends can be evaluated without changing routing semantics.

> **ADR-005 - ACCEPTED: Integer global coordinates**
>
> Global board coordinates use signed 64-bit integer database units. GPU kernels use tile-local compact coordinates and explicit transforms.

> **ADR-006 - PROPOSED: Hybrid raster and topological routing**
>
> Dense directional fields handle constrained detailed search; sparse portals and free-space topology identify long-distance and homotopically distinct alternatives.

> **ADR-007 - ACCEPTED: Host CAD remains final authority**
>
> Before commit, generated primitives are checked by the host CAD engine. APGAR's own DRC enables speed and isolation but does not silently replace editor semantics.

## 6. System Context and Boundaries

### 6.1 External actors

| **Actor** | **Interaction** |
| --- | --- |
| EDA editor | Supplies board state, routing request, locks, rules, and user intent; previews or commits results. |
| Headless CLI/service | Loads serialized Board IR, runs route sessions, exports solutions and replay artifacts. |
| Host CAD DRC/connectivity engine | Performs final validation and returns structured violations. |
| Benchmark harness | Runs deterministic corpora, gathers metrics, compares baselines and configurations. |
| GPU runtime/driver | Executes compiled kernels and reports device capabilities and failures. |

### 6.2 Trust boundaries

- Imported CAD data is untrusted until normalized and validated.
- GPU outputs are untrusted until bounds, predecessor, and geometry invariants pass.
- Serialized sessions are versioned and checksummed.
- A host CAD rejection overrides APGAR's internal legality assessment.

## 7. Component Architecture

| **Module** | **Primary responsibility** |
| --- | --- |
| board_ir | Canonical entities, IDs, units, rules, net constraints, geometry ownership. |
| geometry | Exact predicates, polygon operations, offsetting, distance and intersection queries. |
| geometry_compiler | Rule buckets, sparse tiles, masks, fields, portals, via feasibility, dependency graph. |
| gpu_runtime | Device discovery, allocators, streams, kernels, scans, queues, profiling, error containment. |
| route_generators | Sweep, frontier, topological, line-explore, multipin, and future specialty engines. |
| candidate_store | Candidate geometry, provenance, metrics, signatures, indices, persistence, pruning. |
| global_allocator | Resource capacities, prices, world states, selection, conflict analysis, column generation. |
| legalizer | Guide-to-geometry conversion, local shifts, jogs, corner styles, via relocation, shove hooks. |
| drc | Incremental broad phase, exact narrow phase, supported-rule checking, violation explanations. |
| session | Incremental state, dirty propagation, checkpoints, cancellation, deterministic replay. |
| cad_adapters | Import normalization, preview, host validation, transaction-safe commit. |
| bench | Synthetic generators, corpus manifests, metric collection, statistical comparison. |

### 7.1 Dependency rules

- board_ir MUST NOT depend on a CAD adapter or GPU backend.
- geometry_compiler MAY depend on geometry and board_ir, but MUST NOT depend on a specific editor.
- route_generators MUST consume versioned compiled views, not mutable CAD objects.
- global_allocator MUST consume candidate/resource abstractions, not exact editor primitives.
- legalizer and drc MAY access exact geometry but MUST return changes as transactions.
- cad_adapters MUST translate rather than leak editor object identity into the core.

## 8. Exact Board IR

### 8.1 Identity and versioning

Every persistent entity uses a stable 64-bit ID scoped to one board document. Mutations increment an entity generation. References contain both ID and expected generation where stale use would be dangerous. Serialized Board IR includes a schema version, unit scale, content hash, and adapter metadata.

```cpp
using DbCoord = int64_t;
using EntityId = uint64_t;
using Generation = uint32_t;

struct EntityRef {
    EntityId id;
    Generation generation;
};

struct Point64 {
    DbCoord x;
    DbCoord y;
};
```

### 8.2 Core entities

| **Entity** | **Required fields** |
| --- | --- |
| Board | outline/cutouts, stack-up, global rules, object tables, revision, coordinate units |
| Layer | physical order, type, thickness, material metadata, routability flags |
| Net | terminal references, net class, constraints, priority, criticality |
| Terminal | padstack geometry, allowed connection regions, component and pin identity |
| Track | net, layer, width profile, line/arc primitives, lock state |
| Via | net, via template, position, start/end layer, lock state |
| Obstacle | layer geometry, class, ownership, keepout and interaction flags |
| RuleSet | clearance matrix, width ranges, via rules, neck-down permissions, region overrides |
| ConstraintGroup | differential, bus, length, topology, layer, return-path metadata |
| Region | polygonal scope and rule/behavior overrides |

### 8.3 Geometry primitives

- Integer points and vectors.
- Closed polygon sets with holes and normalized winding.
- Finite line segments with width or width profile.
- Circular arcs represented by center/radius/start/sweep or equivalent exact-safe form.
- Circles, capsules, rounded rectangles, slots, and polygonal pad shapes.
- Layer-aware pad stacks and via pad stacks.

- **IR-001** Geometry normalization MUST reject self-intersecting outlines unless the adapter resolves them into valid polygon sets.
- **IR-002** All imported floating-point coordinates MUST be quantized once at the adapter boundary and never repeatedly converted.
- **IR-003** Every obstacle MUST retain enough provenance to explain a collision to the user.

### 8.4 Rules and rule buckets

The Exact Board IR preserves the full rule system. The compiler derives rule buckets for search efficiency. Bucket equivalence is defined by identical movement width, relevant pairwise clearances, allowed layers, and via-template eligibility for the current compilation mode.

```cpp
struct RoutingProfile {
    DbCoord nominal_width;
    DbCoord minimum_width;
    RuleClassId moving_class;
    LayerMask allowed_layers;
    ViaTemplateMask allowed_vias;
    HeadingMask allowed_headings;
};

struct RuleBucketKey {
    RoutingProfile profile;
    ObstacleInteractionSignature interactions;
};
```

## 9. Geometry Compiler

### 9.1 Compiler contract

The compiler transforms an immutable Board IR snapshot into one or more immutable Compiled Board views. A view is parameterized by grid profile, heading set, rule-bucket set, tile dimensions, and compiler version.

- **GC-001** A compiled legal edge MUST imply that its swept exact trace envelope is legal against all represented static obstacles.
- **GC-002** Compiler conservatism MUST be measurable by comparing compiled reachability with exact local search on generated microcases.
- **GC-003** Compiled artifacts MUST carry the Board IR content hash and compiler-profile fingerprint.

### 9.2 Multi-resolution sparse tile atlas

APGAR uses a coarse full-board capacity map plus sparse detailed tiles. Fine tiles are instantiated around terminals, obstacles, narrow channels, selected topological corridors, and congestion hotspots. Each tile includes a halo large enough to evaluate the largest supported swept envelope relevant to that tile.

| **Tile field** | **Purpose** |
| --- | --- |
| tile descriptor | origin, scale, layer, rule bucket, generation, halo, residency |
| directional legality | bit masks for H/V/diagonal movements and optional turn transitions |
| static cost | length, layer preference, margin, region penalties |
| dynamic price | present and historical congestion prices |
| distance/margin | clearance guidance and channel-center preference |
| terminal seeds | source/destination connection states |
| portal links | sparse links to adjacent resolution levels or topological structures |
| via feasibility | legal via-template sites and span metadata |

### 9.3 Directional edge legality

Node occupancy alone is insufficient. For every supported heading, the compiler evaluates the swept capsule or exact envelope between adjacent samples. Diagonal corner cutting is prohibited unless the entire diagonal movement envelope is legal.

> **Conservatism rule**
>
> False-blocked space may reduce route quality and must be measured. False-free space is a correctness defect and must fail tests.

### 9.4 Distance and margin fields

Distance fields guide routing toward corridor centers and estimate whether a region can accommodate a route profile. They are advisory rather than authoritative because they inherit rasterization error. Exact Euclidean distance transforms are GPU-suitable; the Parallel Banding Algorithm is a relevant implementation reference [R6].

### 9.5 Topological portals

- Obstacle-corner and tangent portals.
- Tile-boundary gates with capacity estimates.
- Pad/escape landing portals.
- Via-site and layer-transition portals.
- Navigation-mesh or decomposed free-space adjacency.
- User or adapter supplied preferred corridors.

Topological routes define broad homotopy choices and detailed regions of interest. They do not certify exact route legality.

### 9.6 Via-template compilation

```cpp
struct ViaTemplate {
    ViaTemplateId id;
    LayerId start_layer;
    LayerId end_layer;
    DbCoord drill;
    SmallVector<LayerShape, 16> conductive_shapes;
    SmallVector<LayerShape, 16> keepout_shapes;
    uint32_t process_flags;
    CostVector base_cost;
};
```

- **GC-010** Via feasibility MUST be evaluated using complete padstack and keepout geometry across every affected layer.
- **GC-011** Full all-pairs layer transitions MUST NOT be materialized as regular graph edges.
- **GC-012** Sparse legal via sites MAY be generated from clearance maxima, portals, pad escapes, and user constraints.

### 9.7 Incremental invalidation

Every compiled tile records dependencies on exact entities and neighboring halo tiles. Mutations generate dirty bounding regions per layer and interaction class. Dirty regions expand by the maximum affected clearance and invalidate only overlapping artifacts.

```text
BoardDelta
  -> normalize affected entity bounds
  -> expand by rule-dependent influence radius
  -> mark exact spatial-index nodes dirty
  -> mark compiled tile interiors + halos dirty
  -> invalidate dependent portals and via sites
  -> rebuild asynchronously
  -> atomically publish new compiled generation
```

## 10. GPU Runtime and Execution Model

### 10.1 Runtime responsibilities

- Capability discovery and backend selection.
- Stream and event management.
- Long-lived device memory pools.
- Tile residency and upload scheduling.
- Prefix scan, segmented reduction, sort, compaction, and queue primitives.
- Kernel launch tracing and deterministic error capture.
- Cancellation at safe batch boundaries.

### 10.2 Memory tiers

| **Tier** | **Content** | **Lifetime** |
| --- | --- | --- |
| Host exact | Board IR, exact geometry, spatial indices | board/session |
| Host compiled cache | compressed tiles, portals, serialization buffers | session |
| Device persistent | resident tiles, resource prices, candidate indices | session |
| Device batch | distance labels, frontiers, predecessors, temporary reductions | route batch |
| Device world | selection arrays, world prices/deltas, objective accumulators | allocator run |

- **GPU-001** The runtime MUST report peak and current memory by subsystem.
- **GPU-002** Out-of-memory conditions MUST be recoverable through smaller batches, tile eviction, or CPU fallback.
- **GPU-003** Kernel errors MUST invalidate the current result and produce a replayable diagnostic bundle.

### 10.3 Deterministic computation

- Integer or fixed-point primary routing costs where practical.
- Stable composite keys for atomic minimum operations.
- Deterministic compaction and stable ordering at externally visible boundaries.
- Counter-based random number generation keyed by board hash, net ID, candidate ordinal, and world ID.
- Hardware/backend identity recorded with results.

> **Determinism level**
>
> Bit-identical results are required on the same backend and supported device class. Cross-backend semantic equivalence is required; bit identity across CUDA and future backends is not initially required.

## 11. Common Routing State and Cost Model

### 11.1 Search state

The generic detailed state includes physical layer, tile/cell coordinate, incoming heading, and optional finite state for special constraints. State extensions must be bounded and explicitly declared so memory use can be estimated.

```cpp
struct RouteState {
    LayerId layer;
    TileId tile;
    LocalCoord x;
    LocalCoord y;
    Heading heading;
    uint16_t extension_state; // generator-specific finite state
};
```

### 11.2 Cost vector

APGAR preserves a quality vector on candidates and uses scalarized fixed-point costs inside individual searches. Search scalarization is configuration-controlled and recorded. Hard illegality is never represented as a large finite cost.

| **Metric** | **Examples** |
| --- | --- |
| Geometric | length, bend count, corner style, margin |
| Layer/via | layer preference, via count, span, microvia stack cost |
| Congestion | present price, historical price, scarce portal use |
| Electrical proxy | reference discontinuity, coupled length, stub penalty |
| Manufacturing | neck-down, acute-angle risk, drill family, testability proxy |
| Constraint error | length error, skew, ordering or topology deviation |

### 11.3 Resource model

Resources are capacity-bearing abstractions used by the allocator. They are intentionally coarser than exact geometry, but candidate footprints may include fine edge spans where needed.

- Directional tile-edge spans.
- Coarse corridor bins.
- Topological portals.
- Via sites and via-stack templates.
- Pad escape channels.
- Tuning-zone capacity.
- Special shared constraints such as bus lanes.

- **RES-001** Every candidate MUST provide a sorted, canonical resource footprint.
- **RES-002** Resource capacity and usage units MUST be explicit and compatible.
- **RES-003** Exact geometric collisions discovered after allocation MUST be projectable back to one or more resources or trigger resource refinement.

## 12. Candidate Generator Interface

```cpp
class ICandidateGenerator {
public:
    virtual GeneratorCapabilities capabilities() const = 0;

    virtual CandidateBatch generate(
        const CompiledBoardView& board,
        const NetRouteRequest& request,
        const PriceSnapshot& prices,
        const CandidateGenerationPolicy& policy,
        CancellationToken cancel) = 0;
};
```

### 12.1 Generator requirements

- **GEN-001** A generator MUST declare supported headings, via models, terminal types, and multipin behavior.
- **GEN-002** A generator MUST return failure explanations distinguishing
  unreachable, work-bound-exceeded, resource-exhausted, unsupported, and
  cancelled.
- **GEN-003** Every candidate MUST include provenance sufficient to reproduce it.
- **GEN-004** Generators SHOULD support banned or penalized resource sets to produce diverse alternatives.
- **GEN-005** Generators MUST NOT mutate global resource usage while searching.
- **GEN-006** Generator identity and backend provenance MUST be derived at a trusted producer-adapter boundary, not accepted from caller-selected metadata. A CPU adapter MUST require opaque evidence sealed by the actual CPU A* result path and bound to its exact associations, policy, cost, and geometry before stamping CPU provenance; no test or production dependency may expose a route-evidence reseal operation. A GPU candidate adapter MUST require immutable opaque host-validation evidence bound to the batch, query, policy, immutable device view, and reconstructed route, plus authentication that the prepared view was produced by the exact final supported GPU-backend type, before it can stamp GPU provenance. The authentication decision MUST live in an always-linked core implementation; an optional backend library MUST NOT receive an otherwise-undefined public friend or seal-minting hook. Generic backends and wrappers remain ineligible even when they report GPU-looking metadata or forward work to a real GPU. Public result fields alone are not such evidence.

### 12.2 Heading-aware sweep router

The sweep router generalizes GAMER's scan-based shortest-path propagation to PCB heading states. GAMER demonstrates that rectilinear multisource-multidestination routing can be transformed into GPU-friendly prefix operations and achieved large maze-routing kernel speedups in VLSI contexts [R1]. APGAR will test horizontal, vertical, and diagonal segmented scans, followed by local heading-turn and via relaxations.

```text
repeat until convergence, destination bound, or round budget:
    segmented_scan(E/W states)
    segmented_scan(N/S states)
    segmented_scan(NE/SW states)
    segmented_scan(NW/SE states)
    relax_same_cell_heading_changes()
    relax_sparse_via_transitions()
    update_destination_bounds()
    compact_changed_regions()
```

- Best expected case: dense regular ROIs with long runs and moderate bend counts.
- Risk: many short obstacle-fragmented runs and highly tortuous paths may require excessive rounds.
- Prototype must compare exactness, path-cost gap, and examined-state work against CPU reference.

### 12.3 Bucketed frontier router

A frontier-based GPU router provides a complementary strategy for sparse exploration. Candidate algorithms include delta-stepping, integer-cost buckets, or multi-queue variants. The first implementation should prioritize deterministic behavior and bounded memory over theoretical generality.

- Best expected case: sparse reachable regions or strongly directed heuristics.
- Use compact active-state queues and duplicate-tolerant relaxation.
- Support ROI expansion when the initial corridor proves insufficient.

### 12.4 Topology-first router

The topology-first router enumerates portal paths representing distinct obstacle bypass choices, then invokes a detailed generator inside each corridor. InstantGR's DAG-based representation demonstrates how route alternatives and their resource footprints can expose more parallelism than bounding-box conflict tests [R2].

### 12.5 Adaptive line-exploration router

A gridless or semi-gridless generator should derive exploration points from obstacles, rules, and destination direction. The 3D LineExplore work is relevant evidence that continuous-space exploration can avoid fixed-grid resolution constraints and can natively incorporate layer transitions [R5]. APGAR should treat this as a candidate generator rather than adopting its one-pass routing paradigm as the whole-board optimizer.

### 12.6 Generator dispatcher

| **Feature** | **Dispatcher signal** |
| --- | --- |
| ROI occupancy density | Dense fields favor scans; sparse fields favor frontier/topological methods. |
| Run fragmentation | Short segmented runs reduce sweep efficiency. |
| Expected turn count | High turn complexity may favor frontier or topology-first search. |
| Portal dominance | Sparse portal graph favors topology-first. |
| Batch regularity | Similar ROI sizes and rule buckets favor GPU batching. |
| Candidate diversity deficit | Choose a generator with a different topology bias. |

- **DSP-001** Dispatcher decisions MUST be logged with features and selected policy.
- **DSP-002** The benchmark harness MUST support forcing any compatible generator to avoid self-confirming dispatch heuristics.

## 13. Candidate Representation and Store

```cpp
struct RouteCandidate {
    CandidateId id;
    NetId net;
    GeneratorId generator;
    Provenance provenance;

    CompressedRouteGeometry geometry;
    CompressedResourceFootprint resources;
    CostVector metrics;
    ConstraintAssessment constraints;
    Hash128 resource_signature;
    Hash128 geometry_signature;
};
```

### 13.1 Geometry representation

- Ordered line/arc/via primitives for two-terminal routes.
- Rooted branch representation for multipin route trees.
- Optional corridor envelopes and movable degrees of freedom for legalization.
- Canonical simplification of collinear segments and redundant waypoints.
- Exact integer endpoints after candidate validation.

### 13.2 Resource compression

- Run-length encoded directional edge spans.
- Sorted portal and via-site IDs.
- Coarse resource bitmap for fast overlap screening.
- Fine sparse list for exact allocator accounting.
- Optional per-resource fractional usage for capacity models wider than one track.

### 13.3 Diversity and deduplication

Candidate count is not a useful metric without diversity. APGAR maintains exact duplicate hashes and approximate similarity statistics. A candidate may be rejected when it adds no new resource footprint, topology, or useful Pareto tradeoff.

| **Diversity metric** | **Use** |
| --- | --- |
| Resource Jaccard overlap | Detect candidates competing for the same board resources. |
| Geometric overlap ratio | Detect nearly coincident paths with minor waypoint noise. |
| Portal sequence | Approximate homotopy/topological distinction. |
| Layer/via signature | Preserve meaningful layer and transition alternatives. |
| Metric dominance | Prune candidates worse on every objective with no unique feasibility benefit. |

- **CAN-001** Candidate stores MUST enforce per-net memory budgets.
- **CAN-002** Pruning MUST preserve candidates currently selected by retained world states.
- **CAN-003** A candidate rejected by exact DRC MUST retain a structured failure record to improve later generation.
- **CAN-004** Candidates produced concurrently for one deterministic invocation
  MUST cross one explicit stable admission transaction boundary. Within that
  transaction, insertion, diagnostics, ranking, deduplication, and pruning MUST
  be independent of worker completion order. Separate store calls are separate
  ordered mutations: their linearization order is part of the invocation input,
  so bounded retention is not required to be commutative across calls. A caller
  requiring schedule-independent publication MUST collect the outputs and use
  one batch; it MUST NOT race individual admissions and call that one
  deterministic invocation.

## 14. Global Allocator

### 14.1 Problem definition

For each routable net, the allocator selects one candidate while respecting resource capacities and minimizing a configurable board-level objective. Intermediate selections may overuse resources; feasibility is approached through negotiated prices, mirroring the useful PathFinder principle that temporarily infeasible shared-resource states can help explore the solution space [R3].

```text
score(net, candidate, world) =
    intrinsic_quality(candidate, world.weights)
  + sum(resource_price[world, r] * candidate.usage[r])
  + constraint_penalties(candidate, net, world)
```

### 14.2 Iterative selection

1. Select the minimum-score candidate for every net in each world.
1. Accumulate resource usage in parallel.
1. Measure over-capacity resources and exact-risk indicators.
1. Update present and historical prices.
1. Identify nets whose candidate sets are insufficient around expensive resources.
1. Request targeted new candidates under the current price snapshot.
1. Prune dominated candidates and continue until feasible, stalled, or budget exhausted.

The named Phase 4 traditional baseline is independently versioned by
`schemas/allocator/sequential_negotiated_baseline_v1.md`. It processes
canonical nets sequentially, rips up only the current net's prior route,
freezes a prospective congestion policy, runs bounded production CPU A*,
exact-admits at most one replacement, and commits it before the next net. It
retains no reusable alternatives. One-World accounting and negotiated-price
updates run only at complete sweep boundaries as independent oracles.

### 14.3 Multi-world execution

Multiple worlds share the immutable candidate pool and base resource model but maintain independent selections, price states, objective weights, perturbations, and update schedules. This allows APGAR to spend additional GPU capacity on board-level search rather than storing full independent pathfinding state for every world.

- **ALL-001** World state MUST be compact enough to run tens or hundreds of worlds for moderate candidate pools.
- **ALL-002** The allocator MUST retain a Pareto set of feasible or near-feasible worlds.
- **ALL-003** Price updates MUST be bounded and recorded to prevent silent numerical instability.

The first versioned multi-world CPU reference branches canonical schedules
from one authenticated negotiated-price state over one fixed complete
candidate-pool snapshot. Each branch runs the One-World and price-update CPU
references independently and retains compact checksum traces. Near-feasible
worlds enter an exact Pareto archive by selected-net count, total overuse, and
common unweighted intrinsic candidate cost; a separate stable lexicographic
choice identifies the Phase 4 decision world. Source candidates remain leased
before production pool authentication and until the retained-winner lease is
acquired. One fixed authentication pass is included in bounded work and replay
counters even when a known unmapped exact conflict short-circuits branch
execution. Interleaved column publication is deferred until a global freeze/
gather/publish/refresh epoch can prevent world or worker order from changing
the shared pool.

### 14.4 Column generation

Candidate generation acts as a pricing subproblem: nets touching high-price resources are rerouted against the latest price field to discover alternatives with lower reduced cost. The allocator is therefore not limited to its initial candidate pool.

- Hotset based on overused resources and affected nets.
- Resource bans or strong penalties to force topological novelty.
- Per-net candidate generation budget based on criticality and conflict impact.
- Stall detection when new candidates fail to improve diversity or reduced cost.

### 14.5 Parallel conflict safety

Generators read immutable price snapshots and do not directly commit global occupancy. Selection and usage accumulation occur in explicit phases, avoiding races caused by many nets mutating a shared congestion map during search. OrthoRoute is useful direct PCB prior art but routes nets sequentially on a shared congestion map while parallelizing each net's SSSP; APGAR's candidate/allocation split is intended to expose additional net-level and world-level parallelism [R4].

## 15. Multipin Nets

Multipin nets are represented as route trees. Fixed decomposition into independently optimized two-pin connections is not the canonical representation because it can discard useful topology.

1. Choose one terminal or existing branch set as the initial tree.
1. Run multisource-to-multidestination search from the tree to unconnected terminals.
1. Attach one or more terminals according to the generator policy.
1. Repeat under alternate root, order, Steiner, layer, and price perturbations.
1. Canonicalize the final tree and compute one resource footprint.

- **MP-001** A multipin candidate MUST maintain connectivity under branch simplification.
- **MP-002** Candidate comparison MUST account for shared trunk resources only once.
- **MP-003** Partial repair MAY replace a branch without regenerating the entire tree.

## 16. Exact Legalization

### 16.1 Legalizer contract

The legalizer converts a selected candidate guide into exact CAD primitives or returns structured conflicts. It operates transactionally: proposed edits are validated before replacing existing board state.

- Snap or translate segments within an allowed corridor.
- Move a via among feasible sites.
- Insert local jogs.
- Replace sharp corners with configured miters or arcs.
- Apply legal neck-downs near terminals.
- Invoke bounded shove operations on explicitly movable copper.
- Request candidate substitution or local regeneration.

- **LEG-001** Legalization MUST NOT silently change a net's hard constraints.
- **LEG-002** Every adjustment MUST remain within declared candidate degrees of freedom or trigger candidate re-evaluation.
- **LEG-003** Legalization failure MUST identify conflicting primitives, violated rules, and suggested repair scope.

### 16.2 Transaction model

```text
begin RouteTransaction
    stage removals / replacements
    stage new exact primitives
    run APGAR incremental DRC
    run host CAD validation (adapter)
    if both pass:
        commit atomically
    else:
        rollback and retain diagnostics
end
```

## 17. DRC and Geometric Verification

### 17.1 Two-level checking

| **Level** | **Role** |
| --- | --- |
| Compiled legality | Fast conservative rejection during search. |
| Exact APGAR DRC | Incremental validation and diagnostic generation for supported rules. |
| Host CAD validation | Authoritative final editor-specific connectivity and rule validation. |

### 17.2 Spatial acceleration

Exact checking uses per-layer spatial bins and hierarchical bounding volumes for broad-phase candidate generation, followed by exact or conservatively bounded narrow-phase predicates. OpenDRC provides relevant evidence for layer-wise BVHs, adaptive partitioning, and edge-based GPU kernels in physical verification [R7].

- Dirty-region broad phase.
- Batched segment-segment, point-segment, arc, pad, and via checks.
- Pairwise rule lookup using moving and obstacle classes.
- Connectivity checks at intended terminal and branch junctions.
- Violation objects containing exact witnesses and provenance.

- **DRC-001** Supported exact rules MUST have generated property tests around equality and one-unit boundary cases.
- **DRC-002** Unsupported rules MUST be declared before routing and delegated to the host, not assumed legal.
- **DRC-003** The internal checker and host checker disagreement MUST be retained as a regression artifact.

## 18. Incremental Session Architecture

### 18.1 Persistent session state

- Board IR snapshot and revision lineage.
- Exact spatial indices.
- Compiled tile generations and residency.
- Candidate pools and invalidation dependencies.
- Resource history and retained allocator worlds.
- Host adapter mapping and pending transactions.
- Reproducibility configuration and telemetry.

### 18.2 Delta classes

| **Delta** | **Likely response** |
| --- | --- |
| Component/pad moved | Invalidate terminal geometry, nearby tiles/portals, affected net candidates, exact spatial index. |
| Rule changed | Rebucket affected nets/obstacles; invalidate dependent compiled views and candidates. |
| Track locked/unlocked | Update obstacle/movable classification and local resource capacities. |
| Layer stack changed | Invalidate stack-dependent via templates, all compiled layer transforms, and affected candidates. |
| Net constraint changed | Invalidate that net's candidate metrics or geometry depending on constraint type. |
| Local manual route edit | Treat edited copper as locked or movable according to user action; reroute impacted hotset. |

- **INC-001** An incremental update MUST produce the same legal result set as a clean rebuild for the same deterministic policy, modulo explicitly allowed cache-sensitive candidate ordering.
- **INC-002** The session MUST expose why each artifact was invalidated.

## 19. Specialty Routing Extension Points

### 19.1 Differential pairs

Differential pairs should be modeled as coupled ribbon states rather than two independent traces. The future extension state includes pair gap, side order, skew/phase, legal coupled-via template, and uncoupled-length budget.

### 19.2 Buses

Bus candidates contain a shared corridor, lane order, legal permutation points, synchronized transitions, and aggregate tuning capacity.

### 19.3 Escape routing

Escape routing generates multiple legal pad-to-portal motifs and allocates channels before area routing. Portal outputs become normal terminals for later candidate generators.

### 19.4 Length tuning

Candidates carry current length, required added length, available tuning-zone capacity, and interference footprint. Global allocation reserves tuning space rather than deferring all feasibility to post-processing.

### 19.5 Return-path awareness

A future route profile may identify a reference conductor and penalize plane-split crossings, layer transitions lacking return-via options, and excessive discontinuity.

## 20. Public API and Data Contracts

### 20.1 Session API sketch

```cpp
class ApgarSession {
public:
    static Result<ApgarSession> create(BoardSnapshot, SessionConfig);

    Result<DeltaReceipt> apply_delta(BoardDelta);
    Result<CompileReceipt> compile(CompileRequest);
    Result<RouteJob> route(RouteRequest);
    Result<Preview> preview(const SolutionId&);
    Result<ValidationReport> validate(const SolutionId&);
    Result<CommitPlan> prepare_commit(const SolutionId&);
    Result<ReplayBundle> export_replay(const JobId&);
};
```

### 20.2 Route request

| **Field** | **Meaning** |
| --- | --- |
| net selection | all eligible nets, explicit set, region, or conflict hotset |
| objective profile | weights, lexicographic priorities, and hard bounds |
| generator policy | allowed/forced generators, candidate and time budgets |
| allocator policy | world count, iterations, price schedule, stall thresholds |
| edit policy | locked/movable existing copper and permitted shove scope |
| determinism | seed and requested determinism level |
| resource limits | VRAM, host memory, wall-clock/cancellation budgets |

### 20.3 Error model

- InvalidInput: malformed Board IR or unsupported semantic combination.
- Unsupported: well-formed request outside current capability.
- Unreachable: no path under the exact supported model and current locks.
- ResourceExhausted: host/device memory or representational arithmetic range
  exhausted.
- WorkBoundExceeded: a declared deterministic search-work or container proxy
  was exhausted; partial results are diagnostic only.
- ValidationFailed: candidate or solution violates exact or host rules.
- BackendFailure: device, driver, or kernel failure.
- Cancelled: cooperative cancellation.
- InternalInvariant: logic defect requiring replay artifact.

## 21. Serialization, Checkpoints, and Replay

- Versioned Board IR snapshot or reference plus content hash.
- Normalized routing configuration.
- Compiler profile and generated-artifact manifests.
- Candidate provenance and selected solution.
- Allocator price/update history or compact replay seed.
- Backend/device metadata and build IDs.
- Failure diagnostics and exact/host DRC reports.

- **SER-001** Serialized formats MUST use explicit schema versions and reject incompatible major versions.
- **SER-002** Replay bundles MUST omit proprietary board content only when configured; redaction must be explicit.
- **SER-003** Checkpoint restoration MUST verify hashes before reusing compiled artifacts.

## 22. CAD Adapter Contract

### 22.1 Adapter responsibilities

1. Read editor-native board state into normalized Board IR.
1. Map editor rules into core semantics and declare unsupported rules.
1. Preserve stable mapping between core entities and editor objects where possible.
1. Provide preview overlays without mutating the board.
1. Validate proposed transactions through the editor engine.
1. Commit atomically or provide rollback behavior.

- **CAD-001** Adapters MUST NOT approximate unsupported host rules as weaker core rules.
- **CAD-002** A host validation rejection MUST include native violation data when available.
- **CAD-003** Adapter integration tests MUST include round-trip coordinate and layer mapping.

## 23. Concurrency, Cancellation, and Failure Recovery

- Immutable snapshots are published using generation IDs.
- Compilation and candidate generation may run concurrently against the same snapshot.
- Board mutations create a new revision rather than modifying data visible to a running job.
- Jobs are cancelled at bounded batch boundaries; long kernels require split launch design.
- A failed device job never partially commits board geometry.
- CPU fallback may continue from persistent candidates and exact state when semantics match.
- Concurrent candidate workers publish one invocation through the stable batch
  boundary required by CAN-004. The store serializes distinct transactions and
  exposes linearizable snapshots; mutex acquisition order between distinct
  transactions is semantic ordering, not an implicit scheduler-independent
  merge.

## 24. Security and Robustness

- Validate all array dimensions and multiplication overflow before allocation.
- Treat imported counts and polygon complexity as adversarial.
- Bound candidate counts, path lengths, and recursion.
- Use checksummed serialization and reject malformed indices.
- Do not execute code or load arbitrary GPU binaries from board files.
- Separate diagnostic board artifacts from telemetry opt-in.

## 25. Verification and Testing Strategy

### 25.1 Test pyramid

| **Level** | **Examples** |
| --- | --- |
| Unit | geometry predicates, rule lookup, coordinate transforms, resource accounting |
| Property | conservatism, symmetry, monotonic clearance, boundary equality, random microboards |
| Differential | CPU versus GPU path costs, predecessor reconstruction, DRC agreement |
| Integration | compile-route-legalize-validate, incremental delta equivalence |
| Corpus | synthetic families and real open-source boards |
| Performance | kernel throughput, candidate/sec, world iterations/sec, memory scaling |
| Fault | OOM, cancellation, corrupted checkpoint, GPU failure, host rejection |

### 25.2 Correctness invariants

- Every candidate forms one connected route or tree over exactly the intended terminals.
- No predecessor chain cycles or exits allocated state.
- Compiled legal movement is exact-legal against represented static geometry.
- Resource usage accumulation equals a CPU recomputation.
- A committed solution has zero supported APGAR DRC violations and zero host-blocking violations.
- Incremental and clean rebuilds agree on normalized exact geometry and supported legality.

### 25.3 Microcase generator

A generated microcase framework should create small exact boards with controlled obstacle separations, corners, slots, pad shapes, layer transitions, and one-unit boundary perturbations. Many cases can be solved exhaustively to provide optimality and reachability oracles.

## 26. Benchmark and Evaluation Plan

### 26.1 Metrics

| **Category** | **Metrics** |
| --- | --- |
| Feasibility | connected nets, unrouted terminals, APGAR DRC violations, host DRC violations |
| Quality | length, vias/span, bends, minimum margin, layer use, constraint error |
| Exploration | candidates/net, unique resource signatures, overlap, topology diversity, best-of-k curve |
| Allocator | overused resources, price convergence, worlds retained, columns generated |
| Performance | end-to-end time, stage time, candidates/sec, world iterations/sec, latency |
| Memory | persistent, batch, candidate, world, exact-index, peak VRAM |
| Incremental | dirty area, artifacts rebuilt, reroute latency, solution churn |

### 26.2 Benchmark tiers

1. Small exact instances solvable by exhaustive search or mathematical optimization.
1. Controlled synthetic families: channels, pin fields, crossbars, mazes, via bottlenecks, topology alternatives.
1. Real open-source boards validated by a CAD engine.

PCBWorld is a newly released engine-grounded KiCad benchmark environment with synthetic families and 679 real open-source boards, and is a promising external evaluation path once its tooling is integrated [R8]. Results from unrelated PCB papers must not be compared without normalizing geometry, design rules, allowed routing behavior, and completion definitions.

### 26.3 Initial experiments

1. CPU A* versus GPU sweep versus GPU frontier on identical compiled fields.
1. Uniform full-grid baseline versus sparse tiled compilation.
1. Sequential negotiated routing versus candidate allocation at equal wall-clock budgets.
1. Candidate pools of 4, 8, 16, 32, 64, and 128 candidates per net.
1. One world versus multi-world search at equal memory and time budgets.
1. Incremental component movement versus clean rebuild.

## 27. Performance and Memory Budgets

Budgets are initial engineering targets, not promises. They are intended to prevent designs that only work on datacenter GPUs.

| **Scenario** | **Initial target** |
| --- | --- |
| Developer GPU | Useful operation on 12-16 GB VRAM for moderate boards. |
| Workstation GPU | Scale to 24-48 GB without changing algorithms. |
| Interactive local edit | Sub-second compilation for small dirty regions; route latency measured separately by scope. |
| Persistent compiled data | Dominated by occupied/detailed tiles rather than full board area. |
| Candidate store | Configurable hard cap by bytes and candidates/net. |
| Batch state | Automatically sized to leave recovery headroom. |

OrthoRoute's documented full-lattice estimates illustrate why APGAR should avoid explicit high-dimensional full-board graph storage; its 200 mm x 200 mm, 32-layer example estimates eight million nodes and tens of gigabytes of VRAM [R4].

## 28. Observability and Diagnostics

- Structured event log with board/job/revision IDs.
- Stage and kernel timing with device utilization context.
- Memory watermark timeline.
- Per-net candidate generation outcomes.
- Allocator convergence traces and top conflicted resources.
- Exact and host DRC disagreement report.
- Visualizable tile, resource, price, candidate, and predecessor dumps.
- One-command replay bundle generation for invariant failures.

## 29. Implementation Roadmap

| **Phase** | **Deliverable** | **Exit criteria** |
| --- | --- | --- |
| 0 - Foundations | Board IR, exact geometry, adapter fixture, build/test infrastructure | Round-trip and geometry property tests pass. |
| 1 - Compiled fields | Sparse tiles, directional masks, rule buckets, CPU reference router | Conservatism suite passes; memory telemetry exists. |
| 2 - GPU kernel bakeoff | Sweep and frontier prototypes | Differential correctness; dispatch benchmark report. |
| 3 - Candidate store | Compression, dedup, metrics, provenance | Best-of-k and diversity curves reproducible. |
| 4 - Global allocator | Representative multi-net workload, resource capacities, deterministic one-world selection, prices, and targeted column generation; multi-world execution follows the reference path | Under equal time and memory budgets, a versioned multi-net candidate-allocation workflow deterministically improves a declared board-level outcome over a named sequential baseline; see Section 29.2. |
| 5 - Legalizer/DRC | Exact transaction pipeline and host validation | Zero supported violations on committed corpus outputs. |
| 6 - Incremental session | Deltas, dirty dependency tracking, checkpoints | Incremental equivalence and latency targets met. |
| 7 - Specialty pilots | Escape and differential-pair prototypes | Interfaces validated without core redesign. |

### 29.1 First vertical slice

> **Milestone M1**
>
> Import a small board into Exact Board IR; compile conservative sparse H/V/45-degree fields; generate multiple candidates with CPU A* and one GPU kernel; validate every candidate exactly; emit a reproducible benchmark and replay bundle.

- Two or four routable signal layers.
- Single-ended two-terminal nets.
- Through vias only.
- Line-segment output with configured mitered corners.
- No shove, copper pours, differential pairs, buses, or length tuning.

### 29.2 Phase 4 global-allocator evidence gate

Phase 4 validates APGAR's central board-level hypothesis: reusable candidate
pools plus global allocation must improve routing outcomes, not merely execute
one path query, candidate batch, or allocator inner loop faster. Work should
proceed through a representative multi-net workload and resource-capacity
contract, a deterministic single-world CPU reference, negotiated prices with
targeted candidate regeneration, and only then multi-world or GPU allocator
acceleration.

#### Required workload

- The canonical corpus MUST contain distinct nets with distinct terminals and
  route requests. Repeating alternative policies for one net MAY remain a
  controlled comparison but MUST NOT substitute for multi-net evidence.
- The corpus MUST cover exact small instances, allocator-stressing synthetic
  families, and at least one imported multi-net board limited to explicitly
  supported rules. It MUST include hundreds of distinct nets and a documented
  stress tier targeting thousands; a lower memory-bounded maximum is a
  scalability result and MUST NOT be hidden by replacing nets with same-net
  alternatives.
- The supported-rule imported seed is versioned by
  `schemas/benchmark/phase4_imported_multi_net_corpus_v1.md`. Its two-net exact
  fixture establishes authentic import, ownership, workload association, and
  replay identity only; it does not satisfy the representative-scale,
  candidate-pool, equal-budget, or improvement requirements below.
- The representative descriptor roster and lazy one-case-at-a-time builders
  are versioned by `schemas/benchmark/phase4_representative_corpus_v1.md`.
  Calibration and held-out seeds, exact cases, fixed-query controls, and the
  thousands-net stress ladder are frozen before canonical allocation evidence.
- Canonical executable-success cells MUST use the corpus-derived bound envelope
  in `schemas/benchmark/phase4_paired_trial_v1.md`: reconstruction states are
  `32` times the descriptor's largest declared pool, selected occupancy and
  price records are derived from net count times that route bound, and every
  candidate, policy, retained-state, and transaction byte cap is a widened,
  checksum-bound projection of the same envelope. Generic standalone allocator
  defaults MUST NOT make a frozen success cell fail before routing. The
  manifest budget roster MUST be refreshed before observation when this
  envelope changes; descriptor identity, cells, and decision thresholds remain
  unchanged.
- Candidate pools MUST include realistic small per-net schedules such as 4, 8,
  and 16 requested alternatives. Larger pools MAY characterize scaling but
  MUST NOT be the only regime used to claim Phase 4 success.
- The thousands-net stress publication is versioned by
  `schemas/benchmark/phase4_stress_evidence_v1.md`. Case 3000 MUST bind a full
  1024-net Raw/report/operational execution, including only the coarse nested
  timing and process-lifetime `wait4` peaks those authorities actually expose.
  Cases 3001 and 3002 MUST use the real representative builder's bounded
  descriptor/Board/one-compiled-net preflight under default limits. Their 739-
  and 369-net quotients are capacity-derived prefixes, never achieved or
  materialized counts; candidate preparation and allocation are not reached.
  Required compiled host bytes are logical estimates, not RSS, and unavailable
  elapsed or peak-memory measurements MUST remain tagged rather than numeric.
  The 4096-net target remains unsupported when 1024 is the largest complete Raw
  success, even though all three diagnostic rows are present.
- Initial CPU pools are prepared through the persistent, bounded, atomic
  contract in `schemas/allocator/cpu_candidate_pool_preparation_v2.md`.
  Worker count and completion order are operational only: identical semantic
  inputs MUST produce identical ordered columns, retained pools, and replay
  checksum. Ordered columns and aggregate counters MUST retain actual CPU A*
  route-work units so an evidence runner can distinguish accepted opportunity
  budgets from consumed work. A fatal post-query preparation MUST retain a
  bounded, checksum-covered roster of every attempted query and available work
  after the worker wave joins. The observation MUST hash an authoritative
  CandidateStore-publication-committed bit; before commit it owns no store, and
  after commit it MUST transfer the authoritative store rather than destroy it
  or imply rollback. A disconnected or unsupported base MUST remain an explicit proof;
  later equivalent alternatives MAY be recorded as skipped columns rather than
  fabricated or repeatedly searched.
- Controlled families MUST vary route length, occupancy, run fragmentation,
  turn complexity, reachability, rule bucket, and region-of-interest size.
  Reports MUST state how these features affect compatible batch fill,
  prepared-view reuse, and CPU/GPU dispatch.
- At fixed total query counts, evidence MUST distinguish one net with many
  alternatives, many nets with one candidate, and many nets with small
  candidate pools. Per-net diversity work MUST remain scoped to each net rather
  than becoming an artificial all-candidate quadratic operation.
  The dedicated publication is versioned by
  `schemas/benchmark/phase4_fixed_query_control_v1.md`. Its common 1024-query
  opportunity applies only to initial candidate preparation. The buildable
  256x4, 128x8, and 64x16 paired trials retain their unequal whole-trial
  opportunities because regeneration adds two queries per net; cross-shape
  whole-trial timing is therefore not an equal-query control. The 1x1024 and
  1024x1 endpoints remain descriptor-only with explicitly unavailable
  measurements, never numeric zero placeholders. Every diversity count is a
  sum of within-net unordered pairs; cross-net candidate pairs are forbidden.
  Executed rows MUST fully validate and bind Raw, per-net report, and
  operational authorities from one clean source, environment, and cap set.

#### Correctness and determinism

- A CPU reference MUST independently reproduce every correctness-critical
  allocator primitive, including candidate scoring, resource accumulation,
  capacity overuse, selection, and price updates. Any GPU implementation MUST
  pass CPU/GPU differential tests before contributing performance evidence.
- Each world MUST select exactly one immutable candidate for every routable net
  with an admissible pool and retain a structured outcome for a net with no
  admissible candidate. Selected candidate identity and provenance MUST remain
  bound to its exact net, ordered request endpoint coordinates and layers,
  terminals, Board IR snapshot, compiled view, rule bucket, and policy.
- Resource usage and over-capacity totals MUST equal an independent CPU
  recomputation from canonical candidate footprints. Resource refinement MUST
  remain available when exact conflicts cannot be represented by the current
  capacity model.
- Canonical exact cases MUST expose a diagnostic final-pool snapshot before the
  candidate allocation session is destroyed. The snapshot contract is
  versioned by `schemas/benchmark/phase4_exact_small_snapshot_v1.md`: it binds
  complete immutable candidate payloads, the current capacity vocabulary, and
  the production-selected world to the Raw and per-net authorities. Its actual
  `prod(max(1, pool size))` MUST be preflighted with widened arithmetic against
  the case bound and 4096 before any candidate traversal; overflow or excess
  fails as a whole and MUST NOT emit a truncated pool prefix. This diagnostic
  artifact MUST bind the complete candidate-arm semantics, reproduce the exact
  ordered two-level final-pool manifest (including empty pools), replay every
  non-authenticating exact candidate-admission check, and independently expand
  selected footprints to reproduce capacity overuse. It is subject to frozen
  component and serialized-output bounds and is an input to independent
  fixed-pool enumeration, never timing evidence or proof that the frozen pools
  are route-complete. Raw and per-net checksums in the snapshot are claimed
  associations until the publication validator loads and joins those external
  documents. The v1 snapshot carrier MUST select exactly a Raw-v1/Wire-v1 or
  Raw-v2/Wire-v2 source envelope before candidate work; this authority selector
  does not change the frozen snapshot shape or checksum domains.
- Exact-small publication MUST follow
  `schemas/benchmark/phase4_exact_small_oracle_publication_v1.md`. The
  independent validator MUST fully validate and structurally join Raw v1,
  Per-Net Report v1, and the final-pool snapshot before enumeration, including
  every per-net final-pool and selected identity/payload/metric field rather
  than telemetry checksums alone. After product-only and aggregate shape-only
  preflight, it MUST independently reconstruct candidate metrics/resources and
  replay source-private non-authenticating exact admission against the rebuilt
  Board/workload without invoking allocator, session, or scoring logic. It MUST
  enumerate the complete bounded product with an explicit sentinel for each
  empty pool and rank worlds only by maximum selected-net count, minimum total
  overuse units, then minimum unweighted intrinsic base cost. Overused-resource
  count is a separately recomputed diagnostic and MUST NOT rank worlds. Equal
  objective values establish production optimality even when witnesses differ;
  the diagnostic artifact retains the optimum count and lowest ordered
  candidate-ID witness. This is fixed-pool evidence only and remains ineligible
  for timing or route-completeness claims.
- Exact cells assigned to Same-Run Raw Evidence v2 MUST instead follow
  `schemas/benchmark/phase4_exact_small_oracle_publication_v2.md`. The
  validator MUST fail closed in authority order: fully join Raw v2 with its
  Same-Run Decision Telemetry companion before reading the Wire-v2 report, and
  fully join that report before reading the snapshot. Oracle Artifact v2 MUST
  bind the artifact and source envelope of all four inputs while preserving
  the independent reconstruction, replay, and exhaustive fixed-pool proof
  boundary above. Negative exact-rejection telemetry remains valid evidence;
  the diagnostic oracle MUST NOT reinterpret it as a failed publication.
- Prices, histories, iteration counts, regeneration budgets, and world state
  MUST be bounded, versioned, and replayable. Identical board, configuration,
  seed, candidate pools, and supported backend/device class MUST produce the
  same externally visible selections and diagnostics.
- Fixed-pool multi-world execution MUST branch every schedule from one common
  authenticated price state, retain exact nondominated feasible or declared
  near-feasible outcomes, and separately identify the stable lexicographic
  decision world. Search-weighted selection scores from different schedules
  MUST NOT be compared as a common quality metric.
- Multi-world source preflight MUST enforce its store, One-World input/expanded-
  use, and aggregate work caps before traversing any already rejected suffix;
  a deterministic bound error MUST itself have bounded work under adversarial
  repeated candidate handles.
- Candidate generators MUST continue to read immutable price snapshots and
  MUST NOT mutate global occupancy while searching. Unsupported rules or
  resource semantics MUST be declared and delegated rather than approximated
  silently.
- One targeted-regeneration execution MUST validate its complete source pools,
  collect authentic generator outputs, publish candidates and diagnostics
  through one CAN-004 transaction, rerun the CPU reference at the same immutable
  price snapshot, and retain refreshed winners before releasing source-world
  retention. Known exact conflicts absent from the current resource vocabulary
  MUST produce resource-refinement-required rather than convergence or stall.
- Production targeted-regeneration planning is versioned by
  `schemas/allocator/targeted_regeneration_plan_v3.md`. Before ordinary
  remainder allocation, it MUST reserve a price-only column and one
  primary-conflict hard-ban column for the best bounded representative of each
  retained distinct primary resource. The exact-net union, coverage grouping,
  fallback retention, and final two-lane roster MUST remain bounded,
  deterministic, and replayable. A retained target MUST still budget its
  price-only column plus every reachable retained hard-ban action, subject to
  the per-net, total-column, action, and candidate-headroom caps.
  Production targeted-regeneration execution is versioned by
  `schemas/allocator/targeted_regeneration_execution_v6.md`. It MUST validate
  the complete Plan-v3 coverage prefix, both lane orders, primary-resource
  uniqueness, action semantics, aggregate replay, and the complete
  authenticated plan checksum before query one. It MUST use bounded CPU A*,
  preflight aggregate route work, all complete price-roster projection
  passes, candidate-draft/rejection/transient bytes, and CandidateStore
  input/exact work before the first query. The same preflight MUST prove the
  worst-case refreshed candidate count and expanded resource uses fit the
  complete One-World limits. It MUST retain available per-column CPU telemetry
  and the complete canonical rejection, and bind a bounded attempted prefix
  plus an authoritative CandidateStore-publication-committed bit into any
  post-query failure observation. CandidateStore staging failures MUST change
  neither pools, rejection history, pins, session/net bindings, nor telemetry;
  post-commit failures MUST identify that committed state rather than imply
  rollback. A complete route request MUST exist before its attempted column and
  query counter become observable. Attempted columns MUST retain their truthful
  query, build, rejection-evidence, unpublished-draft, or publication-committed
  outcome-correlation stage and counters at failure time. A final rejection
  stage MUST have complete canonical rejection evidence, and publication-
  committed columns MUST enter the outcome-correlation stage before the commit
  bit becomes visible. Successor-pin counters MUST advance only after the
  successor lease is acquired. Source pool/candidate count drift MUST fail
  before footprint traversal, and refreshed expanded-use preflight MUST stop at
  its configured envelope without scanning a rejected suffix.
  Every execution MUST accept a caller-rooted deterministic seed and mix it
  into each derived target batch and policy identity; the root seed is part of
  execution and composed-session replay identity.
- The reusable CPU contender is versioned by
  `schemas/allocator/cpu_candidate_allocation_session_v5.md`. It MUST own the
  prepared CandidateStore and every lease-bearing terminal result on success,
  retain caller ownership of all inputs on failure, and preserve that
  authoritative store when a failed targeted observation says publication
  committed. It repeats bounded targeted-regeneration epochs on one common
  One-World lineage, declares a fixed point only when complete pool semantics,
  selected route semantics, and complete price values are unchanged, then
  freezes the final pools for one fixed-pool Multi-World execution. Its
  whole-session preflight MUST cover CandidateStore transaction input bytes,
  exact admission work, cumulative rejection retention, fixed-pool terminal
  work, buffers, retained worlds, and winner pins before the first route query.
  Count-known failures MUST precede source-footprint traversal, and a rejected
  footprint suffix MUST not be scanned. The terminal preferred retained world
  MAY upgrade the common-lineage outcome to feasible. Version 1 MUST NOT
  simulate interleaved multi-world column generation by sequential publication
  whose visible pool depends on world order.
- ADR-060 activates
  `schemas/allocator/targeted_regeneration_plan_v3.md`,
  `schemas/allocator/targeted_regeneration_execution_v6.md`, and
  `schemas/allocator/cpu_candidate_allocation_session_v5.md`. Production
  rejects Plan v1/v2, Execution v1-v5, and Session v1-v4 before routing,
  publication, mutation, or input consumption.
  Plan v3 MUST reserve a price-only and primary-conflict hard-ban column for
  the best bounded representative of each retained distinct primary conflict
  before secondary actions or duplicate-primary targets consume remaining
  opportunity. The seed prefix and count are replay identity; grouping scratch
  MUST be bounded and input-order independent, and a retained-target rescan
  MUST reproduce its provisional primary action field-for-field. This rule
  neither prioritizes thin pools nor promises cross-epoch rotation among
  repeatedly failing nets on one resource.
- Session-v5 activation does not reinterpret any frozen Corpus-v2 or H=4096
  algorithm-budget preimage. Those builders deliberately reconstruct Session
  v4, and every corresponding arm, snapshot, controller, and hidden-worker
  execution entry point MUST fail with
  `P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001` before fixture, preparer, worker, or
  allocator access. Canonical roster v3, Confirmatory Decision Protocol v2,
  and existing Raw/report/operational/publication artifacts remain unchanged.
  A future acquisition requires separately reviewed Session-v5 budget and
  consuming authorities. Explicitly nondecision Representative-Corpus-v1
  diagnostic tests may use
  `schemas/benchmark/phase4_current_v1_diagnostic_budget_roster_v2.md`; that
  roster cannot authorize Corpus-v2 execution or publication.
- ADR-061 freezes the inactive acquisition-free Session-v5/H=4096 canonical
  budget contract in
  `schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v4.md`.
  Roster v4 MUST retain the complete ordered 102-cell roster-v3 authority and
  change exactly the nested candidate-allocation Session schema from v4 to v5.
  Its aggregate authority MUST explicitly bind the Plan-v2-to-v3 and
  Execution-v5-to-v6 child transition composed by Session v5, while every
  baseline, preparation, non-schema session, price, query/work, stopping, and
  external-budget field remains unchanged. The separately named preimage
  builder MUST derive the frozen H=4096/Session-v4 preimage, positively require
  Session v4, and replace only that schema field with an explicit fixed
  Session-v5 constant. It MUST remain private, configuration-only, and absent
  from every execution-authority switch. Roster v4 alone authorizes no
  fixture, preparer, allocator, worker, Raw, telemetry, report, operational,
  snapshot, oracle, matrix, decision, publication, or acquisition path.
  Confirmatory Decision Protocol v2 remains bound to roster v3; a separately
  reviewed Protocol v3 and separately reviewed consuming authorities are
  required before any Session-v5 Corpus-v2 execution.

#### Equal-budget decision evidence

- The primary comparison MUST name a sequential negotiated-routing baseline
  and hold wall-clock, memory, supported-rule, corpus, candidate-generation,
  and stopping budgets equal. CPU comparisons MUST use a persistent production
  worker pool rather than per-invocation thread creation when the allocator
  implementation uses persistent workers.
- A paired trial MUST use one declared deterministic root seed for baseline,
  initial-pool preparation, and targeted regeneration. Derived per-net or
  per-target seeds MAY differ by domain, but they MUST remain rooted in that
  paired value and checksum-covered. Reports MUST publish both conservative
  query/work opportunities and actual consumed CPU route work; unequal actual
  work alone does not invalidate a trial whose predeclared opportunity caps,
  stopping depth, and external wall/memory limits are equal.
- Decision-eligible successful arms and pairs are versioned by
  `schemas/benchmark/phase4_paired_trial_v1.md`. Each contender MUST build an
  independent case in an isolated process. Before execution, the runner MUST
  prove `N*S == N*K + E*C`, identical per-query A* limits, exact aggregate
  route caps, structurally reachable `C` under every planner shape/headroom
  bound, and normalized stopping depth `S == E + R - 1`. The candidate
  outcome is its preferred retained Multi-World when present and its common-
  lineage One-World otherwise. Semantic identity excludes order, worker count,
  timing, and measured resources; a separately checksum-bound external
  authority MUST name distinct arm process instances, exact enforced wall and
  virtual-address-space safety limits, observed process-lifetime peak resident
  memory, successful exit, and persistent-preparer lifecycle evidence before
  pairing. `RLIMIT_AS` MUST NOT be relabelled as an RSS limit. The measured arm MUST capture preparer telemetry
  around its contender call, the external observation MUST match it exactly,
  and assembly MUST revalidate both copies and the supported worker range. A
  failed paired arm MUST retain its typed child failure and any caller-owned
  or post-publication authoritative CandidateStore until explicit
  reconciliation.
- Raw paired execution is versioned by
  `schemas/benchmark/phase4_raw_evidence_v1.md`. One source-identical,
  separately exec'd worker per contender MUST persist across all 20
  repetitions in a case/pool/worker cell after one untimed warm-up. Measured
  arms MUST execute serially with ten AB and ten BA repetitions. The parent
  alone owns absolute monotonic watchdog timing, process and controller
  identities, exit state, `wait4` lifetime peak RSS, external finalization,
  and pairing. An abnormal later exit invalidates earlier arms from that
  process. Child messages MUST use a bounded explicit wire format; typed
  failures MUST reconcile any authoritative CandidateStore before crossing
  the process boundary into a bounded diagnostic fingerprint; that fingerprint
  MUST NOT be represented as the heavyweight invariant replay. Successful raw
  cells MUST bind every case, Board IR, workload, and capacity identity to the
  frozen machine-readable representative manifest. Source fields MUST be
  checksum-bound to the complete cell artifact, and publication validation
  MUST compare the artifact commit with an independently supplied expected
  commit. Timeouts, launch/exec errors, signals, nonzero exits,
  protocol failures, and resource-bound failures remain incomplete attempts,
  never allocator losses.
- The canonical sequential reference is
  `schemas/allocator/sequential_negotiated_baseline_v1.md`. Its one-current-route
  search state MUST NOT be relabeled candidate allocation: it routes and commits
  one net at a time, restores a prior exact route after a rejected reroute, and
  uses the One-World reference only as an end-of-sweep occupancy oracle. Known
  unmapped exact conflicts require resource refinement, and fixed-point stall
  requires both unchanged route semantics and unchanged complete price values.
- Prepared-session timing MUST include scheduling, compatible-batch formation,
  candidate generation, compact readback, exact validation and admission,
  per-net store publication, selection, resource accumulation, price updates,
  targeted regeneration, and transient release. Import, compilation, initial
  upload, cache misses, and release MUST also be reported in a separate cold
  scope; neither scope may hide required disconnected-result confirmation.
- The predeclared primary outcome MUST be board-level and lexicographic:
  maximize nets with an admissible selected candidate, then minimize total
  over-capacity resource usage, then minimize the configured quality objective,
  subject to predeclared candidate-quality and exact-rejection guardrails.
  Candidate queries per second and world iterations per second are diagnostic
  metrics, not Phase 4 success criteria.
- Phase 4 exits only when uncertainty-qualified, family-level evidence shows a
  strict improvement over the named sequential baseline on at least two
  allocator-stressing synthetic families at equal budgets, without reducing
  the primary feasibility outcome or violating the declared guardrails on the
  imported supported-rule case. At least one passing family MUST have one
  board-wide resource-conflict graph coupling its routable nets; repeated
  independent local motifs cannot supply both required family wins. Pooled
  throughput MUST NOT hide family regressions, and negative or workload-
  specific results MUST remain visible.
- The report MUST publish per-net candidate yield and diversity consumed,
  selected-candidate quality, unresolved overuse, convergence or stall reason,
  columns requested and admitted, batch fill, prepared-view cache behavior,
  stage timings, CPU/GPU utilization context, and peak host/device memory.
  Successful in-process per-net yield, pool diversity, and selected-candidate
  fields are versioned by
  `schemas/benchmark/phase4_per_net_arm_telemetry_v1.md`; the diagnostic seam
  remains separate from Raw Evidence v1. Canonical diagnostic serialization,
  the independently frozen complete-EntityRef representative roster, and the
  rebuilding C++ validator are versioned by
  `schemas/benchmark/phase4_per_net_report_artifact_v1.md`. The report is
  explicitly not decision-eligible. The separate diagnostic process and
  independent publication join are versioned by
  `schemas/benchmark/phase4_per_net_report_publication_join_v1.md`; publication
  first validates complete Raw v1 with an independently supplied expected
  commit, then binds exact source/config/cell/pair/arm identities and compares
  both complete diagnostic semantics objects with the Raw repetition-zero
  baseline-first records.
  The Raw-v2 counterpart is versioned by
  `schemas/benchmark/phase4_per_net_report_publication_join_v2.md`; it first
  validates the mandatory Same-Run Decision Telemetry companion, then requires
  the unchanged report payload to carry the exact Wire-v2/Raw-v2 source
  association before the same structural diagnostic comparison.
  Exact-validation rejection guardrails instead use
  `schemas/benchmark/phase4_same_run_decision_telemetry_v1.md`. A distinct
  telemetry-aware Wire v2 success response MUST carry the ordinary measured
  arm execution and its minimal per-net column partition from the same
  authentic contender execution. All 20 repetitions and both arms are
  required for every exact, held-out, and imported decision cell. The
  controller MUST apply the Raw process-lifetime invalidation rules to both
  outputs, and publication MUST join every finalized attempt, arm, external
  authority, pair, process, dispatch, controller, and cell identity. Existing
  Raw or diagnostic artifacts cannot be promoted into this authority.
  The ordinary measured half of that invocation is versioned separately by
  `schemas/benchmark/phase4_same_run_raw_evidence_v2.md`. Its root MUST bind
  Raw Evidence schema 2 and the actual Wire-v2 carrier; a Raw-v1 validator MUST
  reject it. Failed Wire-v2 runs MUST still emit their complete checksummed Raw
  attempt artifact and exit nonzero without a companion. A companion final
  path MUST be installed atomically without replacement only after checked Raw
  output and synchronized unpublished-file construction; partial publication
  MUST NOT expose the final companion name.
  Operational Projection v1 is separately versioned by
  `schemas/benchmark/phase4_operational_projection_v1.md`. It MUST first
  validate complete canonical Raw v1 against an independently supplied commit,
  bind every pair and arm authentication layer, and derive only nested timing,
  route-work, candidate-accounting, process-lifecycle, and process-lifetime
  peak fields already authenticated by Raw. Measurements absent from Raw MUST
  use explicit unavailable or not-applicable tags, never numeric zero. A
  complete Raw cell may be marked as eligible input to later statistics, but
  its projection is not standalone-decision-eligible, has incomplete matrix
  coverage and incomplete Phase 4 telemetry, and does not establish the
  Section 29.2 exit gate.
  Operational Projection v2 is separately versioned by
  `schemas/benchmark/phase4_operational_projection_v2.md`. It accepts only a
  complete joined Raw-v2/same-run authority, binds every same-run pair and arm
  capture plus the observed rejection-guardrail result, and preserves the same
  incomplete telemetry and non-decision flags.
  Operational Measurement Publication v1 is separately versioned by
  `schemas/benchmark/phase4_operational_measurement_publication_v1.md`. It MUST
  preserve Raw v1/v2 and both operational projections unchanged. For each
  successful cell it uses four distinct diagnostic execs: measured baseline,
  measured candidate, unmeasured baseline authority, and unmeasured candidate
  authority. Exact-child `wait4` owns measured CPU/RSS, controller monotonic
  fork-to-reap owns outer wall time, and authority replays MUST NOT publish
  numeric process-resource measurements. The controller MUST establish
  temporary child-subreaper authority from an empty child roster and reject,
  pidfd-terminate, and exactly reap every adopted descendant before restoring
  that authority; session/process-group escape and closed capture pipes MUST
  NOT evade containment. If an empty child roster cannot be proven, the
  subreaper MUST remain enabled and the controller MUST fail without another
  dispatch or publication. Controller affinity and namespace-visible cgroup
  identity/controls MUST match before and after every dispatch, and exact-child
  affinity/cgroup path MUST match after fork and before reap. Cgroup provenance
  MUST bind namespace/mount identity and MUST NOT claim ancestry visibility
  above the namespace root. Each authority MUST recompute its
  session checksum from the complete live result preimage before destruction;
  candidate publication additionally requires exact equality between the
  separately measured and authority compact witnesses. The final join MUST
  validate the frozen cell's version-appropriate Raw/same-run authority and
  legacy operational projection, then bind exact source, config, process,
  worker, replay, provenance, and capture identities. CPU-only unavailable
  fields MUST use typed applicability. One complete cell is eligible only for
  later matrix aggregation and remains non-standalone, non-statistical,
  and incomplete coverage. Durable output MUST be installed atomically without
  replacement.
  Every optimization claim MUST bind the versioned corpus, configuration,
  hardware, toolchains, exact clean commit, and comparison baseline.
  The pre-observation Phase 4 matrix, lexicographic comparison, family-level
  exact sign test and Holm correction, non-regression guardrails, and diagnostic
  timing summaries are versioned by
  `schemas/benchmark/phase4_statistical_decision_protocol_v1.md`. Its canonical
  104-cell expansion includes 86 noncalibration closure cells. It contains no
  observed decision and does not complete Phase 4; missing or failed evidence
  remains incomplete rather than becoming an allocator loss. The
  authority-only supersession in
  `schemas/benchmark/phase4_statistical_decision_protocol_v2.md` incorporates
  that exact v1 artifact and changes no decision rule: it assigns the 78 exact,
  held-out, and imported cells to Same-Run Raw v2 plus their v2 diagnostic
  joins, while calibration, fixed-query, and stress retain Raw v1. The further
  authority-only supersession in
  `schemas/benchmark/phase4_statistical_decision_protocol_v3.md` changes only
  the three exact cells from Oracle Artifact v1 to the sidecar-binding Oracle
  Artifact v2; its expanded cells, evidence dispositions, thresholds,
  inference, timing, guardrails, and completion requirements MUST remain
  identical to protocol v2. The final pre-observation authority-only
  supersession in
  `schemas/benchmark/phase4_statistical_decision_protocol_v4.md` replaces the
  legacy Operational Projection authority with Operational Measurement
  Publication v1 on all 100 success cells and authenticates the exact 102-entry
  canonical algorithm-budget roster. Its logical cells, evidence
  dispositions, decision rules, thresholds, and completion requirements MUST
  remain identical to protocol v3.
  Representative Corpus v2 begins a separate confirmatory campaign rather
  than superseding or reinterpreting the authentic negative V1 publication.
  `schemas/benchmark/phase4_representative_manifest_v2.md` freezes its 40
  paired rows and 102 canonical algorithm budgets;
  `schemas/benchmark/phase4_workload_net_roster_manifest_v2.md` independently
  freezes 38 complete workload EntityRef rosters and four typed exclusions.
  The pre-observation V2 matrix and evidence authority namespace are versioned
  by `schemas/benchmark/phase4_confirmatory_decision_protocol_v1.md`. It
  incorporates Protocol v4's outcome, inference, thresholds, timing, and
  completion rules without change, preserves the V1 decision, and expands to
  104 disjoint V2 logical cells. Corpus authority MUST remain trusted and
  explicit; no case-ID fallback may select V1 or V2. At the authority freeze,
  no V2 heldout allocation outcome has been observed. Heldout acquisition MUST
  run from the clean commit containing the frozen V2 authorities, and any
  earlier observation invalidates that heldout roster.
  The initial post-freeze confirmatory runner and validators are a separate,
  explicit Corpus v2 authority path. Before the complete acquisition and
  publication chain is reviewed and committed, that development runner MUST
  accept only exact and calibration roles and MUST reject fixed-query, stress,
  heldout, and imported cases before worker launch. Legacy runner, wire, and
  validator entry points remain Corpus v1-only; V2 wire decoding and Raw joins
  require an explicit out-of-band Corpus v2 entry point. The runner's internal
  ordinary and same-run worker modes MUST independently repeat the frozen role
  and protocol-assigned carrier check before fixture access, preparer creation,
  warmup, or request decoding; caller-supplied worker descriptors do not inherit
  controller authorization.
  The first confirmatory downstream join is versioned by
  `schemas/benchmark/phase4_confirmatory_per_net_report_publication_join_v1.md`.
  It retains Per-Net Report Artifact v1 as non-decision-eligible diagnostic
  payload while selecting Corpus v2 explicitly. Before later report roles are
  reviewed and committed, its runner MUST accept exactly ordinary cell
  `(10200,4)` and MUST reject Wire 2 and every other case/pool before
  diagnostic execution. Its publication validator MUST fully authenticate the
  Raw cell before opening the bounded regular report input, then structurally
  join both complete arm semantics and the frozen V2 EntityRef roster.
  The separately versioned confirmatory same-run join is
  `schemas/benchmark/phase4_confirmatory_same_run_per_net_report_publication_join_v1.md`.
  Its runner MUST accept exactly `(10100,4)` with explicit Corpus 2 and Wire 2
  and MUST reject every other case/pool before diagnostic execution. Its
  publication validator MUST read and completely authenticate Raw first,
  enforce that exact development cell, then read and completely join the
  bounded regular same-run telemetry sidecar. Only after the sidecar join
  succeeds may it open and structurally join the bounded regular report. Raw
  remains outcome/timing authority, the sidecar remains exact-rejection
  guardrail authority, and the report remains non-decision-eligible diagnostic
  evidence.
  The first confirmatory operational join is versioned by
  `schemas/benchmark/phase4_confirmatory_operational_measurement_publication_v1.md`.
  Its production and test-only workers MUST require explicit Corpus 2, Wire 1,
  and exactly calibration cell `(10200,4)` before fixture access, case
  construction, preparer creation, warmup, or replay; only the test target may
  recognize an unstamped-source escape. The separately named publisher MUST
  completely validate bounded regular ordinary Raw first and enforce that
  exact scope before opening the bounded regular four-process capture. Only
  after capture validation and the complete Raw/capture semantic join may it
  open a publication validation input or install output. Corpus v2 authority
  MUST be selected explicitly in worker serialization and capture validation,
  while legacy entry points remain Corpus v1-only. Worker selection MUST come
  only from the compiled public launcher's adjacent, target-specific standalone
  Bazel runfiles tree after the launcher clears ambient runfiles variables, and
  the publisher MUST independently bind the canonical production-worker digest.
  The test worker MUST retain its
  actual non-publishable source envelope; it MUST NOT synthesize clean source
  state. When an unstamped test build exposes no commit identity, its worker
  source envelope MUST use the all-zero 40-character unavailable sentinel
  rather than the caller-supplied commit association, and a test worker built
  from a publishable clean stamped source MUST fail without replay or output.
  Inner Python targets MUST consume a one-use inherited handshake from the
  compiled public launcher before parsing or emitting evidence. Nested Bazel
  execution MAY invoke the launcher through an enclosing runfiles symlink only
  when that path resolves to the canonical launcher, but the enclosing tree is
  never an execution-authority candidate. The launcher MUST authenticate and
  traverse exactly its adjacent target-specific standalone tree, including
  external repository subtrees, and MUST delegate only from that tree. Direct
  stage-one Python execution MUST disable `site` initialization until the
  rules_python bootstrap establishes that authenticated tree; stage-two site
  initialization MAY run only with the standalone root already selected. The
  launcher MUST establish default `SIGCHLD` reaping semantics rather than
  inherit an ignored child signal, and the delegated authority MUST terminate
  when its exact public launcher dies. Any isolated bytecode-cache path MUST be
  absent before delegation so cancellation cannot leak it. Test-worker publishability MUST
  be derived solely from embedded source state; caller commit mismatch MUST
  NOT downgrade a clean stamped build.
  These stable hashes remain non-cryptographic association checks, not
  protection against arbitrary malicious artifact rewriting. Measured process
  resources remain diagnostic, Raw remains outcome/timing authority, and the
  confirmatory publication remains non-standalone, non-statistical, and
  incomplete coverage.
  The separately versioned confirmatory same-run operational join is
  `schemas/benchmark/phase4_confirmatory_same_run_operational_measurement_publication_v1.md`.
  Its production and test-only workers MUST require explicit Corpus 2, Raw
  Wire 2, and exactly exact cell `(10100,4)` before fixture access, case
  construction, preparer creation, warmup, or replay. Its separately named
  publisher MUST completely validate bounded regular Same-Run Raw first,
  enforce that exact scope, then completely validate and join the bounded
  regular Telemetry Wire 2 companion before resolving the bundled worker or
  opening the bounded regular capture. Only after capture validation and the
  complete Raw/telemetry/capture semantic join may it open a publication
  validation input or install output. Worker selection, compiled-launcher
  isolation, test-source handling, and independent digest binding retain the
  ordinary confirmatory operational requirements but use a distinct worker
  target and publication checksum domains. Raw remains outcome/timing
  authority; the telemetry companion remains exact-rejection guardrail
  authority. A structurally valid false guardrail MUST remain publishable
  authentic negative evidence for complete aggregation rather than being
  rejected or erased by this diagnostic join. The publication remains
  non-standalone, non-statistical, and incomplete coverage.
  The confirmatory exact-small authority is separately versioned by
  `schemas/benchmark/phase4_confirmatory_exact_small_oracle_publication_v1.md`.
  Its production and test-only snapshot runners MUST require explicit Corpus 2,
  Raw Evidence schema 2, Raw Wire 2, and exactly `(10100,4)` before fixture
  access, representative-case construction, preparer creation, or candidate
  execution. Every claimed Raw/report artifact, source envelope, and complete
  repetition-zero Raw reference MUST be nonzero at that pre-execution boundary.
  The snapshot retains Exact-Small Snapshot v1's frozen payload,
  bounds, canonical JSON, and checksum domains, but its separately selected
  Corpus v2 builder and validator MUST rebuild the V2 cell plan, case, roster,
  budget, and complete semantics; legacy snapshot publishers remain Corpus
  v1-only. The publication CLI MUST completely validate bounded regular
  Same-Run Raw and enforce that scope before opening bounded regular Telemetry
  Wire 2, completely join telemetry before opening the bounded regular Wire-2
  report, and completely join the report before opening the bounded regular
  snapshot. Only then may it invoke the fixed separately compiled Corpus v2
  admission-replay authority. The public publisher MUST be a compiled launcher
  that authenticates its canonical executable, complete adjacent target-specific
  standalone runfiles tree, and hermetic interpreter before delegating to a
  fixed private Python target through a one-use parent-bound handshake. An
  enclosing Bazel runfiles tree MAY contain an invocation symlink but MUST never
  become the selected Python/data authority. The inner target MUST reject direct
  execution, and replay MUST resolve only from the authenticated standalone tree
  without ambient runfiles fallback. A nested process test MUST declare a
  build-only marker action that receives each launcher through its
  `FilesToRunProvider`, forcing Bazel to materialize that exact target-specific
  standalone tree before the test begins. The marker, not the standalone tree,
  is test data; neither the marker nor the enclosing test tree is an execution
  authority. The process test MUST pass from a fresh Bazel output root in both
  normal and stamped benchmark configurations. Direct stage-one Python
  execution MUST disable `site` initialization until the rules_python bootstrap
  establishes that tree; stage-two site initialization MAY run only after that
  selection.
  Only then may the validator enumerate the complete bounded final-pool product.
  The domain-separated output MUST bind campaign,
  Corpus 2, every field of the frozen exact configuration, and the complete
  Raw, telemetry, report, and snapshot artifact/source associations. A
  rechecksummed configuration alias MUST be rejected. Production and exhaustive
  objectives MUST be
  exactly equal before an Oracle Artifact can be emitted; a mismatch produces
  no exact completion authority and leaves the campaign incomplete, never an
  allocator loss. A structurally valid false exact-rejection guardrail remains
  publishable authentic negative evidence only when the fixed-pool proof
  otherwise completes. This authority proves only fixed-pool optimality for one
  development cell; its operational publication remains a sibling requirement,
  and it does not prove pool route completeness, publish timing evidence, open
  another cell, complete the confirmatory matrix, or decide Phase 4.
  The acquisition-free H=4096 remediation configuration is separately frozen
  by
  `schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v3.md`.
  It retains Representative Corpus v2, Representative Manifest v2, Workload
  Roster Manifest v2, every case/workload/root-seed/opportunity/external-budget
  identity, and every non-price canonical configuration field. Only
  `history_step_per_overuse_unit` changes from 2250 to 4096, identically in the
  sequential baseline and reusable-candidate session; present price remains
  one. The fresh 102-cell roster checksum binds all H=4096 configuration
  preimages and the superseded H=2250 roster. The H=2250 canonical
  configuration and roster stdout, manifests, protocol, authority checksums,
  and evidence remain stable; binary byte identity is not claimed.
  This roster is inactive configuration authority only. Its fixed no-argument
  generator MUST use a configuration-preimage-only link surface containing no
  fixture, representative-case, worker, or allocator-execution symbol.
  ASan and UBSan registration metadata roots otherwise-dead shared-source
  sections and is therefore incompatible with that link-surface contract. The
  generator target MUST reject those instrumented configurations rather than
  produce a partially sanitized artifact. Sanitizer gates MUST still run the
  independently instrumented H=4096 structural test and MUST link-inspect the
  exact ordinary generator obtained through an explicit sanitizer-reset audit
  transition.
  The frozen Corpus-v2/H=2250 preimages positively bind equal-arm
  `present=1,history=2250`; corpus version, case ID, cell-plan checksum, wire
  schema, source commit, and opaque budget-checksum matching MUST NOT
  reinterpret them as H=4096. Session-v5 activation closes both H=2250 and
  H=4096 execution surfaces before case, fixture, preparer, or worker access.
  Separately frozen Session-v5 budget, acquisition, and consuming authorities
  are required before either configuration may run again. The roster alone
  does not claim exact optimality, publish evidence, open another development
  cell, or authorize heldout execution.
  The acquisition-free authority-only supersession is frozen by
  `schemas/benchmark/phase4_confirmatory_decision_protocol_v2.md`. It
  incorporates Confirmatory Decision Protocol v1 checksum
  `7747512371013753061`, binds canonical budget roster v3 authority/schema,
  its 102 cells and checksum `18429170436700418962`, and binds the complete
  equal-arm `present=1,history=2250` to `present=1,history=4096`
  configuration transition. The 104-cell matrix, evidence dispositions,
  families, decision rules, thresholds, timing, guardrails, and nine
  completion requirements MUST remain unchanged. Ten configuration-specific
  v2 artifact authorities and
  `phase4_confirmatory_matrix_decision_publication_v2` are reserved; their
  authority versions MUST NOT implicitly select or change a payload or wire
  schema. Protocol v2 alone authorizes no execution or acquisition. Future
  separately reviewed development entry points MAY initially open only exact
  `(10100,4)` through the same-run carrier and historical calibration identity
  `(10200,8)` through the ordinary carrier; H=2250 evidence cannot be promoted
  into either authority. Heldout, imported, fixed-query, stress, matrix, and
  decision paths remain closed. Before heldout observation, a future separately
  reviewed campaign-acquisition authority MUST bind one clean stamped source
  commit containing the exact Protocol v2 and roster v3 authorities plus the
  completed reviewed execution and publication chain; the protocol/roster-only
  commit is insufficient. Premature H=4096 heldout observation invalidates the
  roster and requires a fresh versioned authority.
  The following H=4096 carrier, artifact, and publication contracts remain
  authoritative for validation of already captured material. Their existing
  Session-v4 execution surfaces are suspended by the Session-v5 boundary; any
  future executable successor must satisfy these constraints or revise them
  through an explicit versioned authority.
  The first separately reviewed H=4096 executable authority is frozen by
  `docs/adr/ADR-053-phase4-confirmatory-h4096-raw-authority.md`. The compiled
  production `phase4_confirmatory_h4096_evidence_runner`, never a caller
  option, MUST select configuration authority
  `phase4_confirmatory_corpus_v2_h4096`, require explicit Corpus 2, and accept
  exactly exact cell `(10100,4)` through atomic same-run Raw Wire 2 with its
  Telemetry Wire 2 companion or calibration cell `(10200,8)` through ordinary
  Raw Wire 1. The separately named
  `phase4_confirmatory_h4096_raw_evidence_validator` MUST accept only the
  ordinary calibration identity. The separately named
  `phase4_confirmatory_h4096_same_run_decision_telemetry_validator` MUST
  completely authenticate exact-cell Raw before opening, authenticating, and
  joining its same-invocation telemetry companion. Both validators MUST bind
  Protocol v2, canonical algorithm-budget roster v3, the unchanged
  Representative Corpus v2 case and workload authorities, and one
  independently supplied expected clean source commit.
  The frozen Raw schemas provide no separately authenticated authority for an
  H=4096 cell containing only failed arm attempts. H=4096 Raw authentication
  MUST require at least one completely authenticated successful arm witness;
  an all-failure capture is deliberately non-authenticatable. This requirement
  MUST NOT weaken complete ordinary publication or the exact Raw-plus-telemetry
  join.
  The public controller and both hidden worker modes MUST independently reject
  every other configuration, case, pool, and carrier assignment before fixture
  access. The runner MUST invoke the complete pure carrier-specific H=4096
  preflight for the controller and each hidden worker before source or fixture
  checks. The controller library MUST repeat the literal two-cell carrier
  boundary and positively require equal-arm `present=1,history=4096` before
  worker launch or representative-case construction. Each worker MUST repeat
  that configuration and canonical-spec validation before preparer creation,
  warmup, allocator access, or request decoding. The historical H=2250/H=4096
  carrier distinction remains a pure validation invariant. The shared
  Session-v5 closure now precedes both configurations at every execution
  boundary.
  The test-only `phase4_confirmatory_h4096_evidence_test_runner` MUST be
  permanently nonpublishable and fixtureless. It declares no board fixture,
  cannot enter either authorized execution path, and may exercise only parsing
  and pre-access firewall rejection. It MUST reject before fixture resolution,
  case construction, preparer creation, worker launch, or artifact
  serialization even when its embedded source is clean and stamped; no testing
  option, caller commit, or source state may make its output publishable.
  A production-shaped test-only source-firewall target MAY compile-force only
  its embedded source gate to an unpublishable state for deterministic
  clean-build hidden-worker probes. It MUST declare no fixture data, MUST NOT
  alter the production target's actual embedded source fields, and MUST NOT
  emit evidence.
  Ordinary evidence retains Raw Evidence schema 1 and Raw Wire 1. Same-run
  evidence retains Raw Evidence schema 2 and Raw Wire 2, and its companion
  retains Same-Run Decision Telemetry schema 1 and Telemetry Wire 2. The
  reserved H=4096 `v2` artifact authorities do not select or revise those
  payload or carrier schemas. Production evidence MUST bind a clean stamped
  source whose explicit runtime commit equals the embedded commit, and H=2250
  evidence cannot be promoted, relabeled, or rechecksummed. H=4096 report,
  operational, replay, snapshot, exact-small Oracle, fixed-query, stress,
  aggregation, matrix, decision, heldout, and imported paths remain closed
  pending their own separately reviewed authorities; Raw development execution
  does not authorize campaign acquisition or complete Phase 4.
  The first downstream H=4096 authority is the ordinary per-net publication
  join frozen by
  `docs/adr/ADR-054-phase4-confirmatory-h4096-per-net-report.md` and
  `schemas/benchmark/phase4_confirmatory_per_net_report_publication_join_v2.md`.
  Its separately compiled production runner MUST select H=4096 out of band,
  accept only Corpus 2 ordinary `(10200,8)` over Raw Wire 1, positively require
  equal-arm `present=1,history=4096` and canonical algorithm-budget checksum
  `8230401457668518004`, and expose no fixture path or fixture runfile
  capability. Existing V1 and Corpus-v2/H=2250 report entry points MUST remain
  unchanged and reject H=4096 semantics.
  Per-Net Report Artifact schema 1, its checksum domains, complete 64-net
  telemetry, and permanent `decision_eligible=false` value remain unchanged;
  the Protocol-v2 `v2` name versions the configuration-specific publication
  authority rather than the payload. The test runner MUST be permanently
  preflight-only and unable to emit a report.
  The publication validator MUST positively bind the reserved Protocol-v2
  ordinary per-net authority, completely authenticate H=4096 Raw `(10200,8)`
  before opening the bounded regular report, and then require exact source,
  configuration, Raw, repetition-zero arm, complete semantic, telemetry, and
  ordered workload-roster equality. Raw, report, and independently supplied
  expected commit MUST name the same clean source; earlier Raw evidence cannot
  be relabeled or joined across commits.
  The H=4096 exact same-run per-net publication join is separately frozen by
  `docs/adr/ADR-055-phase4-confirmatory-h4096-same-run-per-net-report.md` and
  `schemas/benchmark/phase4_confirmatory_same_run_per_net_report_publication_join_v2.md`.
  Its production runner MUST select H=4096 out of band, accept only Corpus 2
  exact `(10100,4)` over Raw Wire 2, positively require equal-arm
  `present=1,history=4096` and canonical algorithm-budget checksum
  `8829615204625848656`, and expose no fixture path or fixture runfile
  capability. Existing H=2250 same-run and H=4096 ordinary report entry points
  MUST reject its semantics. The H=4096 same-run test runner MUST be
  permanently preflight-only and unable to emit a report.
  The three-way publication validator MUST positively bind the reserved
  Protocol-v2 same-run per-net authority, completely authenticate exact
  H=4096 Raw-v2/Wire-2 `(10100,4)`, then completely join its bounded regular
  H=4096 same-run telemetry companion, and only then open the bounded regular
  report. Raw, sidecar, report, and independently supplied expected commit
  MUST name the same clean source. Raw remains outcome/timing authority,
  same-run telemetry remains exact-rejection guardrail authority, and a valid
  failed guardrail cannot be repaired or offset by the diagnostic report.
  The H=4096 exact same-run operational authority is separately frozen by
  `docs/adr/ADR-056-phase4-confirmatory-h4096-same-run-operational-measurement.md`
  and
  `schemas/benchmark/phase4_confirmatory_same_run_operational_measurement_publication_v2.md`.
  Its separately compiled capture, worker, and publication validator MUST
  select H=4096 out of band and accept only explicit Corpus 2 exact
  `(10100,4)`, Raw Evidence schema 2 over Raw Wire 2, Same-Run Decision
  Telemetry schema 1 over Telemetry Wire 2, four workers, 20 repetitions,
  equal-arm `present=1,history=4096`, and canonical algorithm-budget checksum
  `8829615204625848656`. Existing H=2250 and ordinary operational entry points
  MUST reject that authority before fixture access, case construction,
  preparer creation, warmup, or replay.
  Worker Output v1, Operational Measurement Capture v1, Operational Profile
  v1, Replay Authority v1, and final publication payload schema 1 remain
  unchanged. The Protocol-v2 `v2` name selects only the H=4096 authority
  binding and domain-separated V2 publication artifact/source checksums;
  Protocol-v2 JSON MUST remain byte-unchanged, and the frozen payload formats
  MUST retain their existing schemas.
  The capture MUST retain the reviewed four-exec order, exact-child and
  descendant containment, process/provenance checks, launcher handshake,
  standalone-runfiles authority, and pinned H=4096 worker digest.
  The publisher MUST completely authenticate bounded regular H=4096 Raw first,
  then open and completely join its bounded regular H=4096 same-run telemetry
  companion. Only then may it resolve and hash the pinned worker or open and
  validate the bounded regular capture; only after that complete
  Raw/sidecar/capture source, configuration, and semantic join may it open a
  publication validation input or install output.
  Raw remains outcome/timing authority, same-run telemetry remains
  exact-rejection guardrail authority, and a structurally valid false
  guardrail remains publishable authentic negative evidence. The operational
  publication remains diagnostic, non-standalone, non-statistical, and
  incomplete coverage.
  After this implementation is adversarially reviewed and committed, exact
  Raw, sidecar, sibling per-net report, and operational capture/publication
  MUST be reacquired from that same clean commit. Earlier artifacts cannot be
  relabeled or joined across commits.
  Every other development cell, standalone replay, fixed-query, stress,
  aggregation, matrix, decision, heldout, and imported paths remain closed.
  The H=4096 ordinary operational authority is separately frozen by
  `docs/adr/ADR-058-phase4-confirmatory-h4096-operational-measurement.md` and
  `schemas/benchmark/phase4_confirmatory_operational_measurement_publication_v2.md`.
  Its separately compiled capture, worker, and publication validator MUST
  select H=4096 out of band and accept only explicit Corpus 2 calibration
  `(10200,8)`, Raw Evidence schema 1 over Raw Wire 1, four workers, 20
  repetitions, equal-arm `present=1,history=4096`, canonical algorithm-budget
  checksum `8230401457668518004`, and paired semantic
  `budget_checksum=12108149041077564710`. It MUST positively authenticate
  Confirmatory Decision Protocol v2 checksum `11520586171987743043`, canonical
  algorithm-budget roster v3 checksum `18429170436700418962`, configuration
  authority `phase4_confirmatory_corpus_v2_h4096`, and Raw binding
  `phase4_confirmatory_raw_evidence_v2`. Existing H=2250 and H=4096 same-run
  operational entry points MUST reject this authority before fixture access,
  case construction, preparer creation, warmup, or replay.
  Worker Output v1, Operational Measurement Capture v1, Operational Profile
  v1, Replay Authority v1, and final publication payload schema 1 remain
  unchanged. The Protocol-v2 `v2` name selects only the H=4096 authority
  binding and the domain-separated
  `APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2`
  and
  `APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2`
  checksum domains; Protocol-v2 JSON MUST remain byte-unchanged.
  The capture MUST retain the reviewed measured-baseline, measured-candidate,
  unmeasured-baseline-authority, unmeasured-candidate-authority exec order,
  exact-child and descendant containment, process/provenance checks, launcher
  handshake, complete adjacent standalone-runfiles authority, and pinned
  H=4096 worker inode and digest. Capture and publication MUST independently
  hash the fixed
  `phase4_confirmatory_h4096_operational_replay_worker`; caller paths, ambient
  runfiles, working directories, enclosing test trees, arguments, and opaque
  checksums MUST NOT substitute an executable or authority.
  The publisher MUST completely authenticate bounded regular H=4096 ordinary
  Raw `(10200,8)` before resolving the worker or opening and validating the
  bounded regular capture. Only after the complete Raw/capture source,
  configuration, process, replay, and semantic join may it open a publication
  validation input or install output. No Same-Run Decision Telemetry sidecar
  is accepted or opened. Raw remains the sole outcome/timing authority; the
  operational publication retains `cell_role="calibration"` and remains
  diagnostic, non-standalone, non-statistical, and incomplete coverage.
  After this implementation is adversarially reviewed and committed, ordinary
  Raw, its sibling per-net report, and the operational capture/publication
  MUST be reacquired from that same clean commit. Earlier artifacts cannot be
  copied, relabeled, rechecksummed, or joined across commits.
  The H=4096 exact-small Oracle authority is separately frozen by
  `docs/adr/ADR-057-phase4-confirmatory-h4096-exact-small-oracle.md` and
  `schemas/benchmark/phase4_confirmatory_exact_small_oracle_publication_v2.md`.
  Its separately compiled production snapshot runner and publication validator
  MUST select H=4096 out of band and accept only explicit Corpus 2 exact
  `(10100,4)`, Raw Evidence schema 2 over Raw Wire 2, Same-Run Decision
  Telemetry schema 1 over Telemetry Wire 2, four workers, 20 repetitions,
  equal-arm `present=1,history=4096`, and canonical algorithm-budget checksum
  `8829615204625848656`. It MUST independently require the Raw/Snapshot paired
  semantic `budget_checksum=5851813264366095594`; neither budget identity may
  be derived from or substituted for the other. Existing H=2250 snapshot,
  replay, and Oracle entry points MUST remain unchanged and reject H=4096
  before fixture access, case construction, preparer creation, candidate
  execution, replay, or enumeration. Protocol-v2 JSON MUST remain
  byte-unchanged.
  Exact-Small Snapshot v1 retains its frozen payload schema, component and
  output bounds, canonical key order, artifact checksum domain, and source
  envelope checksum domain. The H=4096 production snapshot target MUST be
  fixtureless because case 10100 is generated. Its test target MUST remain
  permanently preflight-only and unable to construct a case, create a
  preparer, execute a candidate arm, serialize a snapshot, or emit evidence.
  The Oracle output likewise retains payload schema 1 and its fixed key order.
  The Protocol-v2 `v2` authority selects only the H=4096 binding values and
  domain-separated V2 Oracle publication artifact/source checksums.
  Before authenticating runfiles, delegating to Python, or opening any input,
  the H=4096 production publisher's compiled launcher MUST require its
  generated source stamp to identify a clean stamped tree and MUST require
  exactly one well-formed independently supplied expected commit equal to the
  embedded build commit. The compiled launcher MUST reject abbreviated
  expected-commit spellings, and the inner parser MUST disable long-option
  abbreviation so a second spelling cannot replace the checked commit after
  delegation. Missing, duplicate, abbreviated, malformed, unstamped, dirty, or
  mismatched source identity MUST fail without input access.
  Fixed-source test-only launcher variants MAY exercise this firewall
  deterministically, but they MUST terminate inside the compiled boundary
  immediately after source and argument preflight. They MUST declare no inner
  Python target or runfiles authority, MUST NOT depend on the production
  validator or replay helper, and MUST remain incapable of opening inputs,
  executing replay or enumeration, constructing an artifact, or emitting an
  Oracle Artifact. Existing H=2250 and ordinary compiled launchers retain
  their current source behavior.
  The publisher MUST completely authenticate bounded regular H=4096 Raw first,
  then open and completely join its bounded regular H=4096 same-run telemetry
  companion. Only then may it open and completely join the bounded regular
  H=4096 Wire-2 report; only after the report joins may it open and completely
  validate the bounded regular Snapshot v1. Only after all four inputs join
  may it invoke the fixed separately compiled
  `phase4_confirmatory_h4096_exact_small_candidate_admission_replay`. That
  helper's private replay wire v3 MUST bind Corpus 2, case 10100, pool four,
  equal-arm `present=1,history=4096`, canonical algorithm-budget cell checksum
  `8829615204625848656`, and paired semantic budget checksum
  `5851813264366095594` before case construction or candidate traversal. It
  MUST independently pin both checksums to their proper preimages, MUST NOT
  conflate them, MUST rebuild only that case, replay every non-authenticating
  exact candidate-admission check, require exact EOF, and expose no
  caller-selectable corpus, configuration, case, helper, or executable
  authority. Complete bounded enumeration may begin only after replay
  succeeds.
  Production and exhaustive objectives MUST be exactly equal before an Oracle
  Artifact can be emitted. The reviewed H=4096 production objective
  `(6,1,159000)` is motivation, not proof; Raw, telemetry, report, and snapshot
  MUST be freshly reacquired from the same clean implementation commit and the
  optimum MUST be freshly enumerated. A mismatch emits no completion authority
  and leaves the campaign incomplete, never an allocator loss.
  One remaining overused resource and one overuse unit are compatible with
  fixed-pool optimality; objective equality MUST NOT be presented as resource
  feasibility. A structurally valid false exact-rejection guardrail remains
  publishable authentic negative evidence when the fixed-pool proof otherwise
  completes. This authority proves neither candidate-pool route completeness,
  operational performance, combined-board legality, another cell, heldout or
  imported execution, confirmatory matrix completion, Phase 4 completion, nor
  M1 completion. Fixed-query, stress, aggregation, matrix, decision, heldout,
  imported, and every other development path remain closed.
  The reviewed H=4096 development observation and remediation boundary are
  recorded by
  `docs/adr/ADR-059-phase4-confirmatory-h4096-development-observation.md` and
  `docs/evidence/phase4-confirmatory-h4096-development-b57f885.md`. Exact
  development cell `(10100,4)` proves production equality with one unique
  optimum only inside its captured pools and still retains one overuse unit.
  Independently acquired ordinary calibration cell `(10200,8)` prefers the
  zero-resource-overuse baseline in all 20 balanced pairs because the candidate
  introduces 16 overuse units. The two observations bind different clean
  source commits and MUST NOT be cross-joined, relabeled, rechecksummed, or
  treated as one timing population.
  This negative result keeps the confirmatory campaign closed. Protocol v2,
  canonical algorithm-budget roster v3, and all observed artifacts remain
  byte-unchanged. Remediation MUST address general candidate generation,
  retention, pricing, regeneration, or allocation behavior through open
  development cells; authenticated thin-pool and rejection diagnostics MUST
  NOT be presented as unique causal proof. Any checksum-bound configuration,
  case, budget, role, carrier, threshold, or authority-preimage change requires
  a separately versioned contract before execution. Each implementation
  remediation MUST pass adversarial review and be committed before fresh
  source-bound acquisition. Before any heldout-acquisition authority is
  proposed, the exact cell MUST retain independent production/fixed-pool
  objective equality and the calibration candidate MUST no longer
  lexicographically regress its baseline. That condition is remediation
  readiness only; it proves neither campaign success, resource feasibility,
  Phase 4 completion, nor M1 completion. Fixed-query, stress, aggregation,
  matrix, decision, heldout, imported, and every other development path remain
  closed pending separately reviewed authority.
  Complete observed aggregation is versioned by
  `schemas/benchmark/phase4_matrix_decision_publication_v1.md`. The aggregator
  MUST derive its exact external bundle inventory from Protocol v4, fully
  rebuild every Raw/report/capture/operational/oracle/fixed/stress authority
  against one independently supplied clean commit, and bind all 487 evidence
  files. Missing, malformed, foreign, failed, or wrongly disposed evidence is
  incomplete and MUST NOT produce a decision publication or an allocator loss.
  A complete authentic negative matrix MUST produce a checksummed failed
  publication. Only Raw owns outcome and paired timing; only Same-Run
  Telemetry owns exact-rejection guardrails; operational replays remain
  diagnostic. `phase4_complete=true` requires complete 104-cell coverage, all
  guardrails, at least two Holm-qualified primary families including the
  globally coupled family, and all nine frozen completion authorities.
  The V1 publication remains historical after a V2 campaign begins. Phase 4
  may close from Corpus v2 only through a separately versioned confirmatory
  decision publication that fully rebuilds its own frozen authority chain and
  reports both `phase4_exit_status=passed` and `phase4_complete=true`.

This Phase 4 gate establishes resource-feasible candidate allocation, not final
board legality. Exact combined-geometry legalization, APGAR DRC, host-CAD
validation, shove, tuning, differential pairs, buses, and other deferred
specialty behavior remain governed by Phases 5 and 7. Individually selected
candidates remain subject to the existing exact admission and GPU trust
boundaries throughout Phase 4.

## 30. Risks and Mitigations

| **Risk** | **Mitigation** |
| --- | --- |
| Raster conservatism blocks important routes | Measure false-blocked space; refine tiles/corridors; add topology/gridless generators. |
| Exact legalization dominates runtime | Build incremental broad phase early; batch exact checks; preserve movable guide degrees of freedom. |
| Candidate pool lacks diversity | Resource/topology diversity metrics; forced bans; multiple generators; targeted column generation. |
| Allocator oscillates or stalls | Historical pricing, multi-world schedules, stall diagnostics, candidate insufficiency detection. |
| GPU memory grows with board dimensions | Sparse tiles, procedural adjacency, ROI batching, eviction, CPU fallback. |
| Cost model produces ugly legal routes | Pareto metrics, user objective profiles, explainability, quality corpus reviews. |
| CAD semantics disagree | Host validation is authoritative; retain disagreement regressions; adapter conformance suites. |
| Specialty routing forces redesign | Finite-state extension and specialty candidate interfaces are specified before implementation. |

## 31. Open Architecture Questions

| **ID** | **Question** | **Proposed experiment / decision trigger** |
| --- | --- | --- |
| OQ-01 | What tile sizes and resolution hierarchy minimize total work? | Sweep board families across occupancy and feature scales. |
| OQ-02 | How many heading states are needed in the first production mode? | Compare H/V, octilinear, and legalizer-assisted variants. |
| OQ-03 | Can heading-aware sweeps converge efficiently on PCB-like tortuous paths? | Kernel bakeoff with turn-count-controlled mazes. |
| OQ-04 | What resource granularity best predicts exact conflicts? | Correlate allocator overuse with legalization failures. |
| OQ-05 | What candidate similarity metric best predicts global value? | Ablate pruning metrics and measure final quality. |
| OQ-06 | How many multi-world states provide useful marginal quality? | Quality-versus-world-count curves under fixed budgets. |
| OQ-07 | How should movable existing copper be represented for shove? | Prototype transaction and dependency semantics before geometry optimization. |
| OQ-08 | Which internal DRC rules must be production-complete before adoption? | Prioritize by host rejection frequency and runtime contribution. |

## 32. Engineering Governance

- Architecture decisions are recorded as ADRs with status, rationale, alternatives, and consequences.
- Every performance claim includes a corpus, configuration, hardware, and exact commit ID.
- A published benchmark source identity is a reproducibility binding under the
  canonical checked-in Bazel invocation on a trusted runner, not a
  cryptographic attestation against an operator who controls the Bazel command,
  executable search path, workspace-status command, or repository metadata.
  Such overrides define a different, untrusted build and require independent CI
  or build-provenance attestation before publication.
- Every new GPU optimization requires differential tests against a reference path.
- Every serialized schema change has compatibility tests.
- Every new route-quality objective includes an explainable metric and regression visualization.
- No unsupported design rule may silently degrade to a weaker interpretation.

## 33. Research-to-Architecture Traceability

| **Source** | **Adopted insight** | **APGAR adaptation** |
| --- | --- | --- |
| GAMER [R1] | GPU-friendly directional sweeps and prefix-style propagation | Heading-aware H/V/diagonal candidate generator; benchmarked rather than universal. |
| InstantGR [R2] | DAG route alternatives, resource-aware parallel batching, flexible layer transitions | Candidate/topology representation and footprint-aware parallel allocation. |
| PathFinder [R3] | Negotiated present and historical congestion; temporarily infeasible states | Candidate-level resource prices and multi-world selection. |
| OrthoRoute [R4] | Direct GPU PCB routing viability and practical full-lattice memory pressure | Avoid full explicit graph; expose net/world parallelism beyond per-net SSSP. |
| 3D LineExplore [R5] | Adaptive continuous-space exploration and multilayer routing | Optional adaptive line-exploration generator and topological portals. |
| PBA [R6] | Exact GPU Euclidean distance transform | Advisory distance/margin fields. |
| OpenDRC [R7] | Layer hierarchy, adaptive partition, edge-based GPU checks | Incremental broad phase and batched exact-rule kernels. |
| PCBWorld [R8] | Engine-grounded evaluation on synthetic and real KiCad boards | External benchmark and host-verified metric strategy. |

## 34. References

**[R1]** S. Lin, J. Liu, E. F. Y. Young, and M. D. F. Wong, “GAMER: GPU-Accelerated Maze Routing,” IEEE TCAD, vol. 42, no. 2, pp. 583-593, 2023. DOI: 10.1109/TCAD.2022.3184281. https://doi.org/10.1109/TCAD.2022.3184281

**[R2]** L. Xiao, S. Lin, J. Liu, Q. Duan, T.-Y. Ho, and E. F. Y. Young, “InstantGR: Scalable GPU Parallelization for 3-D Global Routing,” IEEE TCAD, vol. 45, no. 1, pp. 441-452, 2026. DOI: 10.1109/TCAD.2025.3573685. https://doi.org/10.1109/TCAD.2025.3573685

**[R3]** L. McMurchie and C. Ebeling, “PathFinder: A Negotiation-Based Performance-Driven Router for FPGAs,” in Reconfigurable Computing, pp. 365-381. DOI: 10.1016/B978-012370522-8.50024-8. https://doi.org/10.1016/B978-012370522-8.50024-8

**[R4]** B. Benchoff, “OrthoRoute: A GPU-accelerated PCB autorouter for KiCad,” source repository and project documentation. https://github.com/bbenchoff/OrthoRoute

**[R5]** N. Sun et al., “3D LineExplore: a 3D line exploration method for multi-layer PCB geometric routing,” Scientific Reports, vol. 16, article 6588, 2026. DOI: 10.1038/s41598-026-36925-0. https://doi.org/10.1038/s41598-026-36925-0

**[R6]** T.-T. Cao, K. Tang, A. Mohamed, and T.-S. Tan, “Parallel Banding Algorithm to Compute Exact Distance Transform with the GPU,” SI3D 2010. DOI: 10.1145/1730804.1730818. https://doi.org/10.1145/1730804.1730818

**[R7]** Z. He, Y. Zuo, J. Jiang, H. Zheng, Y. Ma, and B. Yu, “OpenDRC: An Efficient Open-Source Design Rule Checking Engine with Hierarchical GPU Acceleration,” DAC 2023. https://github.com/opendrc/opendrc

**[R8]** H. Song et al., “PCBWorld: A Benchmark Environment for Engine-Grounded PCB Design Automation,” arXiv:2607.05915, 2026. https://arxiv.org/abs/2607.05915

**[R9]** CUHK-EDA XPlace GPU global routing implementation, including CUDA maze-routing sweep kernels. https://github.com/cuhk-eda/Xplace

## Appendix A. Proposed Repository Layout

```text
apgar/
  cmake/
  docs/
    architecture/
    adr/
    research/
  include/apgar/
    board_ir/
    geometry/
    compiler/
    routing/
    candidates/
    allocator/
    legalizer/
    drc/
    session/
    adapters/
  src/
    ... matching public modules ...
  gpu/
    common/
    cuda/
      scans/
      frontier/
      distance/
      drc/
  adapters/
    kicad/
  tools/
    apgar-cli/
    replay-viewer/
    benchmark/
  tests/
    unit/
    property/
    differential/
    integration/
    corpus/
  benchmarks/
    manifests/
    synthetic/
  schemas/
    board_ir/
    replay/
    telemetry/
```

## Appendix B. Candidate Lifecycle State Machine

```text
Generated
  -> normalized
  -> exact-validated
      -> rejected (diagnostic retained)
      -> accepted
          -> stored
          -> selected by zero or more worlds
              -> legalization pending
                  -> legal
                      -> host validation
                          -> committable
                          -> host rejected
                  -> legalization failed
          -> pruned when unreferenced and dominated
```

## Appendix C. Definition of Done for M1

- Board IR and schema documented and versioned.
- KiCad fixture adapter imports selected test boards.
- Exact geometry predicates have property and boundary tests.
- Sparse tiles and directional masks pass conservatism tests.
- CPU reference A* produces reproducible paths.
- At least one GPU generator matches CPU path cost on supported exact microcases.
- Candidate store records provenance, geometry, resources, metrics, and diversity.
- Exact candidate checker rejects every injected violation.
- Benchmark report includes time, VRAM, quality, and diversity.
- One replay bundle can reproduce a deliberately injected invariant failure.
