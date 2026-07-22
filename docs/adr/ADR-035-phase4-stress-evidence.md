# ADR-035: Phase 4 Stress Evidence

**Status:** Accepted for the twenty-second Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Thousands-net stress ladder, capacity boundaries, and honest measurement scope

## Context

The representative corpus declares 1024-, 2048-, and 4096-net stress tiers,
but default compiled-work bounds allow only the first to become a complete
workload. Treating a capacity quotient as an achieved partial run, or treating
logical compiled bytes as measured RSS, would overstate scalability evidence.
Eagerly building the rejected tiers merely to measure failure would also
discard the builder's bounded uniform-work preflight.

## Decision

- Adopt `schemas/benchmark/phase4_stress_evidence_v1.md`.
- Require full Raw, per-net report, and operational authorities for the
  1024-net pool-4 tier, including its coarse timings and two process-lifetime
  `wait4` peaks.
- Use a source-stamped C++ probe of the real representative builder for the
  2048- and 4096-net tiers. The probe materializes a Board and one authentic
  compiled net, then retains the builder's deterministic work-bound error.
- Report 739 and 369 only as capacity-derived prefixes, never achieved or
  materialized net counts. Report EntityRef(1739,0) and EntityRef(1369,0) as
  the first unpreparable nets.
- Keep both limiting enums as compiled-nodes-only. Keep required compiled host
  bytes labeled logical estimates, not RSS; elapsed and peak memory remain
  unavailable for bounded tiers.
- State that 1024 is the maximum full Raw-success tier and that the declared
  4096-net target is not fully supported.

## Consequences

Phase 4 retains a reproducible negative scalability result without unbounded
materialization or fabricated performance evidence. The stress ladder is
complete as a diagnostic input, but neither the 2048- nor 4096-net tier is a
successful allocation run and the artifact does not complete Phase 4.
