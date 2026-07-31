# ADR-063: Phase 4 Session-v5 H=4096 Raw Authority

**Status:** Accepted as an inactive successor contract; execution and
acquisition remain closed
**Date:** July 30, 2026
**Applies to:** Development-only Session-v5/H=4096 Raw capability and
validation before the complete consuming-publication chain exists

## Context

ADR-061 activated canonical algorithm-budget roster v4 as an acquisition-free
Session-v5/H=4096 configuration authority. ADR-062 then activated
Confirmatory Decision Protocol v3 as an acquisition-free protocol and artifact
namespace authority. Protocol v3 reserves only exact cell `(10100,4)` through
the same-run carrier and calibration cell `(10200,8)` through the ordinary
carrier as the initial Session-v5 development scope.

The existing H=4096 runner, workers, serializers, and validators remain
Protocol-v2/roster-v3/Session-v4 authorities. Their development observations
are authentic historical evidence, but Session-v5 activation now rejects
their execution surfaces before fixture, preparer, worker, case, or allocator
access. Reusing those entry points or relabeling their artifacts would make
the two candidate-allocation session contracts substitutable.

Roster v4 additionally requires separately reviewed development acquisition
and consuming-publication authorities to bind the complete successor chain
from one clean stamped source commit before either designated development cell
may run. Raw validation alone is not that complete chain. This contract can
therefore freeze the Raw successor boundary and permit acquisition-free
implementation work, but it cannot activate a fixture-bearing production
target or create an allocation outcome.

## Decision

- Preserve every Protocol-v2/roster-v3/Session-v4 runner, worker, validator,
  payload, artifact, and source association unchanged. The existing
  `phase4_confirmatory_h4096_evidence_runner` and
  `Phase4TrialExecutionAuthority::kCorpusV2H4096` remain historical
  Session-v4 identities and must never select Session v5.
