# ADR-053: Phase 4 Confirmatory H=4096 Raw Authority

**Status:** Accepted for the first post-Protocol-v2 confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Development-only H=4096 Raw execution and validation before any
downstream publication or heldout acquisition

## Context

ADR-051 froze canonical algorithm-budget roster v3 as an inactive
`phase4_confirmatory_corpus_v2_h4096` configuration preimage. ADR-052 then
froze Confirmatory Decision Protocol v2, including the only two development
cells that a later separately reviewed execution slice may open. Neither
authority implemented an execution path, authorized fixture access, or created
an H=4096 allocation outcome.

The existing Corpus-v2 runner, paired-trial entry points, workers, serializers,
and validators remain H=2250 Protocol-v1 authorities. Corpus version, case
identity, wire version, source commit, or an opaque budget checksum cannot
select H=4096. Widening those entry points would make H=2250 and H=4096
evidence substitutable.

The first executable H=4096 slice therefore needs a separately named,
configuration-fixed path through the complete Raw process and validation
boundary while every downstream and heldout capability remains closed.

## Decision

- Add a separately named production
  `phase4_confirmatory_h4096_evidence_runner`. Its compiled target, rather than
  a caller option, selects configuration authority
  `phase4_confirmatory_corpus_v2_h4096`. It still requires explicit Corpus 2
  and accepts exactly:
  - exact cell `(10100,4)` through atomic same-run Raw Wire 2 together with its
    Telemetry Wire 2 companion; and
  - calibration cell `(10200,8)` through ordinary Raw Wire 1.
- Add separately named
  `phase4_confirmatory_h4096_raw_evidence_validator` and
  `phase4_confirmatory_h4096_same_run_decision_telemetry_validator` entry
  points. The ordinary validator accepts only `(10200,8)`. The same-run
  validator first completely authenticates Raw for `(10100,4)`, then opens,
  authenticates, and completely joins the telemetry companion. Both validators
  bind Confirmatory Decision Protocol v2, canonical algorithm-budget roster
  v3, Representative Corpus v2, its unchanged case and workload authorities,
  and an independently supplied expected clean source commit.
- Retain the existing payload and carrier schemas without change. Ordinary
  evidence remains Raw Evidence schema 1 over Raw Wire 1. Same-run evidence
  remains Raw Evidence schema 2 over Raw Wire 2, and its companion remains
  Same-Run Decision Telemetry schema 1 over Telemetry Wire 2. The reserved
  `phase4_confirmatory_raw_evidence_v2`,
  `phase4_confirmatory_same_run_raw_evidence_v2`, and
  `phase4_confirmatory_same_run_decision_telemetry_v2` names are artifact
  authorities, not payload-schema selectors.
- The frozen Raw schemas have no separately authenticated authority for an
  H=4096 cell containing only failed arm attempts. H=4096 Raw authentication
  therefore requires at least one completely authenticated successful arm
  witness. An all-failure capture is deliberately non-authenticatable rather
  than being serialized as a weaker authority. This does not relax complete
  ordinary publication or the exact Raw-plus-telemetry join.
- Require the public controller and both hidden worker modes to independently
  enforce the literal configuration, case, pool, and protocol-assigned carrier
  boundary before fixture access. The runner invokes the complete pure
  carrier-specific H=4096 preflight for the controller and each hidden worker
  before source or fixture checks. The controller library repeats that
  boundary and positively validates equal-arm `present=1,history=4096` before
  worker launch or representative-case construction. Each worker repeats the
  configuration and canonical-spec checks before preparer construction,
  warmup, allocator access, or request decoding. Caller-supplied descriptors,
  case IDs, checksums, or worker arguments cannot inherit or manufacture
  controller authority.
- Preserve all existing Corpus-v2 H=2250 entry points unchanged. They continue
  to positively require equal-arm `present=1,history=2250` and reject H=4096
  before case, fixture, preparer, or worker access. The new H=4096 entry points
  positively require equal-arm `present=1,history=4096` and reject H=2250.
- Add a test-only
  `phase4_confirmatory_h4096_evidence_test_runner` that is permanently
  nonpublishable and fixtureless. It declares no board fixture, cannot enter
  either authorized execution path, and exists only to exercise parsing and
  pre-access firewall rejection. It must reject before fixture resolution,
  case construction, preparer creation, worker launch, or artifact
  serialization, even when built from a clean stamped tree. No testing option,
  caller commit, or source-state combination may make its output publishable.
  A separate production-shaped test-only target may compile-force only its
  embedded source gate to an unpublishable state so clean-build hidden-worker
  firewall probes are deterministic. That target declares no fixture data,
  cannot weaken the production target's actual embedded source fields, and
  cannot emit evidence.
- Production Raw evidence requires a clean stamped source, and the explicit
  runtime commit association must equal the embedded commit. The eventual
  development evidence must be acquired from one clean commit containing the
  exact Protocol v2 and roster v3 authorities together with the reviewed
  H=4096 runner and validators. The controller and every worker must execute
  that commit. Self-workers therefore re-execute the forked controller's
  already-running executable inode through `/proc/self/exe`; resolving that
  link to a mutable output pathname is forbidden because a concurrent rebuild
  could silently substitute a different clean commit. An H=2250 artifact
  cannot be promoted, relabeled, or rechecksummed into an H=4096 authority.
- Keep every H=4096 per-net report, operational measurement, replay, snapshot,
  exact-small Oracle, fixed-query control, stress, aggregation, full-matrix,
  and decision publisher closed. Heldout and imported execution remain closed.
  A later campaign-acquisition authority binding the complete execution and
  publication chain is still required before any heldout observation.

## Consequences

The repository can implement and independently validate only the two
Protocol-v2 development Raw identities without changing a frozen schema or
widening an H=2250 authority. Exact same-run Raw is incomplete without its
same-invocation telemetry companion, while calibration remains an ordinary Raw
identity with no telemetry authority. A capture with no authenticated
successful arm witness produces neither identity.

This slice does not establish that H=4096 restores exact optimality, publish an
Oracle Artifact, open a downstream join, authorize a confirmatory campaign,
decide the matrix, or complete Phase 4. The documentation authority itself
does not execute either cell or inspect an H=4096 allocation outcome.

## Rejected alternatives

- **Select H=4096 with a runner flag or opaque checksum.** Configuration
  authority must be fixed out of band by a separately named entry point.
- **Widen the existing Corpus-v2 runner or validators.** Those entry points
  remain H=2250 Protocol-v1 authorities.
- **Reuse H=2250 calibration evidence.** `(10200,8)` reuses only the historical
  identity; its H=4096 outcome must be newly acquired and validated.
- **Allow a fixture-backed test runner.** A test target capable of allocation
  would create an unreviewed observation path and could be confused with the
  production source authority.
- **Open reports, operational replay, the Oracle, or heldout cells together
  with Raw.** Each later authority requires its own narrow adversarial review.
