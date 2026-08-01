# APGAR Review Angles

Use this checklist selectively after reading the current architecture, phase ADRs, schemas, and changed code. The current target is authoritative; examples below describe defect classes, not assumed findings.

## 1. Contracts, promises, and scope

- Trace every changed public promise from specification or ADR to schema, header, implementation, tests, and Bazel target.
- Audit deleted or replaced guards and tests by naming the invariant they used to enforce and finding where it is now re-established.
- For new files, adapt removed-behavior review into promised-behavior review: determinism, exactness, validation, fingerprinting, bounds, error taxonomy, provenance, and telemetry claims must be enforced.
- Check that unsupported rules fail closed and are delegated explicitly.
- Check current milestone deliverables and non-goals. Do not demand shove routing, pours, differential pairs, buses, arbitrary angles, tuning, or SI sign-off inside M1 without an architecture revision.
- Distinguish a present contract violation from an attractive later-phase refactor.

## 2. Exactness and conservatism

- For every compiled legal movement, trace the complete swept exact envelope to the CPU oracle and all represented static obstacles.
- Check H/V/45 headings, diagonal corner cutting, cross-tile edges, missing sparse destinations, layer identity, rule bucket, and obstacle-class interactions.
- Exercise closed-boundary equality and plus/minus one DBU perturbations.
- Search for false-free paths caused by endpoint-only checks, lossy conversions, overflow, stale masks, missing halos, incomplete padstacks, or weakened unsupported rules.
- Ensure reconstructed search paths are bounds-checked, connected, and exactly revalidated before becoming candidates.
- Treat search fields and GPU output as untrusted. Verify integrity at a defined boundary, not only on states a particular search happens to visit.

## 3. Association, identity, and lifecycle

- Trace Board IR content hash, compiler profile normalization/fingerprint, compiler/schema version, rule bucket, backend/device class, request identity, seed, and provenance.
- Check whether a validation compares independent expectations or only two fields stored in the same self-consistent artifact.
- Check stale, corrupted, reordered, duplicated, or partially initialized inputs.
- Check constructor argument evaluation order, move-from state, lifetime, aliasing, and publication order.
- For serialization/cache/replay seams, verify major-version rejection, hashes before reuse, and replayable invariant failures.

## 4. Arithmetic, determinism, and portability

- Keep global coordinates signed 64-bit; widen before addition, subtraction, multiplication, squared distance, indexing, and allocation-size arithmetic.
- Check `INT64_MIN/MAX`, negative floor/ceil division, signed/unsigned conversion, sentinel collision, count multiplication, and cost overflow.
- Verify admissible/consistent heuristics for every legal heading and cost subset. A zero fallback may be correct but still a material performance defect.
- Verify stable neighbor order, total queue tie-breaking, canonical sorting/deduplication, deterministic hashing, and lack of iteration-order dependence.
- Look for unsequenced arguments, undefined behavior, transitive includes, compiler/library differences, sanitizer gaps, and nondeterministic containers or atomics.
- Do not infer cross-backend bit identity when the specification only promises semantic equivalence.

## 5. Producer/consumer tracing

- Trace each public struct and function through every producer and consumer.
- Compare compiler output assumptions with router reads: index/layout, units, sort order, sentinels, masks, layers, rule buckets, and initialization on every path.
- Compare real compiler output with test builders and peers; tests must not construct impossible artifacts unless explicitly testing corruption handling.
- Search for duplicated exactness-critical conversions or geometry logic. A test that validates production output with its own drifting copy can self-confirm a bug.
- Check caller preconditions, return/error shape, cancellation, timing, and lifetime changes.
- Check Bazel `srcs`, `hdrs`, `deps`, `data`, sanitizer compatibility, and whether untracked/deleted files make the graph differ from the review description.

## 6. Adversarial validation and tests

- Demand discriminating tests, not assertions that would pass under the old behavior or a broken implementation.
- Cover boundary equality, one-unit changes, overlapping regions, cross-tile diagonals, sparse gaps, stale/corrupt associations, excluded headings, unsupported layers, reconstruction corruption, and deterministic repeats as relevant.
- For compiler conservatism, compare compiled legal edges against the exact CPU oracle on generated microcases. Green hand-picked examples alone do not establish GC-001.
- For GPU primitives, require CPU/GPU differential tests and a replayable artifact for every new invariant class.
- Check tests and docs against the actual current scope; a deleted target can silently remove promised coverage while `bazel test //...` stays green.
- Record which gates ran on the captured OID and which were only inherited from another session.

## 7. Performance, simplification, and delivery

- Profile by reasoning before suggesting changes: name the hot loop, repeated lookup/oracle/allocation/hash, frequency, upper bound, and cheaper equivalent.
- Inspect duplicate symmetric geometry checks, per-neighbor tree searches, map allocation in search state, repeated fingerprinting/sorting, and avoidable copies.
- Confirm an alternative preserves deterministic output and exactness before recommending it.
- Prefer one authoritative conversion/validation mechanism to scattered call-site checks.
- Flag special cases only when a deeper existing abstraction can handle them now; otherwise keep them as explicitly labeled design notes.
- Rank performance below correctness unless it violates a stated memory/latency bound or makes a supported input practically unusable.
- Trace the change to its active epic row or requested outcome. Name the runnable
  behavior, validated evidence, or mechanical decision that closes the slice.
- Count new persistent concepts: schemas, artifact kinds, authorities,
  validators, wrappers, and Bazel targets. Require each independent seam to be
  necessary now, and consolidate self-authenticating parallel representations.
- Compare control-plane surface with delivered domain behavior. Flag a slice
  that can only validate its own scaffolding when the task promises an
  allocator, router, benchmark, or decision observation.
- Keep campaign-only work conditional on readiness, preserve bounded stop
  gates, and allow valid negative results to terminate a hypothesis.
- Match integrity machinery to the declared threat model. Separate reproducible
  trusted-runner provenance from hostile-operator attestation.
- Apply the epic's file/line/concept budget before review. Re-sequence an
  oversized slice instead of treating archived or sunk work as a reason to
  finish it.

## Candidate quality bar

A useful candidate answers all of these:

1. What precise line or interface is wrong?
2. What input, board geometry, state, compiler/platform, or artifact makes it fail?
3. What observable wrong result follows?
4. Which code path or contract proves reachability?
5. What root-cause direction would fix it?
6. What regression would fail before the fix and pass afterward?
7. What current delivery decision does fixing it enable or protect?

Reject vague style preferences, hypothetical future features with no current effect, and findings whose failure scenario contradicts a proved Board IR or schema invariant.

## Severity and reporting

Rank in this order unless impact evidence says otherwise:

1. false-free geometry, wrong-net/layer routing, invalid commit, UB, corruption, or nondeterministic externally visible results;
2. contract/schema mismatch, stale-association acceptance, missing exact validation, unsupported-rule weakening, or reachable hard failure;
3. deterministic but severe performance/memory blow-up on supported inputs;
4. build portability, missing discriminating coverage, error-taxonomy ambiguity, and maintainability hazards;
5. future design notes and cleanups.

Keep `PLAUSIBLE` findings when the state is realistic and not excluded. Put future design notes in a separate section so they cannot be mistaken for merge blockers.
