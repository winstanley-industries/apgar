# APGAR Documentation

## Source-of-truth order

1. `APGAR_Architecture_Specification_v0.1.md` is the authoritative living
   architecture and requirements document.
2. Focused architecture decision records, once added under `docs/adr/`, refine
   decisions without silently overriding the specification.
3. `KICKOFF.md` preserves the initial research framing and motivation.
4. `epics/` tracks non-normative delivery sequence and current progress. An
   epic cannot create or override an architecture requirement, ADR, schema, or
   evidence result.
5. The repository `README.md` is a concise public orientation, not a complete
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
- `epics/README.md`: epic lifecycle, status vocabulary, update policy, and
  active-epic index.
- `epics/EPIC-001-phase4-reboot.md`: clean Phase 4 restart, salvage boundary,
  PR sequence, and stop gates.
- `adr/ADR-068-phase4-clean-reboot.md`: accepted clean-restart decision and
  archived-lineage boundary.
- `adr/ADR-069-deterministic-cpu-candidate-pool-preparation.md`: bounded,
  worker-invariant production CPU candidate-pool preparation and mixed-draft
  publication boundary.
- `adr/ADR-070-canonical-sequential-negotiated-routing-baseline.md`: bounded
  canonical sequential rip-up-and-reroute comparison policy, incumbent
  handling, congestion snapshots, and deterministic termination.
- `adr/ADR-071-bounded-negotiated-regeneration-plan.md`: candidate-allocator
  present/historical price schedule, canonical hot resources, and bounded
  deterministic targeted-regeneration plans.
- `adr/ADR-008-exact-coordinate-arithmetic.md`: accepted arithmetic envelope,
  KiCad fixture unit scale, and exact boundary semantics for Board IR v1.
- `adr/ADR-009-m1-sparse-field-and-planar-reference-semantics.md`: accepted
  sparse active-region, directional-edge, telemetry, and planar CPU reference
  semantics for Phase 1.
- `adr/ADR-010-phase2-planar-gpu-bakeoff.md`: accepted hermetic CUDA,
  backend-validation, benchmark-evidence, and current planar dispatch decisions.
- `adr/ADR-011-phase3-candidate-identity-admission-and-store.md`: accepted
  candidate identity, exact admission, and immutable store contracts.
- `adr/ADR-012-deterministic-alternative-policies-and-batched-exploration.md`:
  accepted deterministic alternative generation and batched exploration.
- `adr/ADR-013-phase3-candidate-dispatch-conclusions.md`: accepted Phase 3
  production CPU dispatch and experimental-backend conclusions.
- `adr/ADR-014-persistent-compact-cuda-sweep.md`: accepted persistent bounded
  CUDA workspace and compact-readback trust contracts.
- `../benchmarks/phase2_planar_dispatch_report.md`: reproducible Phase 2 CPU,
  CUDA frontier, and CUDA sweep dispatch evidence.
- `../schemas/candidate/route_candidate_v1.md`: immutable candidate identity and
  resource-use representation.
- `../schemas/candidate/store_v1.md`: deterministic candidate admission,
  storage, and batch-publication contract.
- `../schemas/routing/alternative_policy_v1.md`: deterministic alternative
  policy and request-identity contract.
- `../schemas/gpu_batch_replay/v1.md`: checksummed batched-GPU invariant replay
  contract.
- `../schemas/board_ir/v1.md`: normalized M1 Board IR entities,
  canonicalization, fingerprinting, and deliberate schema limits.
- `../schemas/compiled_board/v1.md`: versioned CompilerProfile, rule bucket,
  sparse directional field, telemetry, and CPU reference consumption contract.
- `../schemas/device_compiled_board/v1.md`: versioned immutable GPU device view
  and stable incoming-heading state indexing.
- `../schemas/gpu_replay/v1.md`: checksummed GPU invariant replay contract.
- `../schemas/benchmark/planar_bakeoff_v1.md`: Google Benchmark JSON and APGAR
  domain-counter contract for Phase 2 dispatch evidence.

## Change policy

- Preserve requirement IDs when editing existing requirements.
- Add rationale for new MUST or MUST NOT requirements.
- Mark architecture decisions as proposed or accepted.
- Keep performance claims tied to reproducible evidence.
- Use exact legality language: false-blocked search space affects quality;
  false-free search space is a correctness defect.
