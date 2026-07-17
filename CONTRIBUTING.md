# Contributing to APGAR

APGAR is at the architecture and foundation stage. Changes should strengthen a
testable contract or complete a small vertical slice; broad placeholder
scaffolding makes the architecture harder to evaluate.

## Before starting

- Read `AGENTS.md` and the architecture specification.
- Identify the requirement IDs or architecture decision affected by the change.
- Keep M1 changes inside the stated milestone boundary unless the specification
  is being revised deliberately.

## Development workflow

1. Start from an up-to-date branch with an understood working tree.
2. Add or update tests alongside behavior.
3. Run the narrowest relevant checks while iterating.
4. Run repository-wide formatting, build, and tests before committing.
5. Keep commits focused and describe observable behavior rather than internal
   activity.

Bazel is the canonical build interface. Build and test the repository with:

```sh
bazelisk build //...
bazelisk test //...
```

See `docs/BUILDING.md` for the pinned toolchain, sanitizer configurations,
formatting command, and hermeticity boundary.

## Correctness expectations

- Exact geometry, rule boundaries, and connectivity take priority over speed.
- GPU implementations must be tested against CPU reference implementations.
- Conservative compilation may block legal space, but it must never expose
  exactly illegal movement as legal.
- Incremental behavior must be checked against a clean rebuild.
- Host-CAD disagreement must be retained as a regression artifact.

## Performance claims

A performance result is meaningful only when it records the corpus, objective,
configuration, hardware/backend, build ID or commit, warm-up policy, and
baseline. Route quality and completion must be reported alongside runtime.

## Documentation changes

The architecture specification is authoritative. `docs/KICKOFF.md` explains
the research direction and should not acquire requirements that conflict with
the specification. Significant decisions should become focused ADRs with
status, rationale, alternatives, and consequences.
