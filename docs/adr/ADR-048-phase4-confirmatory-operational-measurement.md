# ADR-048: Phase 4 Confirmatory Operational Measurement

**Status:** Accepted for the fourth post-freeze confirmatory implementation slice
**Date:** July 23, 2026
**Applies to:** Corpus v2 ordinary operational diagnostics before heldout acquisition

## Context

ADR-046 opens the ordinary Corpus v2 `(10200,4)` Raw/Wire-1 diagnostic
surface, but the legacy operational worker, capture validator, cell-role
lookup, and publication CLI remain Corpus v1 authorities. Reusing those entry
points would either reject authentic Corpus v2 semantics or infer a corpus from
an overlapping artifact shape. Operational measurements are diagnostic replays
and cannot replace Raw outcome or paired-timing authority.

## Decision

- Add separately named production and test-only confirmatory operational
  workers. Both require explicit Corpus 2, Wire 1, and exactly `(10200,4)`
  before fixture access, representative-case construction, preparer creation,
  warmup, or replay. Only the test target recognizes the unstamped-source
  escape.
- Add explicit Corpus v2 worker serializers that retain Operational Profile
  v1, Replay Authority v1, and Worker Output v1 wire/checksum formats while
  validating their semantics against Representative Corpus v2. Legacy
  serializers remain Corpus v1-only.
- Reuse the reviewed four-exec containment controller: measured baseline,
  measured candidate, unmeasured baseline replay authority, then unmeasured
  candidate replay authority. The confirmatory entry point fixes the worker
  target, exact digest from its own Bazel runfiles tree, and exact cell;
  production exposes neither worker substitution nor a testing escape. A
  caller-controlled working directory or ambient runfiles environment cannot
  select a worker: compiled public launchers clear runfiles variables before
  entering Python, and inner Python authorities require their launcher's
  one-use inherited file-descriptor handshake. A nested Bazel invocation uses
  its authenticated enclosing runfiles tree instead of an undeclared or stale
  standalone tree; incomplete traversal of the entire selected root, including
  external repository subtrees, fails authentication, and an invalid enclosing
  tree cannot fall back to a standalone tree. The delegated authority is
  parent-death-coupled to the exact public launcher so launcher termination
  cannot leave publication work running. The launcher normalizes inherited
  ignored `SIGCHLD` state before delegation so successful authority output
  cannot be followed by an `ECHILD` reaping failure, and removes its isolated
  bytecode-cache path before delegation so cancellation cannot leak it.
  Publication independently rehashes its bundled production worker. The
  separately compiled test worker never synthesizes a clean source envelope or
  adopts the caller's commit association as its build identity.
  Embedded source state alone determines whether that test build is
  publishable, so a mismatched caller commit cannot downgrade it. An unstamped
  build with no embedded commit uses an all-zero 40-character unavailable
  sentinel. A test worker built from publishable clean stamped source fails
  before replay, and test-only publication explicitly permits only the
  non-publishable envelope.
- Add the separately named and domain-separated
  `phase4_confirmatory_operational_measurement_publication_v1` validator. It
  completely validates bounded regular Raw first, requires ordinary
  Raw/Wire 1 and `(10200,4)`, then opens and validates the bounded regular
  capture with explicit Corpus v2 authority. The complete Raw/capture
  configuration, source, and semantic join is rebuilt before it may open a
  publication validation input or install output.
- Preserve Operational Measurement Capture v1 and worker payload formats, but
  do not mint a legacy Operational Projection v1 identity from Corpus v2 Raw.
  The confirmatory publication omits that legacy binding, explicitly binds
  Corpus 2/Wire 1, and uses confirmatory artifact/source checksum domains. The
  protocol role serialized for `(10200,4)` is `calibration`.
- Keep all legacy worker, capture, validator, projection, and publication entry
  points Corpus v1-only.

## Consequences

One ordinary development cell now exercises the confirmatory operational
publication chain without observing a heldout allocation outcome. Exact-child
`wait4` CPU/RSS and controller fork-to-reap wall measurements remain
diagnostic. Raw remains the only outcome and paired-timing authority, and the
publication remains non-standalone, non-statistical, and incomplete coverage.

Same-run operational measurement, other development cells, heldout and
imported cells, fixed-query and stress evidence, complete aggregation, campaign
execution, and the Phase 4 decision remain closed.

As throughout the Phase 4 evidence formats, stable hashes provide corruption
and association checks rather than cryptographic attestation. They do not
defend against an actor who can arbitrarily rewrite an artifact and recompute
all public fields; a future hostile-producer threat model would require an
external signature or platform attestation authority.
