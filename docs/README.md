# APGAR Documentation

## Source-of-truth order

1. `APGAR_Architecture_Specification_v0.1.md` is the authoritative living
   architecture and requirements document.
2. Focused architecture decision records, once added under `docs/adr/`, refine
   decisions without silently overriding the specification.
3. `KICKOFF.md` preserves the initial research framing and motivation.
4. The repository `README.md` is a concise public orientation, not a complete
   design contract.

If two documents disagree, correct the lower-authority document or revise the
architecture explicitly.

## Current documents

- `APGAR_Architecture_Specification_v0.1.md`: normative requirements, component
  boundaries, data contracts, algorithms, testing strategy, roadmap, risks,
  open questions, and references.
- `KICKOFF.md`: project vision, early research summary, and the architectural
  proposal that led to the full specification.
- `BUILDING.md`: canonical Bazel commands, pinned toolchain behavior, and the
  current hermeticity boundary.

## Change policy

- Preserve requirement IDs when editing existing requirements.
- Add rationale for new MUST or MUST NOT requirements.
- Mark architecture decisions as proposed or accepted.
- Keep performance claims tied to reproducible evidence.
- Use exact legality language: false-blocked search space affects quality;
  false-free search space is a correctness defect.
