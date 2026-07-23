# ADR-036: Phase 4 same-run decision telemetry

**Status:** Accepted
**Date:** July 20, 2026
**Applies to:** Exact-validation rejection guardrails for Phase 4 decision cells

## Context

Raw Evidence v1 deliberately excludes per-net column partitions. Per-Net
Report v1 reconstructs rich telemetry in a separate diagnostic execution and
is permanently non-decision-eligible. The frozen Phase 4 statistical protocol
requires exact-validation rejection counts from the actual decision
executions, for both arms and every repetition. Deterministic semantic equality
does not prove that rejection categories were identical inside a different
execution.

## Decision

- Preserve Raw Evidence v1 and Wire v1 unchanged.
- Supersede Statistical Decision Protocol v1 only at the evidence-authority
  layer. Protocol v2 incorporates the exact v1 checksum and retains every
  matrix, outcome, inference, guardrail, timing, and completion rule while
  assigning the 78 same-run cells to Raw v2. Calibration, fixed-query, and
  stress retain Raw v1.
- Add a distinct telemetry-aware Wire v2 mode. Its successful response carries
  the ordinary measured arm execution and a minimal per-net column partition
  captured from the same authentic contender execution.
- Rerun all 78 exact, held-out, and imported decision cells through that mode.
  One controller invocation emits Same-Run Raw Evidence v2 plus the separately
  checksummed Same-Run Decision Telemetry v1 companion. Raw v2 authenticates
  both its evidence schema and actual Wire-v2 carrier; Raw-v1 validation must
  reject it. Existing Raw or diagnostic files cannot be upgraded after the
  fact.
- Capture all 20 repetitions and both arms. A repetition-zero proxy is
  forbidden. Require complete workload-roster equality, closed per-net and
  aggregate arithmetic, the copied complete board outcome, and exact
  association to each finalized Raw attempt, arm, authority, pair, process,
  dispatch, controller, and cell identity.
- Keep the leaf minimal: nine column counters per net. Do not execute the
  quadratic pool-diversity/overlap diagnostics in the measured path.
- Apply process-lifetime invalidation symmetrically. Any later worker exit,
  protocol, output, teardown, or exact-reap failure invalidates both Raw and
  same-run telemetry from that worker. A failed cell is incomplete, never a
  zero-rejection observation or allocator loss.
- Preserve the complete checksummed Raw-v2 attempt artifact when the companion
  is incomplete and exit nonzero. Publish the companion without replacement
  only after checked Raw output and an atomic, synchronized final-name install;
  a failed output or interrupted write leaves no companion at the final path.
- Require zero exact-validation rejection columns in every arm of every
  repetition for decision eligibility. No pooling may offset a violation. An
  authenticated nonzero count is retained as a guardrail failure rather than
  rejected as corrupt or relabeled incomplete.
- Keep joined Same-Run Raw v2 authoritative for outcomes, timing, memory, and
  paired results in telemetry-aware decision cells. Raw v1 retains that role
  only for legacy Wire-v1 cells. Keep Per-Net Report v1 authoritative only for
  diagnostic diversity and selected-candidate disclosure.

## Consequences

The telemetry scan becomes a bounded part of every decision contender
invocation, so telemetry-aware cells form a separate uniform measurement mode.
Timing remains diagnostic. Wire v1 goldens and existing Raw validators remain
stable and reject Raw v2, while decision closure gains a direct same-execution
authority instead of inferring internal rejection categories from a rerun.

This decision supplies only the exact-rejection guardrail. It does not supply
complete stage timing, utilization, compatible-batch fill, prepared-view cache
behavior, toolchain/hardware provenance, statistical aggregation, or a Phase 4
completion result.
