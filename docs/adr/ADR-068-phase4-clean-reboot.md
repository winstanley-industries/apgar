# ADR-068: Phase 4 Clean Reboot

**Status:** Accepted for the Phase 4 reboot
**Date:** August 1, 2026
**Applies to:** Phase 4 development after the archived experimental lineage

## Context

Phase 4 began from clean Phase 3 commit
`1a0d6600111c44e7f915b039190f9125d37d4a06`. The first implementation lineage
eventually reached
`1f68ded7ff36547c3ffb8a0629482ad425786106`: 71 commits touching 447 files and
adding 146,527 lines relative to that baseline.

That lineage implemented substantial allocator behavior, but its evidence
control plane grew into a second product. Configuration changes propagated
through multiple authority, carrier, validator, report, operational, and
preflight layers. The final committed slices deliberately could not run the
current allocator or produce an allocation observation. This contradicted the
repository preference for small vertical slices with observable inputs and
outputs and left the Phase 4 hypothesis undecided.

The complete lineage is preserved at remote branch
`archive/phase4-pre-reset-2026-08-01`, whose tip is exactly `1f68ded`. It is a
historical donor and audit record, not the active Phase 4 source of truth.

The active architecture specification already states the compact Phase 4
contract: implement prices, worlds, selection, and column generation, then beat
the sequential baseline on selected synthetic families. It does not require
the archived publication graph.

## Decision

- Restart Phase 4 from exact Phase 3 baseline
  `1a0d6600111c44e7f915b039190f9125d37d4a06` on a new development branch. Do
  not reset or delete the existing local `main` branch in place.
- Preserve the archived lineage unchanged. No archived implementation, ADR,
  schema, fixture, benchmark, or evidence result is automatically active in
  the reboot.
- Reintroduce desired behavior manually in bounded, dependency-ordered slices.
  Do not cherry-pick the original oversized commits. Every salvaged behavior
  receives current review, focused tests, and the required repository gates.
- Continue global ADR numbering at ADR-068. Identifiers ADR-015 through ADR-067
  remain reserved for the archived lineage and MUST NOT be reused for different
  decisions in the reboot.
- Reserve archived serialized schema identities as well. Reuse an archived
  path and major version only by restoring a compatible contract; give an
  incompatible reboot contract a distinct identity or a new major version.
- Track the reboot in
  `docs/epics/EPIC-001-phase4-reboot.md` under the non-normative policy in
  `docs/epics/README.md`. Architecture, ADRs, schemas, and evidence retain their
  separate authorities.
- Optimize the reboot for an honest decision, not a guaranteed positive result.
  A complete negative readiness or campaign outcome terminates the engineering
  epic without satisfying the positive Phase 4 exit criterion.
- Keep campaign-only implementation conditional on the development-readiness
  gate. Freeze the development workload identities, configurations, budgets,
  and mechanical comparison rule before acquisition; do not rebuild campaign
  publication machinery before the allocator is credible on those predeclared
  cells.
- Treat source commit, corpus, configuration, seed, hardware, and toolchain
  identity as reproducibility bindings on a trusted canonical runner. Stronger
  hostile-operator attestation requires a separate decision and is not part of
  this reboot.

## Salvage boundary

The reboot evaluates behavior rather than accepting archived files wholesale:

- preserve the existing Phase 3 Candidate Store, exact-admission, CPU-oracle,
  replay, and immutable-candidate contracts already present at the baseline;
- reimplement and re-review authentic multi-net workloads, resource capacities,
  deterministic selection, pool preparation, the sequential baseline, bounded
  prices, targeted regeneration, and any retained multi-world/session behavior;
- treat the archived primary-conflict coverage and H=4096 changes as unproven
  remediation hypotheses, not foundations; and
- do not port the archived Raw/report/telemetry/operational/preflight authority
  graph. Historical negative observations may inform development but cannot be
  relabeled as reboot evidence.

## Consequences

- Some correct code will be reintroduced rather than inherited. This costs
  engineering time but restores reviewable causality and clean dependencies.
- The archive makes discarded work recoverable and preserves the rationale for
  future comparison. Its existence does not create a backlog to finish.
- The reboot may end with a negative result. Such a result is a useful project
  decision, not permission to create an unbounded successor protocol inside the
  same epic.
- Replacing or rewriting `main`, deleting the archive, or publishing stronger
  provenance claims requires separate explicit approval.

## Rejected alternatives

- **Continue from `1f68ded` and finish the successor chain.** This preserves the
  control-plane coupling that caused the reset and delays allocator observation.
- **Restart from allocator-session commit `81a5620`.** This removes later
  evidence machinery but retains nine large Phase 4 commits and 34,404 added
  lines without re-establishing PR-sized review boundaries.
- **Reset `main` directly.** The archived lineage was local-only before this
  decision; moving `main` first would create needless recovery risk.
- **Cherry-pick the original allocator commits.** Their sizes and coupled
  contents would import the sequencing problem rather than correct it.
- **Reuse ADR-015 for the reboot.** Two different decisions with the same ADR
  identifier across preserved histories would make references ambiguous.
