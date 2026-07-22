# ADR-031: Phase 4 Per-Net Report Publication Join

**Status:** Accepted for the seventeenth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Diagnostic report process execution and independent Raw/report publication joins

## Context

ADR-030 defines a canonical per-net report artifact, but its C++ builder accepts
caller-supplied Raw association checksums and cannot prove that a separate Raw
file exists. Running diagnostics inside the measured Raw controller would also
change the already-reviewed Raw Evidence v1 timing and memory contract. A
publication boundary must authenticate both files without treating the
diagnostic rerun as decision evidence.

## Decision

- Adopt `schemas/benchmark/phase4_per_net_report_publication_join_v1.md`.
- Add a separate `phase4_per_net_report_runner` process. It accepts every
  canonical cell field, clean source commit, and repetition-zero Raw reference
  checksum as bounded, duplicate-rejecting `--name=value` arguments. It never
  invokes a shell, reads the imported fixture only from its fixed Bazel
  runfile, preflights source/config/Raw-envelope shape, requires the canonical
  four-worker/20-repetition configuration and a successful frozen-roster case,
  executes baseline then candidate diagnostics, and writes no stdout until one
  complete canonical report is ready.
- Keep the runner outside Raw v1 measurement. Its report contains configured
  caps but no observed timing, utilization, lifecycle, process, or memory
  measurement, and remains permanently `decision_eligible=false`.
- Add a publication-only Python validator. It first calls the complete bounded
  Raw v1 publication validator with an independently supplied expected commit,
  then strictly parses one bounded canonical report. It independently checks
  every JSON type, key, enum, optional, arithmetic closure, telemetry checksum,
  full EntityRef roster, report artifact checksum, and source envelope.
- Join exact source, wire, complete config, corpus, cell-plan, Raw artifact, and
  Raw source-envelope identity. Join the repetition-zero baseline-first pair
  attempt, paired semantic/artifact, and both arm semantic/artifact checksums.
  Finally compare the complete diagnostic semantics objects with the complete
  Raw arm semantics objects; checksum equality alone is insufficient because
  execution order and preparation-worker count are operational fields omitted
  from the semantic hash.
- Expose no publication relaxations for worker count, repetition count, dirty
  source, or unstamped source. A separately compiled test-only runner may
  synthesize a clean source envelope for cross-language tests; the production
  binary does not recognize that option.
- Keep Raw Evidence v1 and Wire v1 byte-for-byte unchanged.

## Consequences

Per-net diagnostic artifacts can now be generated reproducibly and admitted
for publication only beside the exact complete Raw cell they describe. A
foreign cell, later repetition, generation-changed roster, rehashed operational
identity, malformed optional shape, or internally consistent foreign commit
fails the independent join.

This slice does not make diagnostic time or memory decision evidence and does
not add stage/utilization telemetry, fixed-query or stress artifacts,
statistics, family aggregation, legalization, or a Phase 4 success decision.
