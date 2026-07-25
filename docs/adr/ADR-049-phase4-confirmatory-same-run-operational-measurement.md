# ADR-049: Phase 4 Confirmatory Same-Run Operational Measurement

**Status:** Accepted for the fifth post-freeze confirmatory implementation slice
**Date:** July 23, 2026
**Applies to:** Corpus v2 same-run operational diagnostics before heldout acquisition

## Context

ADR-047 opens Same-Run Raw/Wire 2 and its Decision Telemetry companion for
exact development cell `(10100,4)`. ADR-048 opens the ordinary Corpus v2
operational path, but deliberately accepts only Raw/Wire 1 calibration cell
`(10200,4)`. Reusing that entry point would discard the same-run telemetry
authority or infer an incompatible wire from an overlapping artifact shape.
Operational measurements are diagnostic replays and cannot replace Raw
outcome, Raw paired timing, or telemetry guardrail authority.

## Decision

- Add separately named production and test-only confirmatory same-run
  operational workers. Both require explicit Corpus 2, Raw Wire 2, and exactly
  `(10100,4)` before fixture access, representative-case construction,
  preparer creation, warmup, or replay. Only the test target recognizes the
  unstamped-source escape.
- Reuse the reviewed four-exec containment controller: measured baseline,
  measured candidate, unmeasured baseline replay authority, then unmeasured
  candidate replay authority. The entry point fixes its separately named
  worker target, worker digest from its own Bazel runfiles tree, exact cell,
  and complete budgets. Compiled public launchers clear ambient runfiles and
  Python environment selection before entering isolated Python. Inner Python
  authorities require a one-use inherited file-descriptor handshake, and a
  nested Bazel invocation accepts its enclosing declared runfiles tree only
  after authenticating both the invoked launcher and canonical inner target.
  Traversal of the entire selected runfiles root, including external repository
  subtrees, must complete; invalid enclosing trees fail closed without
  standalone fallback, and parent death terminates the delegated authority
  before it can publish after launcher cancellation. The launcher normalizes
  inherited ignored `SIGCHLD` state before delegation so successful authority
  output cannot be followed by an `ECHILD` reaping failure, and removes its
  isolated bytecode-cache path before delegation so cancellation cannot leak
  it.
  Test-worker publishability is derived only from embedded source state; a
  caller commit mismatch cannot turn a clean stamped test build into an
  accepted non-publishable build.
- Add the separately named and domain-separated
  `phase4_confirmatory_same_run_operational_measurement_publication_v1`
  validator. It completely validates bounded regular Same-Run Raw first,
  requires Raw/Wire 2 and `(10100,4)`, then completely validates and joins the
  bounded regular Telemetry/Wire 2 companion. It may resolve and hash its
  bundled worker and open the bounded regular capture only after that join.
  The complete Raw/telemetry/capture configuration, source, and semantic join
  is rebuilt before it may open a publication validation input or install
  output.
- Preserve Operational Measurement Capture v1 and worker payload formats, but
  do not mint a legacy Operational Projection v1 identity from Corpus v2 Raw.
  The publication directly binds Corpus 2, Raw Wire 2, Telemetry Wire 2, the
  complete three input checksums, and distinct artifact/source checksum
  domains. The protocol role for `(10100,4)` is `exact`.
- Preserve a structurally valid
  `exact_rejection_guardrail_passed=false` value as authentic negative
  evidence. This slice publishes rather than pre-judges it; only complete
  aggregation may turn that guardrail into a decision failure.
- Keep ordinary Corpus v2 and all legacy worker, capture, validator,
  projection, and publication entry points closed to Same-Run Raw/Wire 2.

## Consequences

One exact development cell now exercises the complete confirmatory same-run
operational publication chain without observing a heldout allocation outcome.
Exact-child `wait4` CPU/RSS and controller fork-to-reap wall measurements
remain diagnostic. Raw remains the only outcome and paired-timing authority,
the telemetry companion remains exact-rejection authority, and the publication
remains non-standalone, non-statistical, and incomplete coverage.

Other development cells, heldout and imported cells, exact-oracle,
fixed-query, and stress evidence, complete aggregation, campaign execution,
and the Phase 4 decision remain closed.

As throughout the Phase 4 evidence formats, stable hashes provide corruption
and association checks rather than cryptographic attestation. They do not
defend against an actor who can arbitrarily rewrite an artifact and recompute
all public fields; a future hostile-producer threat model would require an
external signature or platform attestation authority.
