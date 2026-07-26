# ADR-059: Phase 4 Confirmatory H=4096 Development Observation

**Status:** Accepted for the seventh post-Protocol-v2 confirmatory slice
**Date:** July 26, 2026
**Applies to:** Outcome-informed remediation after the two authorized H=4096
development cells and before campaign, heldout, or decision acquisition

## Context

ADRs 053 through 058 opened two development-only H=4096 evidence paths:
exact cell `(10100,4)` and ordinary calibration cell `(10200,8)`. They remain
separate authorities with different Raw carriers, roles, payload joins, and
source commits.

The exact bundle was acquired from clean source commit
`ec3d22dab978f797c06180c3821b1b97d6e80fa1`. Its independently replayed and
exhaustively enumerated eight-world Cartesian product has one unique optimum.
The production candidate selection equals that optimum at
`(selected=6,overuse_units=1,intrinsic_cost=159000)`. This is fixed-pool
optimality, but the remaining overuse prevents a resource-feasibility claim.

After the ordinary operational authority and its process-test hermeticity fix
were reviewed and committed, the complete calibration bundle was freshly
acquired from clean source commit
`b57f885ed2dc820bfb26b3b0742810286338e9c6`. All 20 balanced paired
repetitions prefer the baseline:

- baseline: `(selected=64,overuse_units=0,intrinsic_cost=1874600)`;
- reusable candidate:
  `(selected=64,overuse_units=16,intrinsic_cost=1520200)`.

The lower candidate intrinsic cost cannot compensate for worse resource
overuse in the frozen lexicographic objective. The per-net diagnostic reports
111 admitted candidates from 640 requested columns, with 465 duplicate and
64 disconnected rejections. Its 64 final pools contain only one or two
candidates, and every pool is below the requested size of eight. Those counts
identify a remediation surface; they do not prove whether generation,
retention, pricing, or world selection caused the negative outcome.

## Decision

- Preserve both authentic negative development observations. Their artifact
  registries and interpretation are recorded in
  `docs/evidence/phase4-confirmatory-h4096-development-b57f885.md`.
- Keep the exact and calibration bundles source-bound and independent. They
  may be interpreted together as two observations of the reviewed H=4096
  hypothesis, but they are not a mutually joinable artifact bundle. No file,
  checksum, timing, process identity, or source envelope from one commit may
  satisfy an authority bound to the other.
- Reject the current H=4096 configuration hypothesis as sufficient authority
  to begin the confirmatory campaign. Exact-cell fixed-pool equality does not
  offset the calibration candidate's 20-of-20 lexicographic regression.
- Keep Confirmatory Decision Protocol v2, canonical algorithm-budget roster
  v3, their reserved artifact authorities, and every observed artifact
  byte-unchanged. A checksum-bound configuration, case, budget, role, carrier,
  threshold, or authority preimage must not be changed in place. Such a change
  requires a separately versioned authority and contract-first review before
  execution.
- Diagnose whether a proposed remediation is a conformance correction or an
  intentional policy change before editing production behavior. A conformance
  correction must add a discriminating regression for the pre-fix defect. A
  change to candidate-policy, store, regeneration, session, price, one-world,
  or multi-world decision semantics requires new child contracts and consuming
  authorities before a new roster and protocol can bind it.
- Pursue remediation only through general candidate-generation, candidate
  retention, pricing, regeneration, or allocation behavior exercised by open
  development cells. Do not add a case-ID branch, H=4096-only output repair,
  evidence relabeling, or validator exception.
- Treat the candidate's 465 duplicate and 64 disconnected rejections, thin
  pools, zero exact-validation rejections, and 16 selected candidates above
  their pool-best intrinsic cost as diagnostics rather than causal proof.
  Remediation must be justified by an explicit algorithmic contract and
  focused deterministic tests, not by restating these counters as a root
  cause.
- Review and commit each remediation slice before acquiring evidence from its
  exact clean source commit. An implementation change invalidates the source
  association of both retained bundles; it does not permit either bundle to
  be copied, relabeled, or rechecksummed under the new commit.
- Before proposing a heldout-acquisition authority, reacquire the two
  designated development observations under the reviewed remediation. The
  exact cell must retain independently proven production/fixed-pool objective
  equality, and the calibration candidate must no longer lexicographically
  regress its baseline. Meeting this boundary establishes only remediation
  readiness for a separately reviewed campaign authority.
- Keep fixed-query, stress, aggregation, matrix, decision, heldout, imported,
  and every other development path closed. Previously observed Corpus-v1
  heldouts remain historical and cannot become unseen confirmatory evidence.

## Consequences

H=4096 remains a rejected development hypothesis in its current
implementation state, not a failed evidence pipeline. The exact and
calibration artifacts are complete for their narrow authorities and remain
useful for regression and remediation analysis.

No statistical performance, candidate-pool completeness, global optimality,
resource feasibility, combined-board legality, final legality, confirmatory
matrix, Phase 4 completion, or M1 completion claim follows from this
observation.

## Rejected alternatives

- **Open the campaign because the exact Oracle matches production.**
  Fixed-pool equality with one overuse unit is neither feasibility nor evidence
  that the ordinary calibration candidate is non-regressing.
- **Explain the calibration loss solely from pool cardinality.** Thin pools
  and rejection codes are authenticated diagnostics, but no published field
  identifies a unique causal subsystem.
- **Use the lower candidate intrinsic cost as a win.** The frozen objective
  compares selected count, then overuse, then intrinsic cost. The candidate
  loses before cost can decide.
- **Join or average measurements across the two source commits.** The
  authorities require exact clean-source association and do not authorize a
  cross-commit timing population.
- **Retune a frozen checksum-bound field under the existing authority.**
  In-place semantic mutation would make the frozen protocol and roster
  misleading; a new preimage requires a new reviewed authority.
