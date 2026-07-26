# ADR-056: Phase 4 Confirmatory H=4096 Same-Run Operational Measurement

**Status:** Accepted for the fourth post-Protocol-v2 confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Development-only H=4096 exact-cell operational diagnostics
before Oracle or heldout acquisition

## Context

ADR-053 opened exact H=4096 Same-Run Raw/Wire 2 execution and its mandatory
Decision Telemetry companion for `(10100,4)`. ADR-055 then opened an
independent per-net diagnostic join for the same cell. Neither authority opens
operational replay or publication.

Confirmatory Decision Protocol v2 reserves
`phase4_confirmatory_same_run_operational_measurement_publication_v2`.
The existing confirmatory same-run operational authority remains a
Corpus-v2/H=2250 Protocol-v1 path. Its overlapping Corpus, case, carrier, and
payload shapes cannot select H=4096, and widening it would make the two price
configurations substitutable.

The reviewed exact Raw, sidecar, and report also predate this implementation.
Their source identity cannot be relabeled or joined to operational code
committed later. A mutually joinable exact development bundle therefore
requires reacquisition after this authority is reviewed and committed.

## Decision

- Add separately named H=4096 production and test operational workers,
  captures, and publication validators. The compiled authority, never a caller
  option, selects `phase4_confirmatory_corpus_v2_h4096` out of band and
  positively authenticates Confirmatory Decision Protocol v2 and canonical
  algorithm-budget roster v3.
- Fix the authority to explicit Corpus 2, exact cell `(10100,4)`, Raw Evidence
  schema 2 over Raw Wire 2, Same-Run Decision Telemetry schema 1 over
  Telemetry Wire 2, four workers, 20 repetitions, equal-arm
  `present_step_per_overuse_unit=1`,
  `history_step_per_overuse_unit=4096`, and canonical algorithm-budget checksum
  `8829615204625848656`. Every other configuration, case, pool, carrier, or
  role must fail before fixture access, case construction, preparer creation,
  warmup, or replay.
- Preserve Worker Output v1, Operational Measurement Capture v1, Operational
  Profile v1, Replay Authority v1, the final publication payload schema 1, and
  their existing structural contracts. The Protocol-v2 `v2` name selects only
  the H=4096 authority binding and the final publication checksum domains
  `APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2`
  and
  `APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2`.
  Protocol-v2 JSON remains byte-unchanged.
- Reuse the reviewed four-exec containment sequence: measured baseline,
  measured candidate, unmeasured baseline replay authority, and unmeasured
  candidate replay authority. Retain exact-child `wait4`, pidfd and descendant
  containment, limits, affinity and cgroup checks, parent-death coupling,
  default `SIGCHLD` handling, complete standalone-runfiles traversal,
  hermetic Python selection, one-use launcher handshake, isolated bytecode
  cleanup, and worker-inode pinning and digest verification.
- Pin the H=4096 capture and publication to the separately compiled H=4096
  worker in the public launcher's adjacent target-specific standalone runfiles
  tree. Existing H=2250, ordinary H=4096, legacy, and generic operational
  entry points remain unable to select this worker or authority.
- Require the publication validator to fail closed in authority order:
  1. read and completely authenticate bounded regular H=4096 Same-Run Raw-v2;
  2. only then read and completely join its bounded regular H=4096 telemetry
     companion;
  3. only then resolve and hash the pinned H=4096 worker and open and validate
     the bounded regular four-process capture; and
  4. only after the complete Raw/sidecar/capture source, configuration, and
     semantic join may it open a bounded regular publication validation input
     or install output.
- Preserve Raw as the only allocation-outcome and paired-timing authority,
  same-run telemetry as the only exact-rejection guardrail authority, and the
  operational publication as diagnostic replay evidence. A structurally valid
  `exact_rejection_guardrail_passed=false` remains publishable authentic
  negative evidence; this join must not erase, repair, or pre-judge it.
- After this implementation passes adversarial review and is committed,
  reacquire exact Raw `(10100,4)`, its telemetry sidecar, its sibling per-net
  report, and the operational capture/publication from that same clean commit.
  Earlier artifacts cannot be copied, relabeled, rechecksummed, or accepted
  through a cross-commit exception.
- Keep ordinary H=4096 operational measurement, every other development cell,
  standalone replay, snapshot, exact-small Oracle, fixed-query, stress,
  aggregation, matrix, decision, heldout, and imported paths closed pending
  separately reviewed authorities.

## Consequences

One H=4096 exact development cell can acquire a four-process operational
diagnostic under the same containment and payload contracts as the reviewed
H=2250 path without widening that authority. Exact-child CPU/RSS and
controller fork-to-reap wall measurements remain descriptive diagnostics.

The publication remains `standalone_decision_eligible=false`,
`statistical_timing_eligible=false`, and `coverage_complete=false`. It neither
proves fixed-pool optimality nor establishes resource feasibility, opens the
Oracle, authorizes campaign acquisition, decides the matrix, or completes
Phase 4.

As throughout the Phase 4 evidence formats, stable hashes provide corruption
and association checks rather than cryptographic attestation. They do not
defend against an actor who can arbitrarily rewrite an artifact and recompute
all public fields; that threat model requires an external signature or
platform attestation authority.

## Rejected alternatives

- **Widen the H=2250 same-run operational entry points.** Corpus, case, wire,
  source, or matching payload shape cannot choose between price authorities.
- **Mint v2 worker, capture, profile, replay, or publication payload schemas.**
  Protocol v2 reserves an authority substitution; the frozen v1 payloads
  already carry the required operational semantics.
- **Publish before the telemetry join or reject a false guardrail as
  malformed.** This would bypass or pre-judge the separately authoritative
  exact-rejection evidence.
- **Join earlier exact artifacts across commits.** The source-envelope contract
  requires one clean implementation and acquisition commit for a mutually
  joinable development bundle.
- **Open the Oracle or another role in this slice.** Each remaining authority
  requires its own narrow adversarial review.
