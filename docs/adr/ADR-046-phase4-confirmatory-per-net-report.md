# ADR-046: Phase 4 Confirmatory Per-Net Report Join

**Status:** Accepted for the second post-freeze confirmatory implementation slice
**Date:** July 23, 2026
**Applies to:** Corpus v2 ordinary calibration diagnostics before heldout acquisition

## Context

ADR-045 establishes explicit Corpus v2 Raw authorities while leaving every
downstream diagnostic publisher Corpus v1-only. The first downstream slice
must prove a structural per-net join against a real 20-repetition calibration
cell without widening V1 entry points or opening the heldout surface.

Per-Net Report Artifact v1 is payload-compatible with either ordinary Wire 1
or same-run Wire 2, but its C++ builder, frozen roster, runner, and Python join
previously selected Corpus v1 implicitly. Corpus v2 also changed the
workload-roster row and root checksum domains.

## Decision

- Keep Per-Net Report Artifact v1, its checksum domains, telemetry payload, and
  permanent `decision_eligible=false` value unchanged.
- Add separately named Corpus v2 builder, validator, cell-plan, roster, and
  roster-manifest entry points. Legacy entry points remain strictly V1.
- Reconstruct the V2 roster-manifest root checksum from its exact compact
  canonical JSON preimage with `manifest_checksum` omitted; do not reuse the
  V1 field-by-field root preimage. Rebuild and check every compiled V2 row,
  exclusion, and complete EntityRef roster.
- Add separate production and test-only confirmatory report runners. Both
  require explicit Corpus 2 and ordinary Wire 1; this slice accepts exactly
  `(10200,4)` before any diagnostic execution. Only the test binary can
  synthesize a clean stamped source envelope.
- Add a separate confirmatory publication validator. It fully authenticates
  the exact ordinary Raw cell before opening the bounded regular report file,
  then performs the complete structural Raw/report join under Corpus v2
  identity and roster domains.
- Exercise a real 20-repetition Raw acquisition, deterministic C++ report
  generation, complete Python publication join, V1/V2 cross-rejection,
  fail-closed runner scope, production-source enforcement, and nonblocking
  FIFO rejection.

## Consequences

The first ordinary Corpus v2 calibration cell now has a canonical,
independently joined per-net diagnostic companion. The diagnostic rerun owns
no timing, resource, or allocation decision evidence.

No heldout, fixed-query, stress, imported, or same-run report authority is
opened. This slice does not begin the confirmatory campaign and does not
complete Phase 4.
