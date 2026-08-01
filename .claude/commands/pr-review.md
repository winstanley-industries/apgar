---
allowed-tools: Agent, Read, Grep, Glob, Bash(gh pr view:*), Bash(gh pr diff:*), Bash(gh pr review:*), mcp__github_inline_comment__create_inline_comment
description: Review an APGAR pull request across architecture and delivery contracts, correctness, C++/CUDA quality and scalability, and test/evidence coverage, then submit one formal verdict
---

Review the given pull request in one complete architecture-first pass. Fan out to
APGAR's four review agents, validate their findings, and post one consolidated,
high-signal review.

**Arguments: `$ARGUMENTS`** — parse the first token as the PR URL or number
(`<PR>`). `--comment` enables GitHub writes. CI also supplies
`--metadata <path>` and `--diff <path>` for one immutable context snapshot. When
both paths exist, read them directly and do not re-fetch PR metadata or the diff.
Without `--comment`, print findings only and make no GitHub writes.

Follow these steps precisely:

1. **Skip gate.** Read the supplied metadata, or make one standalone
   `gh pr view <PR>` call. Stop only when the PR is closed, is a draft that was
   not explicitly requested for review, or is both mechanically trivial and
   behavior-neutral. Never skip changes to public APIs, Board IR, exact geometry,
   compiled fields, routing, GPU trust boundaries, candidates/allocation,
   schemas/replay/evidence, determinism, error handling, workflow security, or
   build/test authority.

2. **Gather context once.** Use the supplied metadata and diff. For a local dry
   run without prepared paths, make exactly one standalone `gh pr view <PR>` and
   one standalone `gh pr diff <PR>` call. Give all reviewers the same snapshot;
   they must not re-fetch the PR. The diff is authoritative.

3. **Complete all four review dimensions in the foreground.** CI disables
   background tasks. Dispatch these agents with the shared metadata and diff
   paths and ask for only high-confidence findings with file:line, failure case,
   governing contract, concrete fix, confidence, and severity:

   - **spec-consistency-reviewer** — architecture, epic/ADR/schema authority,
     milestone scope, delivery value, exactness, determinism, and complexity;
   - **correctness-reviewer** — concrete geometry, validation, provenance,
     association, arithmetic, routing, candidate, replay, and error defects;
   - **cpp-cuda-quality-reviewer** — C++/CUDA safety, lifetime, API/resource
     design, hermetic build boundaries, and material scalability problems; and
   - **test-coverage-reviewer** — changed behavior missing discriminating exact,
     property, CPU/GPU differential, replay, deterministic, or integration proof.

   Do not consolidate while any dimension remains incomplete. If an agent cannot
   run or returns nothing usable, perform that dimension yourself from the shared
   snapshot.

4. **Validate directly.** Check every candidate against the shared diff and the
   relevant repository contract, producer, consumer, and tests. It must be real,
   reachable, in scope, and anchored to changed code. Drop duplicates,
   medium-or-lower confidence claims, speculative future work, and anything that
   depends on an unverified assumption. A green gate does not refute a source-level
   defect. Do not spawn a second validation wave.

5. **Classify severity.** A finding is **blocking** when it is a correctness or
   security defect; violates exactness, authority, provenance, determinism, or a
   written contract; silently weakens semantics; leaves correctness-critical
   changed behavior without required proof; or causes a practical supported-input
   resource blowup. API polish, simplification, traceability, and non-material
   performance notes are **advisory**. Advisory-only reviews approve.

6. **Consolidate and report.** Keep at most one comment per unique issue. Include
   the spec reviewer's `DELIVERY-CLEAR` or `RESEQUENCE` verdict. Prepare a short
   summary split into Must fix and Advisory, or state that no high-signal issues
   survived across all four dimensions.

7. **Write only when enabled.** Without `--comment`, stop after printing the
   summary. With `--comment`:

   - Post one inline comment per finding using
     `mcp__github_inline_comment__create_inline_comment` with `confirmed: true`.
     Prefix blockers with `[spec]`, `[bug]`, `[cpp]`, `[perf]`, or `[tests]`;
     prefix advisory findings with `[nit]`. Cite the governing rule for spec
     findings and include a concrete fix.
   - Submit exactly one formal verdict:
     `gh pr review <PR> --request-changes --body "<summary>"` when a blocking
     finding survives, otherwise
     `gh pr review <PR> --approve --body "<summary>"`.

   The body is a concise `## Claude review` section with the verdict, delivery
   assessment, Must fix list (or `none`), and advisory count. Never finish with a
   COMMENTED-only review. Zero findings still require one approving formal
   verdict.

8. **Completion check.** Before finishing, verify that all four dimensions were
   completed, every posted finding was directly validated and deduplicated, every
   inline comment succeeded, and exactly one formal verdict command succeeded.
   Continue or fail explicitly if any item is incomplete.

False positives erode trust. When a finding cannot be justified from the diff and
a concrete APGAR contract or failure scenario, drop it.
