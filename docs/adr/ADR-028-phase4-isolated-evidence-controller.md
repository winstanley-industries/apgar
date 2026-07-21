# ADR-028: Phase 4 Isolated Evidence Controller

**Status:** Accepted for the fourteenth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Raw paired-trial process execution and evidence serialization

## Context

Paired Trial v1 defines successful semantic records and external authority but
does not itself create trustworthy operating-system measurements. A harness
that forks once per repetition would discard the persistent candidate-preparer
contract. A long-lived worker cannot truthfully attribute `wait4` peak RSS to
individual repetitions. `RLIMIT_AS` also limits virtual address space rather
than resident high-water memory. Finally, move-only typed failures containing
owned candidate stores cannot be copied through a pipe. The process boundary
needs a bounded, checksum-covered reconciliation fingerprint without
pretending that it is the heavyweight failure replay itself.

## Decision

- Adopt `schemas/benchmark/phase4_raw_evidence_v1.md`.
- Use one separately exec'd long-lived worker per contender and one exact
  parent controller per case/pool/worker cell. Warm both contenders once,
  retain the candidate preparer, and serialize every measured AB/BA arm.
- Apply and verify an exact pre-exec `RLIMIT_AS` safety cap. Observe one
  conservative lifetime resident peak per worker with parent `wait4`, attach
  it to every arm from that worker, and invalidate those arms if the worker
  later exits abnormally.
- Use an absolute parent `CLOCK_MONOTONIC` watchdog starting before launch or
  command dispatch. Bound diagnostic-channel drains and recheck the absolute
  deadline after every potentially productive read. Kill the complete worker
  process group on timeout or fatal peer failure, signal the exact leader
  through its retained Linux pidfd, and retain timeout, signal, exit, protocol,
  and launch states as incomplete attempts.
- Require each worker to acknowledge the authenticated stop command before a
  clean exact-PID reap. The acknowledgement is provisional until the response
  channel reaches EOF without another byte. Observe exit through
  `waitid(P_PIDFD, WNOWAIT | WNOHANG)` while the child remains unreaped, settle
  terminal response state, and only then perform exact nonblocking `wait4`.
  Post-kill cleanup remains deadline-bounded; stop, acknowledgement, output,
  response-channel, reap-authority, or teardown-timeout drift invalidates every
  earlier success from that worker.
- Reject a controller whose inherited `SIGCHLD` policy is anything other than
  the default handler without `SA_NOCLDWAIT`, and require pidfd support before
  launching workers.
- Before `exec`, close every inherited descriptor except the fixed request,
  response, merged-output, and close-on-exec launch-error channels. Use Linux
  `close_range` with a deterministic `RLIMIT_NOFILE`-bounded fallback.
- Treat a fatal worker protocol or association failure as process-lifetime
  drift and immediately invalidate every earlier apparent success from that
  worker, even if it subsequently exits cleanly.
- Use a bounded, checksummed, fixed-width little-endian wire protocol. The
  child reports algorithm semantics or a durable failure DTO only; the parent
  owns timing, process, limit, RSS, exit, association, finalization, and pairing
  claims.
- Send Ready only after a successful warm-up and include its full built-case
  identity. Require both contenders to agree with the active descriptor and
  with each other, then bind every later identity-bearing response to that
  authenticated worker handshake. Before Ready, associate every typed setup
  failure with the active case and descriptor, and with the peer's complete
  built-case identity when that peer is already Ready; summary-only setup
  failures carry no case identity.
- Reconcile an authoritative CandidateStore in the child before serializing a
  failure. Preserve the stable error type, bounds, case/epoch identity,
  publication state, attempted counters, roster counts, and reconciliation
  checksum.
- Emit canonical one-line JSON containing source identity, configuration,
  environment, total attempt state, successful Paired Trial records, and all
  checksum layers. Bind the source fields to the cell artifact, require an
  independently supplied expected commit, and bind every successful semantic
  record to `phase4_representative_manifest_v1.json`. Validate exact canonical
  bytes with an independent strict parser before evidence can enter statistics.
  The manifest freezes successful-case requirements for nets, compiled nodes,
  logical host bytes, active regions, and Board entities; the parser checks all
  five against the serialized corpus limits independently of checksums.
- Treat durable payload kind and summary code as a bidirectional mapping.
  Summary-only cannot stand in for corpus, sequential, preparation, or session
  state. A corpus work-bound payload names a nonzero first-unpreparable net and
  proves `required > configured` for every declared limiting resource.
- Require observed failure columns to equal observed route queries and cap work
  at the canonical per-query limit; independently apply the active cell's
  preparation and session-epoch column caps. Reserve parent-only association,
  external-budget, and pair-mismatch codes from summary-only worker failures.
- Require normalized arm semantics and the derived paired comparison to remain
  identical across all repetitions before raw evidence can be published.
- Fix canonical per-query A-star work at 1,000,000,000 units, use 20 paired
  repetitions with ten observations of each order, and derive one cell root
  seed independently of operational identities.

## Consequences

The raw artifact can prove process isolation, persistent reuse, complete AB/BA
attempt accounting, and parent-observed resource bounds without pretending to
have per-repetition RSS. Loader, protocol, timeout, and typed algorithm failures
remain distinguishable and cannot be counted as board-level losses.

The durable failure DTO is a bounded reconciled diagnostic fingerprint. It is
not a substitute for the versioned replay artifact required when a new
invariant class is investigated, and every typed failure disqualifies its cell
from publication.

The runner is Linux-specific because its authority names process groups,
pidfds, `RLIMIT_AS`, `CLOCK_MONOTONIC`, and `wait4`. A future platform requires
a new authority kind and schema rather than silently weakening these claims.

This slice does not yet publish the canonical matrix, uncertainty-qualified
family outcome, detailed candidate-yield/cache/utilization telemetry, or a
Phase 4 completion decision.
