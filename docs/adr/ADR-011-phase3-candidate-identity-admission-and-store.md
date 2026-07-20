# ADR-011: Phase 3 Candidate Identity, Admission, and Store

**Status:** Accepted
**Date:** July 19, 2026
**Applies to:** RouteCandidate v1, exact admission, rejection diagnostics, and
deterministic candidate storage

## Context

Phase 2 returns exactly validated planar routes but has no durable candidate
contract. Letting generator or GPU output become a stored route directly would
make compiled masks, self-reported costs, resource hashes, and implementation
object layouts de facto legality and serialization authorities. The global
allocator planned for Phase 4 also requires immutable reusable candidates,
bounded storage, collision-safe deduplication, and a retention seam before any
world state exists.

Board IR v1 does not yet contain exact via templates or via-padstack checking.
Candidate geometry nevertheless needs a stable primitive union that can add
through-via validation later without silently accepting invented transitions
now.

## Decision

- RouteCandidate v1 is defined first by
  `schemas/candidate/route_candidate_v1.md`. It carries a stable 128-bit ID,
  exact net and terminal references, independent Board/compiler/routing/rule
  associations, complete deterministic generator policy/provenance, canonical
  exact geometry, compressed physical-edge resources, reconstructed metrics,
  constraints, versioned signatures, checksum, and deterministic logical-byte
  accounting.
- Geometry v1 supports ordered exact line segments and reserves a tagged
  through-via primitive. Admission returns `Unsupported` for every through-via
  primitive until exact via-template compilation and validation exist. Arcs,
  branches, multipin trees, and arbitrary angles are incompatible with v1.
- Candidate admission follows generated, normalized, exact-validated,
  resource-accounted, signed/deduplicated, and stored stages. It independently
  verifies intended terminals, ordering/connectivity, signed coordinates,
  H/V/45 headings, exact swept Board IR clearance, associations, policy scalar
  cost, policy-independent intrinsic base cost, metrics, physical-edge
  resources, canonical form, signatures, checksum, and memory accounting.
- CompiledBoard and generator/GPU results may reject or guide work but cannot
  prove admission legality. Every exact rejection retains a versioned
  structured diagnostic.
- Provenance crosses typed producer boundaries. The CPU builder derives the
  CPU A*/CPU/device-class fields, accepts only explicit batch/query scheduling
  identities, and requires exact route evidence sealed by the CPU A* result
  path. Its deliberately malformed-route reseal seam exists only in a
  source-private Bazel `testonly` support library; production targets and the
  installed public API cannot depend on it. Source code intentionally depending
  on APGAR-private implementation headers is trusted implementation code, not a
  hostile runtime caller. The GPU builder requires the validated batch, query,
  item, route, backend, and immutable-device-view envelope to agree before it
  derives CUDA provenance. Public construction and mutation helpers produce
  only unsealed diagnostic items. Successful GPU items become opaque
  host-validation capabilities by moving the result into a separately
  allocated, truly const evidence snapshot that binds the DeviceCandidateBatch
  schema version and batch ID; every public mutation helper then fails without
  changing the snapshot. CUDA candidate provenance additionally requires an
  authenticated prepared view created for the exact final CUDA wrapper whose
  constructor and delegate are private to the checksum-pinned CUDA factory.
  The exact-type check and seal decision are defined in the always-linked core,
  so an optional CUDA library does not expose an otherwise-undefined public
  seal-minting friend. Generic backends and wrappers remain ineligible even
  when they report CUDA-looking metadata or forward execution to the real
  backend.
  Public batch metadata is checked for consistency but cannot authenticate a
  route by itself. Copies share the immutable evidence, but query/policy
  attribution remains mandatory. Callers therefore cannot relabel a CPU route
  as GPU output, fabricate CUDA evidence from self-consistent public fields, or
  swap equal-looking GPU results between queries. Input ordinal is a scheduling
  identity and is intentionally independent of candidate ordinal.
  CPU `lattice_path` and telemetry remain redundant diagnostic outputs rather
  than authenticated candidate semantics; candidate construction consumes the
  sealed exact segment sequence only.
