# ADR-040: Phase 4 Canonical Corpus Execution Envelope

**Status:** Accepted for the twenty-seventh Phase 4 vertical slice

**Applies to:** Canonical algorithm bounds for the frozen Phase 4 matrix

## Context

The frozen Phase 4 cell roster declared 100 successful Raw cells, but the
canonical spec builder inherited standalone allocator defaults. In particular,
`maximum_reconstruction_states=100000` combined with a 700000-record baseline
occupancy cap. Every 64-net calibration cell therefore failed the conservative
`N*states` occupancy preflight, and larger success cells failed before routing
as well. Reducing only that product exposed additional generic candidate-draft,
generated-byte, retained-byte, policy, and CandidateStore transaction bounds.

These were configuration defects, not observed allocator outcomes. Leaving
them in place would have converted the frozen success roster into guaranteed
missing evidence.

## Decision

- Preserve the representative descriptor roster, protocol cells, roles,
  inference rules, thresholds, and corpus checksum.
- Let `M` be the largest pool declared by a descriptor and set the canonical
  reconstruction bound to `32*M`, even for a smaller active pool. The corpus
  geometry proves the longest supported portal or fragmented witness uses at
  most `24*M+14` states; pin-field and imported witnesses are smaller.
- Let `B=N*32*M`. Use `B` for price records and selected occupancy, and reserve
  `2*B+N` One-World resource records for price, occupancy, and capacity
  overrides.
- Derive baseline, initial-preparation, regeneration, retained, policy,
  rejection, transient, and CandidateStore atomic-input caps with widened
  arithmetic from that same envelope. Reject arithmetic or public hard-limit
  overflow rather than silently clamping an executable success cell.
- Raise the targeted-regeneration aggregate policy-entry public hard limit from
  100,000,000 to 250,000,000. The largest successful canonical cell requires
  134,218,752 entries under the conservative complete-price-roster projection;
  only the two predeclared compiled-work-bound stress cases may clamp this
  otherwise unreachable downstream configuration field.
- Keep stress cases 3001 and 3002 as compiled-work-bound evidence. They retain
  deterministic budget checksums but are not successful allocator executions.
- Refresh every canonical-algorithm-budget checksum in the representative
  manifest before collecting matrix observations, and bind the exact 102-entry
  roster through Statistical Decision Protocol v4.

## Consequences

All 100 frozen success cells now have closed, case-specific configuration
preimages instead of unrelated generic defaults. This correction does not
assert that the cells pass wall, RSS, correctness, guardrail, or statistical
gates; those remain observed matrix outcomes. Any future geometry or bound
change must refresh the manifest and protocol ancestry before observation.