- Reserve a fresh out-of-band execution identity
  `Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5` and separately
  named future production runner
  `phase4_confirmatory_h4096_session_v5_evidence_runner`. A compiled target,
  never a caller flag, case ID, carrier, source commit, or opaque checksum,
  must select configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`.
- Permit the new execution identity to be added only when it is consumed by a
  complete fail-closed Session-v5 preflight. Until a separately reviewed
  activation slice proves the complete consuming-publication chain is present,
  the fixture-bearing production target must be absent or must fail through an
  immutable activation gate before fixture resolution, case construction,
  preparer creation, worker launch, request decoding, allocation, or artifact
  serialization.
- Fix the only future Raw execution identities to:
  - exact `(10100,4)` through Raw Evidence schema 2 over Raw Wire 2, together
    atomically with Same-Run Decision Telemetry schema 1 over Telemetry Wire 2,
    under `phase4_confirmatory_same_run_raw_evidence_v3` and
    `phase4_confirmatory_same_run_decision_telemetry_v3`; and
  - calibration `(10200,8)` through Raw Evidence schema 1 over Raw Wire 1,
    under `phase4_confirmatory_raw_evidence_v3`, with no telemetry authority.
- Bind Confirmatory Decision Protocol v3 checksum
  `4963299999381388941`, canonical algorithm-budget roster v4 checksum
  `12316700735749461907`, Representative Corpus v2 checksum
  `4182833841936446798`, Representative Manifest v2 checksum
  `9613362670139358355`, Workload-Net Roster Manifest v2 checksum
  `14986327048461036142`, and the complete Protocol-v2/roster-v3 ancestry.
  Positively require canonical per-cell budget checksum
  `13645569624513409309` for exact `(10100,4)` and
  `7657176792159702821` for calibration `(10200,8)`.
- Independently reconstruct the Raw
  `APGAR-PHASE4-PAIRED-BUDGET-V1` semantic budget from the complete canonical
  Session-v5 cell preimage. Require paired semantic budget checksum
  `12493092620111240227` for exact `(10100,4)` and
  `13340538727848385478` for calibration `(10200,8)`. Neither value may be
  substituted by its canonical algorithm-budget checksum or by predecessor
  Session-v4 paired values `5851813264366095594` and
  `12108149041077564710`. The controller, each worker, finalization,
  serialization, and both validators must enforce both independent budget
  identities.
- Reconstruct the complete Session-v5 canonical trial specification
  independently at the controller and each hidden-worker boundary. Positively
  require fixed Session v5, Targeted Regeneration Plan v3, Targeted
  Regeneration Execution v6, equal-arm `present=1,history=4096`, the literal
  cell/carrier assignment, and every canonical budget field before any source,
  fixture, preparer, warmup, worker, request, or allocator access. A payload
  association, case ID, carrier, source commit, or matching checksum alone
  cannot manufacture this authority.
- Reserve separately named validators
  `phase4_confirmatory_h4096_session_v5_raw_evidence_validator` and
  `phase4_confirmatory_h4096_session_v5_same_run_decision_telemetry_validator`.
  The ordinary validator accepts only `(10200,8)`. The same-run validator must
  completely authenticate exact Raw `(10100,4)` before opening,
  authenticating, and completely joining its same-invocation telemetry
  companion. Both validators require an independently supplied expected clean
  source commit and complete Protocol-v3, roster-v4, corpus, manifest,
  workload-roster, configuration, cell-budget, payload, and source-envelope
  association.
- Retain every existing payload and carrier schema. The `v3` suffix versions
  a configuration-specific artifact authority only; it does not mint Raw
  Evidence schema 3, Raw Wire 3, telemetry schema 3, or Telemetry Wire 3.
- Preserve the rule that authenticated Raw requires at least one completely
  authenticated successful arm witness. An all-failure capture remains
  deliberately non-authenticatable. The exact Raw/telemetry join remains
  atomic, and a telemetry sidecar cannot repair, substitute for, or weaken
  incomplete Raw.
- Require production source identity to be clean and stamped, with explicit
  runtime commit equal to the embedded commit. Validators require the same
  independently supplied expected commit. Future self-workers must re-execute
  the running executable inode through `/proc/self/exe`; resolving that link
  to a mutable output path remains forbidden.
- Any test runner must remain permanently fixtureless, preflight-only, and
  nonpublishable even in a clean stamped build. A production-shaped
  forced-unpublishable test target may exercise source and activation gates,
  but it declares no fixture, cannot allocate or serialize, and cannot emit
  evidence.
- Freeze the inactive implementation error as
  `P4PAIR-H4096-SESSION-V5-ACTIVATION-001`. Every acquisition-free
  implementation slice must add discriminating direct and process tests that:
  - prove valid controller, ordinary-worker, and same-run-worker inputs reach
    exactly that immutable error before fixture, request, preparer, output, or
    serialization sentinels can be touched;
  - prove every configuration, Session/Plan/Execution, case, pool, role,
    carrier, payload/wire, canonical algorithm-budget, paired semantic-budget,
    and source-association perturbation fails before the activation barrier;
  - prove no argument, environment variable, source state, matching checksum,
    testing option, direct worker invocation, or serializer call can satisfy
    or bypass the barrier;
  - prove historical and successor entry points and artifacts cross-reject in
    both directions while Protocol v2, roster v3, their goldens, and existing
    Session-v4 acceptance remain unchanged; and
  - use only pure synthetic artifacts to prove bounded-regular-file input,
    Raw-first same-run validation, atomic companion association, corruption
    rejection, expected-commit enforcement, and rejection of all-failure Raw.
    These tests must not construct a representative case, open a fixture, or
    execute an allocator.
- Before either cell may run, separately reviewed Session-v5 successors for
  both per-net report joins, both operational-measurement publications, and
  the exact-small snapshot/Oracle chain must coexist with the Raw runner and
  validators in one clean stamped source commit. A later activation review
  must prove that complete chain and remove or satisfy the immutable
  pre-fixture closure. Raw validators or a Raw implementation alone are
  insufficient.
- Keep exact cases 10101 and 10102, every other development cell, diagnostic
  report execution, operational replay, snapshot, Oracle, fixed-query, stress,
  imported, heldout, aggregation, matrix, decision, campaign, and acquisition
  paths closed in this slice. Protocol-v2 evidence cannot be promoted,
  relabeled, rechecksummed, or accepted under a v3 authority.

## Consequences

The repository may implement and adversarially test the Session-v5 Raw
preflight, parsing, serialization, and strict validation boundary without
observing an allocation result or weakening a historical authority. The
complete consuming-publication chain can be built in later narrow slices
against these fixed identities.

No development outcome may be acquired under this contract alone. No
performance, improvement, feasibility, fixed-pool optimality, calibration
non-regression, heldout, confirmatory, Phase 4, or M1 claim follows from this
inactive authority.

## Rejected alternatives

- **Activate Raw before its consumers exist.** Roster v4 requires the complete
  successor chain from one clean stamped source before either designated cell
  may run.
- **Treat the Raw validators as the complete consuming-publication chain.**
  They authenticate the first artifact boundary; they do not provide the
  required report, operational, snapshot, or Oracle consumers.
- **Widen the Session-v4 runner or reuse its execution enum.** Those surfaces
  remain bound to Protocol v2, roster v3, and authentic historical evidence.
- **Select Session v5 with a flag, source commit, or matching checksum.**
  Configuration authority must be fixed out of band by a separately named
  compiled boundary and reconstructed completely.
- **Add an unused execution enum.** A new identity without a consuming
  preflight creates an ambiguous switch surface and is not an observable
  vertical slice.
- **Allow a fixture-backed test target.** A test capable of allocation would
  create an unreviewed observation path before activation.
- **Mint v3 payload or carrier schemas.** Protocol v3 reserves new artifact
  authorities while explicitly retaining the existing payload formats.
- **Open downstream or heldout work with Raw.** Each consuming authority and
  the eventual activation require their own bounded adversarial review.