- Builder failures are Candidate Rejection v1 records rather than a smaller
  error type. Normalization, exact validation, resource reconstruction, metric,
  and accounting diagnostics retain all available identity, association,
  provenance, witness, conflicting Board IR entity, checksum, and deterministic-byte evidence and
  enter the store's bounded rejection history.
- Public policy containers remain untrusted even when wrapped in a claimed
  normalized-policy aggregate. Policy normalization, CPU/GPU candidate
  builders, and direct admission check banned, penalized, and aggregate entry
  counts in O(1) before copying, sorting, iterating, or hashing resource bulk.
  Over-limit diagnostics omit the candidate-payload checksum because invalid
  bulk is not traversed merely to diagnose its bound violation.
  Store transaction preflight applies the same check to every generated and
  request policy before exact input-byte accounting; a batch reports the
  maximum invalid aggregate count so permutations remain equivalent.
- Physical resources v1 are collision-free structured canonical compiled-edge
  spans with unit usage. Reverse traversal names the same capacity unit, and a
  candidate that reuses a physical edge is rejected as noncanonical. Phase 3
  costs are nonnegative, so this removes useless loops without losing a
  shortest-path alternative. This is deliberately narrower than the future
  allocator resource vocabulary.
- Canonical line geometry visits each same-layer path vertex once and permits
  only consecutive lines to share their ordered endpoint. Every nonconsecutive
  same-layer swept trace pair requires exact centerline distance at least the
  nominal trace width; equality is legal. Crossings, point self-touches, and
  sub-width separation fail exact admission. Positive-length collinear overlap
  remains classified later as repeated physical-edge resource use.
- Exact self-clearance uses a deterministic x-bound sweep with a v1 maximum of
  `1,000,000` broad-phase pair inspections per admission. Candidate pairs count
  before layer, adjacency, and y-bound filtering once reached by the sweep;
  attempting the next pair fails with structured `BudgetExhausted` evidence.
  The hostile-test seam may only reduce this production cap.
- Geometry and resource signatures are versioned 128-bit lookup accelerators.
  Signature matches always receive canonical equality checks before duplicate
  classification.
- The store enforces positive per-net candidate-count and logical-byte budgets,
  deterministic ranking/enumeration/pruning, resource and geometry diversity,
  and metric dominance. Immutable candidates are separate from mutable store
  metadata. A store instance binds to one complete Board/compiler/routing/rule
  association set when its first exact-admitted RouteCandidate enters
  publication. That session binding persists even if later duplicate, budget,
  or pinned-rollback processing retains no candidate. Association drift is
  rejected rather than mixing stale and current candidates under the same net
  identity.
- Retained-pool caps do not bound hostile admission work. Store v1 therefore
  also enforces positive per-transaction item-count, recomputed aggregate-input-
  byte, and conservative deterministic-work caps before exact admission. Input
  and work accounting includes both the policy carried by each generated
  candidate and the independently supplied request policy. A shared-request
  overload checks the repeated request-policy bytes and entry work in O(1),
  but those repeated quantities are conservative schema accounting rather than
  physical-copy requirements. The caller-owned request remains referenced, its
  policy is normalized exactly once, and that immutable result is reused while
  every generated candidate policy and payload remains independently checked.
  Exact typed equality to the verified normalized request policy avoids a
  redundant candidate-policy sort/copy; a differing candidate policy falls
  back to full independent normalization before request association is tested.
  A transaction-wide cap,
  configuration, or accounting failure emits and retains one candidate-less
  structured rejection and performs no exact admission or publication. The
  work formula is versioned by the store schema rather than inferred from
  implementation allocations.
- Accepted candidates live in deterministic per-net pools with a separate
  ordered global candidate-ID index. A publication stages and recomputes only
  touched pools under one mutex, then atomically updates those pools and the ID
  index. Unrelated nets are neither copied nor compared. A pinned-budget
  failure rolls the complete candidate publication back, preserving the prior
  linearizable snapshot.
