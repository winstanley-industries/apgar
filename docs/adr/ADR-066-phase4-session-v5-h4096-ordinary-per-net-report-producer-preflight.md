# ADR-066: Phase 4 Session-v5 H=4096 Ordinary Per-Net Report Producer Preflight

**Status:** Accepted as an inactive producer-preflight contract; report
production, execution, and acquisition remain closed
**Date:** July 30, 2026
**Applies to:** Development-only Session-v5/H=4096 ordinary report capability
authentication before the complete consuming-publication chain exists

## Context

ADR-063 freezes the inactive Session-v5/H=4096 Raw boundary and the shared
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` closure. ADR-064 freezes the ordinary
Session-v5 Raw/report publication join, and ADR-065 freezes the larger
same-run Raw/telemetry/report join. Their strict offline validators now
authenticate synthetic artifacts without producing or observing an allocation
outcome.

The next smallest acquisition-free step is not a report runner. It is a
compiled boundary that authenticates the complete identity a future ordinary
report producer must present for the exact Protocol-v3, roster-v4, Session-v5
authority and then stops at the existing shared activation barrier. Reaching
the Raw controller preflight alone cannot authenticate that future boundary:
its identity intentionally contains no report authority, report payload,
roster cardinality, or telemetry-absence contract.

The historical H=4096 ordinary report runner, diagnostic executor, report
builder, serializer, and their local report-authority selection remain
Protocol-v2/roster-v3/Session-v4 capabilities. Case `10200` is generated, but
executing it still observes allocator behavior and is acquisition. Those
surfaces therefore cannot be reused, widened, linked dormant behind the
barrier, or treated as safe merely because they require no board-fixture
runfile.

This contract freezes a new immutable ordinary report-producer identity and
the evidence required for a later preflight-only implementation. It does not
add that implementation, a production runner, a report builder, or any
execution capability.

## Decision

- Freeze the compiled-only identity
  `Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity`. It is selected by
  a separately named compiled boundary, never by a caller carrier flag, case
  ID, source commit, environment variable, matching checksum, artifact field,
  or runtime option.
- The identity is not serialized evidence, does not gain an artifact checksum,
  and is not an eleventh Confirmatory Decision Protocol v3 substitution.
  Protocol v3 and canonical roster v4 JSON remain byte-for-byte unchanged.
- Bind the complete authority transition:
  - execution authority
    `Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5`;
  - Confirmatory Decision Protocol v3 checksum `4963299999381388941`,
    directly superseding Protocol v2 checksum `11520586171987743043`;
  - canonical algorithm-budget roster v4 checksum
    `12316700735749461907`, directly superseding roster v3 checksum
    `18429170436700418962`;
  - configuration authority
    `phase4_confirmatory_corpus_v2_h4096_session_v5`, directly superseding
    `phase4_confirmatory_corpus_v2_h4096`;
  - ordinary Raw authority `phase4_confirmatory_raw_evidence_v3`, directly
    superseding `phase4_confirmatory_raw_evidence_v2`, whose direct predecessor
    remains `phase4_confirmatory_raw_evidence_v1`;
  - ordinary report authority
    `phase4_confirmatory_per_net_report_publication_join_v3`, directly
    superseding
    `phase4_confirmatory_per_net_report_publication_join_v2`, whose direct
    predecessor remains
    `phase4_confirmatory_per_net_report_publication_join_v1`;
  - Representative Corpus v2 checksum `4182833841936446798`,
    Representative Manifest v2 checksum `9613362670139358355`, and
    Workload-Net Roster Manifest v2 checksum `14986327048461036142`; and
  - exact ordinary calibration cell `(10200,8)`, four preparation workers, and
    20 repetitions.
- Bind the complete canonical configuration: Candidate-Allocation Session v5,
  Targeted Regeneration Plan v3, Targeted Regeneration Execution v6, and
  equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`.
- Require canonical algorithm-budget checksum `7657176792159702821` and
  independently reconstructed paired semantic-budget checksum
  `13340538727848385478`. The identity and preflight must positively reject
  predecessor Session-v4 values `8230401457668518004` and
  `12108149041077564710`, either successor checksum substituted for the other,
  and every reassembled identity carrying those values.
- Bind ordinary Raw Evidence schema 1 over Raw Wire 1 and Per-Net Report
  Artifact schema 1 with `raw_wire_schema_version=1`. The future report binds
  the repetition-zero baseline-first pair through two ordered arms, 64 ordered
  workload `EntityRef` values per arm, 128 total per-net rows, workload-net
  roster checksum `718781758134362332`, and literal
  `decision_eligible=false`.
