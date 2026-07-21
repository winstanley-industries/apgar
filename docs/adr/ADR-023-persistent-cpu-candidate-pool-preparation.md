# ADR-023: Persistent CPU Candidate-Pool Preparation

**Status:** Accepted for the ninth Phase 4 vertical slice; amended before
canonical evidence by ADR-026
**Date:** July 20, 2026
**Applies to:** Initial authentic `4/8/16` per-net candidate pools used by
Phase 4 allocation sessions

## Context

The representative corpus freezes realistic per-net pool sizes, but a corpus
descriptor is not an executable candidate session. Phase 4 timing also forbids
creating operating-system threads for each measurement invocation. Initial
pools must preserve the CPU A* oracle, exact candidate admission, per-net
association binding, deterministic semantics across worker counts, explicit
disconnected confirmation, and one atomic CAN-004 publication.

Unbounded repeated CPU searches are not an acceptable production seam. The
existing CPU oracle intentionally remained source-compatible and unbounded for
single-query callers, so the preparation slice needs an explicit bounded
overload without changing legacy results.

## Decision

- Preserve `schemas/allocator/cpu_candidate_pool_preparation_v1.md` and adopt
  `schemas/allocator/cpu_candidate_pool_preparation_v2.md` for production.
- Add per-query CPU A* bounds for work units, records, queue entries, and
  reconstruction states. A work unit is one queue pop, attempted legal-edge
  relaxation, or reconstructed state. Equality is accepted; a one-under bound
  fails with deterministic telemetry and no candidate evidence.
- Create a typed persistent CPU preparer with an explicit `1..64` worker count.
  It starts its workers once, reuses them across synchronous invocations, and
  rejects a concurrent invocation on the same preparer as busy. Worker count
  and completion order are operational telemetry, not semantic inputs.
- Preflight the complete net, query, aggregate route-work, concurrent record,
  queue, reconstruction, policy-entry, and CandidateStore transaction bounds
  plus conservative retained/generated candidate-byte bounds before
  dispatching the first job. Pool size is exactly one of the frozen Phase 4
  sizes `4`, `8`, or `16`.
- Run two worker waves. The first runs one authentic bounded CPU A* query for
  every canonical workload net. A reached base route supplies the exact sorted
  physical-edge roster derived only from its producer-authenticated segment
  sequence for the existing deterministic alternative-policy schedule; the
  diagnostic lattice trace is not consumed. The complete route is destroyed
  inside its worker after draft construction. The base edge roster is allocated
  at its exact decoded size rather than through geometric vector growth. The second wave runs the
  remaining alternatives. A disconnected or unsupported base is one explicit
  proof followed by structured skipped columns, not repeated equivalent
  searches.
- Workers write only to preallocated canonical column slots. They do not
  mutate the CandidateStore, global prices, occupancy, or the workload.
  Worker exceptions are resolved in canonical column order.
- Give every invocation a fresh owned CandidateStore. Submit all successful
  authenticated CPU drafts and ordinary generation rejections through one
  conditional invocation against the complete empty per-net source roster.
  Exact admission, rejection retention, deduplication, pruning, and pool
  publication therefore remain one CAN-004 transaction.
- Return a move-only result owning the store, explicit empty or populated pool
  for every workload net, complete ordered column records, counters, semantic
  batch identity, and replay checksum. It retains no borrowed Board or workload
  reference.
- Retain exact CPU A* work units on every executed canonical column and their
  widened aggregate sum. Ordinary failures use available failure telemetry;
  skipped alternatives retain zero. Both column and aggregate work are replay
  fields, independent of the conservative opportunity preflight.
- A fatal error after query start retains a checksum-covered, canonical roster
  of every attempted query in the completed worker wave, its available route
  work and route result stage. Every later host, synthesis, staging,
  publication, correlation, and assembly error carries the same bounded
  observation. It hashes an authoritative publication-committed bit; a
  post-commit error transfers ownership of the committed CandidateStore into
  the observation, while a pre-commit error owns no store. Preflight and
  pre-query failures remain plain errors.

## Consequences

Initial Phase 4 pools now come from production CPU A*, typed producer evidence,
exact admission, and a shared persistent execution resource. Repeating a
preparation creates a fresh store but identical semantic output; changing only
worker count cannot change batch identity, ordered columns, candidates,
counters, or checksum.

The conservative preflight charges every requested column its full route-work
budget even when a base proof later skips alternatives. It separately bounds
concurrent reconstruction states, exact-sized retained base-resource rosters, each generated draft,
and all drafts awaiting atomic publication. CandidateStore byte and
exact-admission work are checked again from the generated invocation before
publication and can still fail atomically. Operational wall time, CPU
utilization, and peak resident memory are intentionally deferred to the frozen
evidence runner.

This slice does not negotiate prices, select a sequential baseline, execute a
complete allocation session, add a GPU generator, legalize combined routes, or
claim Phase 4 success.
