# ADR-058: Phase 4 Confirmatory H=4096 Ordinary Operational Measurement

**Status:** Accepted for the sixth post-Protocol-v2 confirmatory implementation slice
**Date:** July 26, 2026
**Applies to:** Development-only H=4096 calibration diagnostics before
campaign, heldout, or decision acquisition

## Context

ADR-053 opens ordinary H=4096 Raw Evidence schema 1 over Raw Wire 1 for
calibration cell `(10200,8)`, and ADR-054 opens its independent per-net report
join. Neither authority opens operational replay or publication.

Confirmatory Decision Protocol v2, checksum `11520586171987743043`, reserves
`phase4_confirmatory_operational_measurement_publication_v2`. The existing
ordinary confirmatory operational authority is a Corpus-v2/H=2250
Protocol-v1 path fixed to `(10200,4)`. Its overlapping Corpus, case, carrier,
and payload shapes cannot select H=4096, and widening it would make the two
price configurations substitutable.

The reviewed H=4096 Raw and report predate this implementation. Their source
identity cannot be relabeled or joined to operational code committed later.
A mutually joinable calibration bundle therefore requires reacquisition after
this authority is adversarially reviewed and committed.

## Decision

- Add separately named H=4096 production and test operational workers,
  captures, and publication validators. The compiled authority, never a caller
  option, selects configuration authority
  `phase4_confirmatory_corpus_v2_h4096` out of band and positively
  authenticates Confirmatory Decision Protocol v2 checksum
  `11520586171987743043` and canonical algorithm-budget roster v3 checksum
  `18429170436700418962`.
- Fix the authority to explicit Representative Corpus 2, calibration cell
  `(case_id=10200, requested_pool_size=8)`, Raw Evidence schema 1 over Raw
  Wire 1, four preparation workers, 20 Raw repetitions, equal-arm
  `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`, canonical algorithm-budget checksum
  `8230401457668518004`, and paired semantic
  `budget_checksum=12108149041077564710`. The two checksums authenticate
  distinct preimages and cannot be derived from, aliased to, or substituted
  for one another.
- Preserve Worker Output v1, Operational Profile v1, Replay Authority v1,
  Operational Measurement Capture v1, and final operational-publication
  payload schema 1 without changing their shapes or existing worker, profile,
  replay, or capture checksum domains. The Protocol-v2 `v2` name selects only
  the H=4096 authority binding and the final publication domains
  `APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2`
  and
  `APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2`.
  Protocol-v2 JSON remains byte-unchanged.
- Reuse the reviewed four-exec order: measured baseline, measured reusable
  candidate, unmeasured baseline replay authority, and unmeasured candidate
  replay authority. Retain exact-child `wait4`, pidfd and descendant
  containment, resource limits, affinity and cgroup checks, parent-death
  coupling, default `SIGCHLD` handling, bounded diagnostics, controller
  deadlines, and complete fork-to-reap accounting.
- Pin capture and publication to
  `phase4_confirmatory_h4096_operational_replay_worker` in the compiled public
  launcher's adjacent target-specific standalone runfiles tree. The worker is
  inode-pinned for execution and hashed by capture; publication independently
  resolves and hashes that same bundled production worker. Ambient runfiles,
  caller paths or working directories, enclosing Bazel test trees, worker
  arguments, case IDs, and opaque checksums cannot substitute an executable or
  authority.
- Retain complete standalone-runfiles traversal, hermetic Python selection,
  the one-use inherited launcher handshake, parent-bound delegation, isolated
  bytecode cleanup, and launcher cancellation containment. Existing H=2250,
  H=4096 same-run, legacy, and generic operational entry points remain unable
  to select this worker or authority.
- Require the publication validator to fail closed in authority and input
  order:
  1. authenticate Protocol v2, roster v3, and the exact reserved ordinary
     operational substitution;
  2. read and completely authenticate bounded regular H=4096 ordinary
     Raw-schema-1/Wire-1 `(10200,8)` from the independently supplied clean
     source commit;
  3. only then resolve and hash the pinned H=4096 worker and open and completely
     validate the bounded regular four-process capture; and
  4. only after rebuilding the complete Raw/capture source, configuration,
     process, replay, and semantic join may it open a bounded regular
     publication-validation input or create and atomically install output.
- Accept no Same-Run Decision Telemetry sidecar. Raw remains the only
  allocation-outcome and paired-timing authority, while the operational
  publication remains diagnostic replay evidence.
- Keep production source identity fail closed. Raw, capture, bundled worker,
  publication, and the independently supplied expected commit must identify
  one clean stamped 40-character lowercase commit. The separately compiled
  test worker and test publication path retain only their actual
  nonpublishable build envelope and cannot synthesize or adopt a clean
  production identity.
- After this implementation passes adversarial review and is committed,
  reacquire ordinary H=4096 Raw `(10200,8)`, its sibling per-net report, and
  the operational capture/publication from that same clean commit. Earlier
  artifacts cannot be copied, relabeled, rechecksummed, or accepted through a
  cross-commit exception.
- Keep every other development cell, standalone replay, fixed-query, stress,
  aggregation, matrix, decision, heldout, and imported paths closed pending
  separately reviewed authorities.

## Consequences

One H=4096 calibration cell can acquire a four-process operational diagnostic
under the reviewed containment and frozen payload contracts without widening
the H=2250 or same-run authorities. Exact-child CPU, peak-host, and controller
fork-to-reap wall measurements remain descriptive diagnostics.

The publication retains `cell_role="calibration"`,
`standalone_decision_eligible=false`,
`statistical_timing_eligible=false`, and `coverage_complete=false`. It does not
establish resource feasibility, route-pool completeness, fixed-pool
optimality, final legality, complete the confirmatory matrix, decide Phase 4,
complete Phase 4, or complete M1.

As throughout the Phase 4 evidence formats, stable hashes provide corruption
and association checks rather than cryptographic attestation. They do not
defend against an actor who can arbitrarily rewrite an artifact and recompute
all public fields; that threat model requires an external signature or
platform attestation authority.

## Rejected alternatives

- **Widen the H=2250 ordinary operational entry points.** Corpus, case, wire,
  source, or matching payload shape cannot choose between price authorities.
- **Route ordinary evidence through the H=4096 same-run authority.** Raw Wire 1
  has no telemetry companion, and the two Protocol-v2 substitutions bind
  different cells, carriers, and evidence roles.
- **Mint v2 worker, capture, profile, replay, or publication payload schemas.**
  Protocol v2 reserves an authority substitution; the frozen v1 payloads carry
  the required operational semantics.
- **Join earlier H=4096 artifacts across commits.** The source-envelope
  contract requires one clean implementation and acquisition commit for a
  mutually joinable development bundle.
- **Open another role in this slice.** Each remaining authority requires its
  own narrow adversarial review.