- Encode the ordinary carrier's negative telemetry contract explicitly:
  `telemetry_required=false`, empty current and predecessor telemetry
  authorities, telemetry schema version zero, and telemetry wire version zero.
  A same-run carrier, nonempty telemetry identity, sidecar path, telemetry
  schema, or telemetry authority cannot be ignored or normalized into the
  ordinary identity.
- Reserve the internal builder
  `BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity()`
  and composite preflight
  `PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(...)`.
  Neither API accepts a carrier selector. The builder returns the one canonical
  ordinary identity. The composite accepts that identity and a controller
  source association only so direct tests can prove field-by-field rejection.
- Fix the composite validation order:
  1. compare every report-producer identity field to the separately built
     canonical ordinary identity;
  2. on any mismatch, return exactly
     `P4PAIR-H4096-SESSION-V5-ORDINARY-REPORT-PRODUCER-001`;
  3. only after complete report identity authentication, delegate exactly once
     to the existing ordinary Session-v5 controller preflight, which
     reconstructs the complete 20-repetition specification roster, both arms,
     Protocol/roster/configuration/Raw authority, both budgets, and source
     association; and
  4. return its result unchanged as the composite's final operation.
- Canonical clean input must terminate at the existing immutable
  `P4PAIR-H4096-SESSION-V5-ACTIVATION-001` error. No code may recognize,
  translate, suppress, catch as success, or continue after that error. No new
  report-specific barrier, complete-chain boolean, environment override,
  testing bypass, or alternate success return is permitted.
- Production source identity remains clean and stamped, with a 40-character
  lowercase runtime commit exactly equal to the embedded commit. Report
  identity failure precedes source validation; source failure precedes the
  shared activation barrier. Source policy does not authorize Raw or report
  input access.
- A separately reviewed implementation may add only the private library target
  `phase4_h4096_session_v5_per_net_report_producer_preflight`, its private
  sanitizer-safe direct-test support target
  `phase4_h4096_session_v5_per_net_report_producer_preflight_test_support`, the
  fixtureless direct test
  `phase4_h4096_session_v5_per_net_report_producer_preflight_test`, and
  fixtureless process-test binaries
  `phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner`
  and
  `phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner`.
  Non-C++ Starlark analysis rules and Python tests may be added only to audit
  those five C++ targets and their derived audit artifacts. They may add no
  APGAR production dependency or execution capability.
  The reserved production runner
  `phase4_confirmatory_h4096_session_v5_per_net_report_runner` remains absent.
- The two test-only process surfaces accept exactly one required
  `--runtime_commit=<40 lowercase hex>` option and no positional arguments.
  They accept no carrier, case, pool, budget, authority, schema, report, Raw,
  telemetry, fixture, output, path, descriptor, child, activation, or testing
  option. Identity mutations belong in direct tests, not in a caller-selectable
  process mode. Every invocation exits with exact status `2`, writes empty
  stdout, and writes exactly one bounded invariant line plus one newline to
  stderr; this applies to the clean activation barrier, unpublishable or
  mismatched source, and every argument rejection. A printed invariant with a
  zero or other exit status is not valid evidence.
- The implementation target's own objects may define only the immutable
  producer identity and composite preflight; its sole APGAR code dependency is
  the existing capability-minimal Session-v5 execution preflight. Both
  production-shaped process runners must exclude report builders, report
  validators, report artifact libraries, diagnostics, case builders,
  representative-case construction, preparers, workers, allocators,
  finalizers, Raw/report/telemetry serializers, wire or file I/O, fixtures,
  runfiles helpers, artifact installers, durable-output paths, and
  acquisition-capable child executables. A linked but unreachable capability
  in either process runner violates this contract.
- The fixtureless direct `cc_test` is the sole narrow exception to that symbol
  closure. It may depend directly only on the new sanitizer-safe producer
  preflight test support and GoogleTest. That support may depend only on the
  exact existing
  `phase4_h4096_session_v5_execution_preflight_test_support` graph documented
  above. Sanitizer registration may retain execution-capable symbols from
  that graph in the direct-test ELF. An analysis-time transitive dependency
  allowlist must reject any wider graph, and the test must remain statically
  linked with no extra runfile, fixture, path, descriptor, report, Raw,
  telemetry, output, environment-selected input, or child executable. The
  instrumented direct test is structural ASan/UBSan evidence only; it is not a
  capability-minimal producer boundary and cannot count as closure or
  activation evidence.
