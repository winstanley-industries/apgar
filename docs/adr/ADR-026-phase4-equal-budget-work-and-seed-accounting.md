# ADR-026: Phase 4 Equal-Budget Work and Seed Accounting

**Status:** Accepted for the twelfth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Replay inputs and CPU route-work evidence required before the
paired equal-budget trial runner

## Context

The reusable candidate-allocation session and sequential baseline were both
bounded and replayable, but two evidence inputs were incomplete. Initial pool
preparation retained only the conservative maximum route-work opportunity and
discarded actual CPU A* work. Targeted regeneration derived policy seeds from
plan and configuration identity without an explicit caller root comparable to
the sequential baseline and initial preparation seed. A paired runner could
therefore enforce equal maxima but could not publish complete consumed-work or
root-seed ancestry.

No canonical Phase 4 equal-budget artifact has been published, so completing
these replay records does not invalidate prior evidence.

## Decision

- Preserve CPU Candidate-Pool Preparation v1 and adopt v2 before canonical evidence. Retain
  exact A* work units for each executed column, zero for skipped columns, and
  the widened aggregate sum in counters. Hash both into the replay checksum.
  The conservative preflight remains the opportunity budget and must still
  pass before dispatch.
- Add a caller-rooted `u64` deterministic seed as the first Targeted
  Regeneration Execution v3 configuration field. Mix the complete execution
  configuration into the existing per-target batch identity, then use that
  identity as the target policy seed. Hash the root through successful and
  failed execution observations and the composed session checksum.
- Preserve the version-2 seedless targeted-execution replay contract and the
  version-1 composed-session contract. Production rejects those legacy
  versions and uses Candidate-Allocation Session v2 with Targeted Execution v3.
- Return a checksum-covered failed Preparation v2 observation whenever a
  failure follows at least one started route query. It retains all actually
  attempted columns in canonical order and their available work. It hashes a
  CandidateStore-publication-committed bit and transfers the authoritative
  store into the observation on any post-commit failure; pre-commit failures
  retain no store.
- Require a paired trial to supply one declared root to the sequential
  baseline, initial preparation, and targeted regeneration. Domain-separated
  derived seeds remain valid and are expected to differ.
- Keep conservative opportunity caps and actual consumed work distinct.
  Candidate and sequential trials must receive equal query/work opportunity,
  stopping depth, wall-clock, and external memory authority; the report also
  publishes actual work from each algorithm rather than summing unrelated
  allocator, admission, and routing work units.

## Consequences

The next evidence runner can reject root-seed drift and report both accepted
CPU route-work opportunity and exact consumed A* work. Worker count remains
operational, and identical semantic inputs continue to produce identical work
records across worker counts.

This slice does not define trial ordering, wall-time measurement, external
memory enforcement, family statistics, GPU execution, combined-route
legalization, or a Phase 4 success decision.
