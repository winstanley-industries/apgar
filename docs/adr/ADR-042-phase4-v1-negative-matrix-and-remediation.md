# ADR-042: Phase 4 v1 Negative Matrix and Remediation Boundary

**Status:** Accepted for the twenty-ninth Phase 4 vertical slice

**Applies to:** The first complete Protocol v4 observation and any
outcome-informed Phase 4 remediation

## Context

The first complete canonical Phase 4 acquisition ran from clean source commit
`7093e8994dc86d0086813bf618bc41b68da05e0b`. It produced all 104 logical
cells, all 487 required evidence files, and all nine completion authorities.
The checksummed Matrix Decision Publication is authentic and complete, but its
decision is negative:

- `matrix_evidence_status` is `complete`;
- `phase4_exit_status` is `failed`;
- `phase4_complete` is `false`;
- 75 of the 78 exact-rejection-scope cells violate the zero-rejection
  guardrail; and
- only `pin_field_crossbar_v1` is Holm-qualified, while Phase 4 requires two
  qualifying families.

The exact-rejection failures are observed decision results, not an aggregation
or evidence-integrity defect. Same-Run Decision Telemetry reports nonzero
`CandidateRejectionCode::kExactValidation` columns in every synthetic exact and
held-out cell. The imported cells have zero. Ephemeral diagnostic probes
suggested at least two representative invariant classes:
`candidate.geometry.swept_self_overlap.v1` and
`candidate.terminals.unintended.v1`. Those probes are remediation hypotheses,
not fields or causal findings authenticated by the matrix publication.

The family ties are also authentic. At primary pool size 8, all eight portal
held-outs and all eight fragmented-maze held-outs tie the sequential baseline.
The pin-field contender wins all eight held-outs.

The v1 held-outs have now been observed. Remediation selected using those
outcomes cannot treat the same cases as unseen evidence for a later
uncertainty-qualified success claim.

## Decision

- Preserve Representative Corpus v1, Statistical Decision Protocol v4, and
  the complete negative decision without changing their cases, authority
  assignments, guardrails, inference, or thresholds.
- Record the observation in
  `docs/evidence/phase4-v1-matrix-7093e899.md`, including independent file and
  artifact checksums. A complete negative publication is durable evidence and
  must not be discarded when a later implementation improves.
- Keep Phase 4 open. Complete evidence acquisition is not Phase 4 completion;
  only `phase4_complete=true` closes the Section 29.2 gate.
- Fix exact-invalid synthetic routing behavior at its source. Do not relabel
  exact-validation rejections, pool them across cells, convert them into
  missing evidence, or weaken the zero-rejection guardrail.
- Pursue a second family improvement through general candidate-generation and
  allocation behavior. The current portal family is the narrowest target:
  candidate selection must retain an exact-admissible alternative that avoids
  an actually overused portal resource and reduces board-level overuse without
  reducing selected-net count.
- Before a success claim, version a new representative corpus and statistical
  protocol with fresh, previously unobserved held-outs. Preserve the same
  board-level lexicographic outcome, exact sign test, Holm correction,
  non-regression guardrails, minimum two-family requirement, globally coupled
  family requirement, and completion authorities.
- Freeze the new corpus, protocol, and implementation before any new held-out
  execution. Remediation may use the preserved v1 observation and explicitly
  designated new calibration cases, but must not inspect new held-out
  outcomes. Any pre-decision observation of those held-outs invalidates that
  set and requires another version with fresh cases.
- Version a corresponding Matrix Decision Publication schema, validator,
  aggregator, and acquisition-runner authority lineage for that corpus and
  protocol. Preserve the v1 publication schema, validator, aggregator behavior,
  exact 487-file inventory, and negative publication unchanged; they are
  checksum-frozen to Protocol v4 and cannot validate a replacement matrix.
- Run the complete new matrix from one clean commit. Phase 4 completes only
  when its new authentic decision publication says both
  `phase4_exit_status=passed` and `phase4_complete=true`.

## Consequences

The first canonical result remains visible as a negative scientific result
rather than being overwritten by a tuned rerun. Remediation can use its
diagnostics, but final uncertainty-qualified evidence must come from fresh
held-outs.

This decision adds no GPU allocator requirement and does not move
legalization, APGAR DRC, host-CAD validation, or specialty routing into Phase
4. Those boundaries remain unchanged.