- Each original direct- or process-test binary provider, before any audit
  repackaging, must expose exactly one target-owned executable in
  `DefaultInfo.files`, default runfiles, and data runfiles, with no other file,
  data, workspace symlink, root symlink, empty filename, or child executable.
  Analysis-time negative contracts must prove unexpected-entry and all
  runfiles-link classifiers are live. Final link inspection of both process
  runners must require the new producer preflight and shared Session-v5
  authority/preimage symbols while rejecting every forbidden producer,
  execution, allocation, serialization, and I/O symbol class. Under ASan and
  UBSan those audits and the process tests must consume explicitly transitioned
  unsanitized runner artifacts, never the instrumented direct test. The direct
  test receives the exact dependency-graph audit above rather than a false
  capability-minimal symbol claim.
- Direct and process tests for the implementation must:
  - prove the exact canonical identity reaches only the shared activation
    barrier, and that its process runner exits exactly `2` with empty stdout,
    exactly the activation invariant plus newline on stderr, and no output
    artifact;
  - mutate every identity field independently and prove the producer-identity
    error occurs before source validation or activation;
  - cross-reject ordinary/same-run, v1/v2/v3, Raw/report, carrier, payload,
    schema, budget, roster, telemetry-presence, configuration, Session, Plan,
    and Execution substitutions;
  - prove clean source reaches the activation barrier while dirty, unstamped,
    malformed, uppercase, cross-commit, and forced-unpublishable source fails
    earlier, with every process-exposed source rejection exiting exactly `2`;
  - reject unknown, abbreviated, duplicate, path, fixture, output, report,
    Raw, telemetry, descriptor, activation, and testing arguments before
    touching any external state, always with exact exit status `2`;
  - poison environment variables, fixture FIFOs, output paths, and inherited
    request/response descriptors and prove they remain untouched; and
  - preserve historical Protocol-v2/roster-v3/Session-v4 behavior and keep the
    future production runner absent.
- ASan and UBSan must instrument and run the direct identity/composite test
  through the named test-support seam. This proves memory and undefined
  behavior properties of the preflight logic, not capability closure. The
  process tests and link audits must still run in sanitizer invocations
  against their explicitly transitioned unsanitized, capability-minimal
  runner artifacts.
- Keep the same-run producer preflight for a separate contract and review. It
  must add the atomic Raw Wire 2/telemetry/report trust surface and cannot
  widen or parameterize this ordinary boundary.
- Keep both operational publications, exact-small snapshot/Oracle, every other
  development cell, heldout, imported, fixed-query, stress, aggregation,
  matrix, decision, campaign, and acquisition paths closed. This preflight is
  not an operational publication, complete-chain member, development
  observation, Phase 4 claim, or M1 claim.

## Consequences

The repository has an exact, reviewable contract for authenticating the
compiled identity boundary that a future ordinary report producer must cross
for the Session-v5 authority before the shared activation closure is
encountered. The later preflight implementation can be tested without adding
that producer, reading a Raw artifact, producing a report, constructing case
`10200`, or observing allocator behavior.

This contract does not add the preflight implementation, a report producer, a
production runner, a publication artifact, or any acquisition capability. It
does not satisfy the complete consuming-publication chain or authorize either
development cell to run.

## Rejected alternatives

- **Implement ordinary and same-run producer preflights together.** The
  ordinary identity has a smaller two-artifact surface and no telemetry
  authority. Same-run must receive its own contract and adversarial review.
- **Reuse the Raw controller identity.** That identity intentionally proves no
  report authority or payload contract.
- **Reuse or hide the historical H=4096 report runner behind the barrier.**
  Its linked diagnostic, preparer, allocator, report-builder, serializer, and
  output capabilities remain Session-v4 acquisition surfaces even if control
  flow appears to stop earlier.
- **Treat generated case `10200` as acquisition-free.** Running diagnostics on
  a fixtureless generated case still observes allocator behavior.
- **Add a Protocol-v3 producer substitution.** The producer identity is a
  compiled boundary over the already reserved Raw and report authorities, not
  a new evidence artifact authority.
- **Accept the activation error and continue.** The barrier is the required
  terminal condition until a future activation contract proves the complete
  chain in one clean stamped commit.
- **Infer ordinary scope from case ID, Raw wire, absent sidecar, checksum, or
  source commit.** Scope is selected out of band by one separately named
  compiled identity.
- **Count validators or preflights as the consuming-publication chain.** They
  authenticate boundaries but do not provide both operational publications or
  the exact-small snapshot/Oracle successor.