- Duplicate groups are decided once over the complete touched pool. Pins remain
  the only override; otherwise only the total stable-rank winner is eligible.
  Representative selection, including same-ID selection, precedes individual
  candidate byte eligibility. Candidate logical bytes are not a
  representative-ranking key, and failure to retain the winner never promotes
  a lower-ranked smaller duplicate. Signature matches remain lookup hints
  followed by canonical collision-safe equality.
  Candidate identity is stricter: an ID already present in the global index is
  an immutable incumbent and every incoming same-ID payload is rejected without
  replacement, even when the incoming payload has a better rank. Stable
  representative choice applies only among same-ID payloads first published in
  one batch.
- Candidate IDs and the versioned geometry/resource signatures index
  deterministic duplicate buckets. Exact canonical equality remains mandatory
  within a matching signature bucket, including deliberately collided
  signatures; a signature never becomes equality evidence. Unrelated winners
  are not compared during duplicate-group construction.
- Structured builder diagnostics that fail before exact store admission enter
  through an explicit single/batch rejection-retention seam. They share the
  store's canonical ordering and bounded rejection cap and do not mutate
  candidate pools. Candidate Rejection v1 fixes 1,024-byte UTF-8 bounds for the
  invariant ID, supported device class, and detail. One shared canonicalizer
  validates schema, enum tags, and O(1) text sizes by reference before copying
  caller text, then recomputes bytes; malformed public records become a bounded
  `candidate.rejection.ingestion.v1` diagnostic rather than retaining hostile
  text, incompatible tags, or a zero overflow sentinel.
  Batch rejection ingestion is independently capped at 1,024 records by
  default and accepts a non-owning span so it can check the count before any
  copy or mutex acquisition. An over-cap batch is not inspected and becomes
  one deterministic candidate-less
  `candidate.store.rejection_transaction.item_budget.v1` diagnostic returned
  directly to the caller as well as submitted to bounded history. Rejection
  retention compares all three strings in canonical length-then-byte order.
  Single records use canonical lower-bound insertion; a submitted batch is
  canonicalized and sorted once, then merged with retained history up to the
  configured cap.
  Candidate admission likewise collects exact-admission and store-publication
  diagnostics across the whole transaction, then performs one canonical
  sort/merge/truncate under the publication mutex rather than repeated vector
  insertion and shifting.
- CAN-002 is represented by owner-scoped retention pins. A pinned candidate is
  never pruned; Phase 3 does not define worlds, selection, congestion, or
  prices.
- Concurrent generation publishes through one explicit batch, which is sorted
  and serialized using stable total keys. Batch results do not depend on worker
  completion order, pointer identity, or unordered-container iteration.
  Separate single-item calls are race-safe and linearizable, but bounded
  retention is intentionally not specified as commutative across unknown
  future calls; their transaction linearization is semantic input.
  Schedule-independent callers must form one batch. This distinction is
  required because immediate bounded publication cannot be both commutative and
  forget rejected/pruned history: a later duplicate or byte-skewed candidate
  can otherwise make the optimal retained representative depend on unseen
  future input. V1 chooses explicit transactions over unbounded tombstones or a
  delayed-finalization store.

## Consequences

- A candidate can be reproduced, compared, and bounded without trusting the
  generator that produced it.
- Exact validation and resource reconstruction add visible end-to-end cost;
  Phase 3 benchmarks must report that cost separately from GPU execution.
- Candidate IDs, signatures, and checksums have distinct roles. None may be
  used alone as collision-proof equality or legality evidence.
- Admission transactions now have an explicit, reproducible denial-of-service
  boundary. Over-cap batches return one transaction-level diagnostic rather
  than allocating one result per rejected input.
- Duplicate publication lookup is ordered-map work plus exact comparisons only
  inside matching geometry/resource-signature collision buckets. The bounded
  Pareto and resource-diversity retention passes remain quadratic in a touched
  net's pool, but no publication work scales with candidates on unrelated nets.
  The global ID check is an ordered-index lookup.
- Candidate ranking and Pareto dominance compare the independently
  reconstructed intrinsic base cost and quality vector. Scalar policy costs
  remain authoritative for identical-policy CPU/GPU differential checks but
  are not compared across alternative objective identities.
- Resource spans are sufficient for the Phase 3 diversity experiment but do
  not implement Phase 4 allocator capacities, portals, prices, worlds, or
  column generation.
- The reserved via tag preserves format evolution while keeping exact
  through-via routing an explicit remaining M1 gap.
