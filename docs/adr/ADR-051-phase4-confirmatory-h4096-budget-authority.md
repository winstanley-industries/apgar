# ADR-051: Phase 4 Confirmatory H=4096 Budget Authority

**Status:** Accepted for the seventh post-freeze confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Acquisition-free canonical configuration remediation after the exact-small gate

## Context

ADR-050 records an authentic development-only result for exact cell
`(10100,4)`: the frozen H=2250 production search did not select the
fixed-pool exact optimum, so no confirmatory exact-small Oracle Artifact could
be published. The history price is part of the complete checksum-bound
baseline and candidate-session configuration. Changing it in place would
silently reinterpret Representative Manifest v2, its 102 budget checksums,
Confirmatory Decision Protocol v1, and every existing confirmatory publisher.

Representative Manifest v2 remains the case authority and Workload-Net Roster
Manifest v2 binds its checksum directly. Minting a sparse Representative
Manifest v3 for an H-only change would overload that case/workload contract and
create an ambiguous roster ancestry.

## Decision

- Preserve the H=2250 configuration emitted by
  `BuildPhase4CanonicalTrialSpecForCorpusV2`, its canonical roster stdout,
  Representative Manifest v2, Workload-Net Roster Manifest v2, Confirmatory
  Decision Protocol v1, and every existing authority checksum. Existing target
  names remain unchanged; binary byte identity is not claimed.
- Add a fixed internal
  `BuildPhase4CanonicalTrialSpecForCorpusV2H4096` configuration-preimage
  capability in a Bazel-private, dead-section-eliminated dependency graph. It
  retains Corpus v2, `present_step_per_overuse_unit=1`, and applies
  `history_step_per_overuse_unit=4096` identically to the sequential baseline
  and reusable-candidate session.
- Freeze
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3` with configuration
  authority `phase4_confirmatory_corpus_v2_h4096`. It binds Representative
  Manifest v2, Workload-Net Roster Manifest v2, the superseded H=2250
  roster-v2 checksum, the explicit equal-arm price change, and all 102 ordered
  H=4096 per-cell budget checksums under a fresh aggregate checksum domain.
- Require all 102 H=4096 specs to equal their H=2250 counterparts after
  normalizing only the two equal-arm history-price fields. Root seeds,
  preparation, query/work opportunity, external limits, schedules, and every
  other canonical configuration field remain unchanged. Every H=4096
  per-cell checksum must match the live fixed builder and differ from its
  H=2250 counterpart.
- Keep the H=4096 authority inactive. Its generator constructs configuration
  preimages only, takes no caller-selected input, and contains no linked
  fixture, case-construction, worker, or allocator-execution symbol.
- Reject ASan- or UBSan-instrumented builds of the generator because sanitizer
  registration roots otherwise-dead shared-source sections and violates that
  capability-minimal link contract. The sanitizer gates instead run the
  instrumented H=4096 structural test and link-inspect the exact ordinary
  generator through a narrowly scoped sanitizer-reset audit transition.
- Positively require equal-arm `present=1,history=2250` in every existing
  Corpus-v2 ordinary, diagnostic, same-run, operational, replay, and snapshot
  execution entry point. Reject any other price authority before case,
  fixture, preparer, or worker access. H=4096 execution requires separately
  named entry points frozen only after Protocol v2.
- Do not infer H=4096 from corpus version, case ID, cell-plan checksum, wire
  schema, artifact schema, source commit, or a matching budget checksum. Later
  validation and execution require separately named fixed authority entry
  points.

## Consequences

The repository has a complete, reproducible H=4096 configuration hypothesis
without changing historical authority or observing a heldout outcome. This
slice does not claim H=4096 restores exact optimality, does not authorize a
development rerun, and does not create publishable Raw or downstream evidence.

Before any H=4096 acquisition, a separate Confirmatory Decision Protocol v2
must bind this roster, refresh the clean-commit observation firewall, and
freeze a configuration-specific artifact namespace. Exact and calibration
execution entry points then require their own adversarially reviewed slices.
All heldout, imported, fixed-query, stress, matrix, and decision paths remain
closed.

## Rejected alternatives

- **Change the H=2250 builder or Manifest v2 in place.** This rewrites frozen
  evidence semantics.
- **Infer a profile from an opaque checksum.** Authority selection must be
  trusted and out of band.
- **Call the budget-only artifact Representative Manifest v3.** That name
  implies a new case/workload root and would require a workload-roster rebind.
- **Create Representative Corpus v3.** Case and workload identity did not
  change; only a nested canonical configuration changed.
