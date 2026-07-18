---
name: apgar-review
description: Perform architecture-first, high-recall code reviews for the APGAR repository. Use when reviewing an APGAR working-tree diff, branch, commit range, pull request, implementation phase, geometry/compiler/router/GPU change, or when auditing fixes against APGAR correctness contracts, M1 scope, schemas, ADRs, determinism, exact geometry, Bazel gates, and CPU/GPU differential-test requirements.
---

# APGAR Review

Review APGAR against its authoritative architecture, not only against local code style. Freeze the exact review surface, search independently from several APGAR-specific angles, verify every retained candidate, and report concrete failure scenarios.

## Preserve review integrity

- Treat review as read-only unless the user explicitly asks for fixes.
- Preserve unrelated dirty-worktree changes.
- Put finder reports, probes, candidate ledgers, and handoffs under `.agent-scratch/`.
- Promote lasting conclusions only through a requested code fix, test, schema, specification update, or focused ADR.
- Do not call a moving target reviewed. If the target changes, re-snapshot and re-check affected findings.

## 1. Establish the exact target

Choose the target named by the user. Otherwise review the current branch plus all tracked and untracked working-tree changes.

Before analysis:

1. Read `AGENTS.md` completely.
2. Read `docs/APGAR_Architecture_Specification_v0.1.md` and `docs/KICKOFF.md` completely.
3. Read the current phase's ADRs, schemas, public headers, tests, and any narrower `AGENTS.md` files governing changed paths.
4. Record the base OID, head OID, branch, changed paths, untracked paths, and file hashes.

For a working-tree review, run:

```sh
python3 .agents/skills/apgar-review/scripts/review_scope.py capture
```

The command writes an ignored manifest under `.agent-scratch/` and prints its path. Keep that path for the final drift check. For a clean committed branch or PR, record the exact base/head OIDs and review from a clean worktree; do not let unrelated local edits contaminate file reads.

If scope is ambiguous, inspect upstream, `origin/main`, `main`, and the current status before selecting a base. Include untracked files explicitly; ordinary `git diff` omits their contents.

## 2. Build a contract map

Map changed behavior to named requirements before judging implementation:

- exact Board IR and exact geometry authority;
- GC-001 conservative compiled movement and false-free prohibition;
- CPU reference and differential validation for correctness-critical GPU work;
- immutable candidates and allocator separation;
- validation of untrusted GPU/search artifacts;
- deterministic externally visible results;
- explicit unsupported/delegated rules;
- current milestone deliverables and non-goals;
- versioned schemas, hashes, replay, and telemetry contracts.

Treat the architecture specification as authoritative when documents disagree. Do not report a deferred M1 capability as a defect merely because a later architecture seam anticipates it. Report a seam as a current defect only when today's implementation violates a current contract, makes legal input wrong, silently weakens behavior, or forces a contradiction with an accepted design.

Read [references/review-angles.md](references/review-angles.md) for the detailed APGAR finder checklist and verdict rubric.

## 3. Find candidates independently

Always perform a direct read of the core changed files yourself. Builds and tests supplement review; they do not replace source reasoning.

At high effort, run six independent finder passes. When subagents are available, delegate one raw review surface and one angle per finder without sharing suspected findings:

1. contract, promised-behavior, and milestone-scope audit;
2. exactness, conservatism, geometry-boundary, and trust-boundary audit;
3. producer/consumer and cross-file representation trace;
4. arithmetic, determinism, platform, lifetime, and corruption audit;
5. adversarial test, schema/ADR, build-target, and gate audit;
6. hot-path efficiency, reuse, simplification, and architectural-altitude audit.

For focused effort, combine adjacent angles but cover every correctness-critical angle relevant to the changed subsystem. For exhaustive effort, split performance, reuse, simplification, security/robustness, and conventions into separate passes.

Require each finder to return at most six candidates with:

- repo-relative file and precise line;
- one-sentence defect statement;
- concrete input/state/platform and observable failure;
- violated contract or producer/consumer assumption;
- direct code evidence;
- classification as current correctness, contract mismatch, performance, maintainability, or future design note.

Bias finders toward recall. Do not let them silently discard a candidate with a realistic failure scenario; precision belongs in verification. Rank current correctness and contract violations above cleanup.

## 4. Deduplicate and verify

Deduplicate candidates only when location, mechanism, and failure are the same. Independent rediscovery increases confidence but does not replace verification.

Give every remaining candidate one independent verification vote. Grouping several candidates in one verifier task is acceptable, but require a separate verdict and evidence for each:

- `CONFIRMED`: the failure follows from current code or a minimal reproducible probe.
- `PLAUSIBLE`: the failure depends on realistic reachable state not excluded by a proved invariant.
- `REFUTED`: the claim is factually wrong, provably impossible, already handled on the captured target, explicitly accepted by the current contract, or has no observable effect.

The verifier must read the relevant implementation, callers/callees, contract text, and tests. Do not refute merely because current tests pass or because a scenario is rare. Do not confirm a performance claim without identifying the repeated work, asymptotic or fixed cost, and behavior-preserving alternative.

Use narrow probes or Bazel targets where they materially discriminate a candidate. Place probe artifacts in `.agent-scratch/`. Do not edit production files during review.

## 5. Re-check scope and run gates

Before reporting a working-tree review, run:

```sh
python3 .agents/skills/apgar-review/scripts/review_scope.py verify .agent-scratch/<captured-manifest>.json
```

If verification reports drift:

- identify which files or OIDs changed;
- re-read and re-verify every affected candidate;
- restart broad review when the change invalidates the shared contract or representation;
- never merge findings from incompatible snapshots without labeling them.

Run the narrowest relevant Bazel targets when useful. For a requested comprehensive or pre-commit review, verify the repository gates in `AGENTS.md`: `bazel lint`, `bazel build //...`, `bazel test //...`, sanitizer configurations expected by the current project, and `bazel test --lockfile_mode=error //...`. GPU changes additionally require CPU/GPU differential checks and replay coverage. Report every gate not run; green gates do not erase source-level findings.

## 6. Report the review

Lead with actionable findings, ranked by severity. For each retained finding include:

- verdict and category;
- file and tight line reference;
- defect mechanism;
- concrete failure scenario;
- named contract or invariant when applicable;
- smallest root-cause fix direction;
- missing discriminating regression, when relevant.

Then state:

- captured base/head and whether the final drift check passed;
- gates run and gates not run;
- verified-clean high-risk areas actually inspected;
- plausible design notes separated from current defects;
- refuted or below-cap candidates in a compact ledger when they may matter later.

Use `.agent-scratch/REVIEW_FINDINGS.md` for a full cross-agent report when requested or when the final response would otherwise lose useful verified detail. Never present an unexecuted gate as passing, and never say "no findings" without naming the inspected risk surfaces and residual limitations.
