# ADR-034: Phase 4 Fixed-Query Controls

**Status:** Accepted for the twenty-first Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Fixed initial candidate-preparation opportunity and diversity-shape diagnostics

## Context

The frozen corpus includes one-net/many-alternative, many-net/one-candidate,
and realistic 4/8/16-pool shapes with the same `N*K=1024` initial preparation
opportunity. The paired trial adds two regeneration queries per net, so the
three buildable shapes do not have equal whole-trial query opportunities.
Building the two endpoint shapes solely to manufacture timing would also
contradict their descriptor-only exclusion.

## Decision

- Adopt `schemas/benchmark/phase4_fixed_query_control_v1.md`.
- Limit the equal-query comparison to initial candidate preparation and publish
  actual consumption separately from the common opportunity.
- Keep cases 2000 and 2001 descriptor-only, with explicit unavailable
  execution measurement and no numeric placeholders.
- Require complete Raw, per-net report, and operational authorities for cases
  2002-2004, sharing source, environment, and execution caps.
- Publish requested and observed diversity work only as sums of within-net
  unordered candidate pairs. Never form cross-net pairs.
- State the unequal 1536/1280/1152 whole-trial query opportunities explicitly
  and prohibit an equal-query timing interpretation across shapes.

## Consequences

The five fixed-query matrix cells close under a deterministic, bounded,
rebuilding publication without claiming unperformed endpoint measurements or
equal whole-trial timing. The artifact remains diagnostic-only and does not
complete Phase 4.
