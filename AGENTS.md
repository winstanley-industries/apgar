# APGAR Agent Guide

This file governs work throughout the repository.

## Read before changing code

1. Read `docs/APGAR_Architecture_Specification_v0.1.md`.
2. Read `docs/KICKOFF.md` for the research rationale behind the specification.
3. When work belongs to an active epic, read `docs/epics/README.md` and the
   active epic before planning or editing.
4. Keep the current milestone and explicit non-goals in view; do not implement
   deferred specialty routing while foundation contracts are unsettled.

When documents disagree, the architecture specification is authoritative.
Record intentional architecture changes in the specification or a focused ADR
rather than allowing the implementation to silently redefine the design.

## Non-negotiable contracts

- Exact Board IR geometry and rules are authoritative.
- A compiled legal movement must imply exact legality against every represented
  static obstacle. False-blocked space may be tolerated and measured;
  false-free space is a correctness defect.
- GPU results are untrusted until bounds, reconstruction, geometry, and rule
  invariants pass.
- Every correctness-critical GPU primitive needs a CPU reference path suitable
  for differential testing.
- Route generators produce immutable candidates; they do not mutate global
  congestion or board state while searching.
- Global allocation consumes candidate/resource abstractions rather than CAD
  editor objects.
- The host CAD engine remains the final validation authority before commit.
- Identical board, configuration, seed, and supported backend/device class must
  produce deterministic externally visible results.
- Unsupported rules must be declared and delegated, never weakened silently.

## Initial milestone boundary

M1 is deliberately narrow: two-terminal single-ended nets, two or four signal
layers, through vias, H/V/45-degree routing, line-segment output, conservative
compiled fields, CPU A*, one GPU generator, exact candidate validation,
benchmarks, and replay.

Do not pull shove routing, copper pours, differential pairs, buses, arbitrary
angles, length tuning, or signal-integrity sign-off into M1 unless the
architecture specification is explicitly revised.

## Engineering discipline

- Prefer small vertical slices with observable inputs and outputs over empty
  subsystem scaffolding.
- For epic work, update the epic's current-state summary and task table in the
  same pull request as the corresponding implementation or evidence change.
- Keep global coordinates in signed 64-bit database units. Quantize imported
  floating-point coordinates once at the adapter boundary.
- Make boundary equality and one-unit perturbations explicit in geometry tests.
- Every optimization claim must name the corpus, configuration, hardware, exact
  commit, and comparison baseline.
- Keep serialized schemas versioned and replay artifacts checksummed.
- Preserve unrelated worktree changes and stage files explicitly.
- Use Bazel as the canonical build interface. Do not introduce a competing
  CMake, Make, or language-specific top-level build without an explicit project
  decision.

## Ephemeral agent artifacts

Place cross-agent review reports, temporary handoff notes, probe output, and
similar working documents under `.agent-scratch/`. The directory is git-ignored
and its contents are not a durable source of truth. Promote lasting decisions
or contracts into the architecture specification, a focused ADR, a versioned
schema, or tests before discarding the scratch artifact.

## Required validation

Run the narrowest relevant checks during development, then the repository-wide
lint, build, and test targets before committing:

```sh
bazel lint
bazel build //...
bazel test //...
```

Use `bazel test --lockfile_mode=error //...` for the final dependency-lock
check. Use `bazel lint --fix` to apply supported formatters. GPU changes
additionally require CPU/GPU differential tests and a replayable failure
artifact for any new invariant class.

If a required tool or accelerator is unavailable, report exactly which check
was not run; do not present an unexecuted check as passing.
