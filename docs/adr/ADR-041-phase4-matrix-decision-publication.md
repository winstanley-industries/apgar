# ADR-041: Phase 4 Matrix Decision Publication

**Status:** Accepted for the twenty-eighth Phase 4 vertical slice

**Applies to:** Complete matrix aggregation and the Phase 4 exit decision

## Context

Statistical Decision Protocol v4 freezes the 104 logical cells and every
decision rule, but the repository had no durable bundle layout, full-matrix
authority join, or observed-decision publication. Per-cell checksums alone
cannot establish coverage, and a missing or failed attempt must not be
laundered into an allocator loss.

## Decision

- Adopt
  `schemas/benchmark/phase4_matrix_decision_publication_v1.md`.
- Keep raw evidence outside the Git worktree during acquisition so benchmark
  stamping continues to name one clean commit.
- Derive the exact bundle inventory from Protocol v4. Reject missing, extra,
  aliased, reordered, foreign-source, or wrongly disposed evidence before
  computing a decision.
- Rebuild every Raw/report/capture/operational/oracle/fixed/stress authority
  structurally. Bind all file hashes, artifact/source checksums, the complete
  protocol ancestry, corpus, canonical budget roster, host environment, and
  provenance.
- Use Raw as the only board-outcome and paired-timing authority. Use Same-Run
  Telemetry as the only exact-rejection authority. Keep all operational
  measurements diagnostic.
- Apply the frozen exact sign test, Holm correction, per-cell guardrails, and
  nine completion requirements. Emit no publication for incomplete evidence;
  emit a checksummed failed publication for an authentic negative result.
- Install durable output atomically without replacement and make validation
  rebuild the complete artifact from its external bundle.

## Consequences

One clean source commit can now produce a deterministic, independently
revalidatable Phase 4 decision. A passing artifact closes only the
global-allocation evidence gate. Legalization, DRC, host-CAD validation,
specialty routing, and later M1 work remain out of scope.
