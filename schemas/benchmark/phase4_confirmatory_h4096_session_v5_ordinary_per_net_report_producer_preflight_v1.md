# Phase 4 Confirmatory H=4096 Session-v5 Ordinary Per-Net Report Producer Preflight v1

This contract defines one compiled-only identity for a future ordinary
Session-v5/H=4096 per-net report producer. It is not a serialized artifact,
does not add a Protocol-v3 substitution, and authorizes no report production or
allocation observation.

The separately reviewed implementation must authenticate this complete
identity and then terminate at the existing
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` barrier before any producer capability
is linked or accessed.

## Canonical compiled identity

`Phase4H4096SessionV5OrdinaryPerNetReportProducerIdentity` has one canonical
value. Exact equality covers all fields below:

- identity schema version: `1`;
- execution authority:
  `Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5`;
- configuration authority:
  `phase4_confirmatory_corpus_v2_h4096_session_v5`;
- superseded configuration authority:
  `phase4_confirmatory_corpus_v2_h4096`;
- Confirmatory Decision Protocol authority:
  `phase4_confirmatory_decision_protocol_v3`;
- Protocol checksum: `4963299999381388941`;
- superseded Protocol authority:
  `phase4_confirmatory_decision_protocol_v2`;
- superseded Protocol checksum: `11520586171987743043`;
- canonical algorithm-budget roster authority:
  `phase4_confirmatory_canonical_algorithm_budget_roster_v4`;
- roster checksum: `12316700735749461907`;
- superseded roster authority:
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3`;
- superseded roster checksum: `18429170436700418962`;
- Representative Corpus version: `2`;
- Representative Corpus checksum: `4182833841936446798`;
- Representative Manifest schema/version: `2`;
- Representative Manifest checksum: `9613362670139358355`;
- Workload-Net Roster Manifest schema/version: `2`;
- Workload-Net Roster Manifest checksum: `14986327048461036142`;
- ordinary Raw authority: `phase4_confirmatory_raw_evidence_v3`;
- superseded ordinary Raw authority: `phase4_confirmatory_raw_evidence_v2`;
- retained ordinary Raw predecessor:
  `phase4_confirmatory_raw_evidence_v1`;
- ordinary report authority:
  `phase4_confirmatory_per_net_report_publication_join_v3`;
- superseded ordinary report authority:
  `phase4_confirmatory_per_net_report_publication_join_v2`;
- retained ordinary report predecessor:
  `phase4_confirmatory_per_net_report_publication_join_v1`;
- case ID: `10200`;
- requested pool size: `8`;
- preparation workers: `4`;
- repetitions: `20`;
- carrier: ordinary;
- Raw Evidence schema version: `1`;
- Raw wire schema version: `1`;
- Per-Net Report Artifact schema version: `1`;
- report Raw wire schema version: `1`;
- report reference repetition: `0`;
- report reference execution order: baseline first;
- report arm count: `2`;
- workload-net count per arm: `64`;
- total report per-net row count: `128`;
- workload-net roster checksum: `718781758134362332`;
- report decision eligibility: `false`;
- Candidate-Allocation Session schema version: `5`;
- Targeted Regeneration Plan schema version: `3`;
- Targeted Regeneration Execution schema version: `6`;
- baseline present step per overuse unit: `1`;
- baseline history step per overuse unit: `4096`;
- candidate present step per overuse unit: `1`;
- candidate history step per overuse unit: `4096`;
- canonical algorithm-budget checksum: `7657176792159702821`;
- paired semantic-budget checksum: `13340538727848385478`;
- telemetry required: `false`;
- telemetry authority: empty;
- superseded telemetry authority: empty;
- telemetry schema version: `0`; and
- telemetry wire schema version: `0`.

The complete Protocol, roster, configuration, Raw, and report predecessor
ancestry remains authoritative. The listed v3-to-v2 transitions do not permit
skipping the retained v1 predecessors or accepting an identity assembled from
unrelated individually valid rows.

Predecessor Session-v4 canonical algorithm-budget checksum
`8230401457668518004` and paired semantic-budget checksum
`12108149041077564710` are explicitly foreign. The two successor budget
checksums are independent and cannot substitute for one another.

## Preflight order and terminal result

The internal builder
`BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity()`
constructs the one canonical value without caller input.

The internal composite
`PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(...)`
uses this order:

1. compare the supplied producer identity field-for-field with the separately
   built canonical value;
2. return
   `P4PAIR-H4096-SESSION-V5-ORDINARY-REPORT-PRODUCER-001` on any mismatch;
3. after complete identity authentication, invoke the existing ordinary
   Session-v5 controller preflight exactly once with the canonical ordinary
   controller identity, canonical ordinary cell, and supplied controller source
   association; and
4. return that result unchanged as the final operation.

For one clean stamped 40-character lowercase runtime commit equal to the
embedded commit, the result is exactly
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001`. That error is terminal. It cannot be
recognized as permission, converted to success, replaced by another barrier,
or followed by any continuation.

Producer-identity validation precedes source validation. Source validation
precedes the activation barrier. The preflight reads no Raw, report, telemetry,
case, fixture, path, descriptor, or environment-selected input.

## Capability-minimal implementation boundary

A separately reviewed implementation may add the private library target
`phase4_h4096_session_v5_per_net_report_producer_preflight` and the private
test-only target
`phase4_h4096_session_v5_per_net_report_producer_preflight_test_support`.
The main target may depend only on the existing capability-minimal
`phase4_h4096_session_v5_execution_preflight` and the minimum standard-library
support needed for immutable value comparison. The test-support target must
compile the identical producer-preflight source and header list without
semantic defines. Its only code-dependency substitution is the existing
sanitizer-safe `phase4_h4096_session_v5_execution_preflight_test_support`;
`testonly`, sanitizer compatibility, and compile/link options may differ only
as required to expose the same code to ASan and UBSan.

The only binaries authorized by this contract are fixtureless `testonly`
preflights:

- direct test
  `phase4_h4096_session_v5_per_net_report_producer_preflight_test`;
- `phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner`;
  and
- `phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner`.

Non-C++ Starlark analysis rules and Python tests may be added only to inspect
the five named C++ targets and derived audit artifacts. They must add no APGAR
production dependency or execution capability.

The two process runners' interface is exactly one required
`--runtime_commit=<40 lowercase hex>` option. They accept no positional,
carrier, case, pool, schema, authority, budget, Raw, report, telemetry,
fixture, path, output, request/response descriptor, child, activation, or
testing option. Direct tests, not process arguments, exercise mutated identity
values. Every process invocation exits with exact status `2`, writes nothing
to stdout, and writes exactly one bounded invariant line plus one newline to
stderr for the activation barrier, source rejection, or argument rejection.
Printing the expected invariant while returning zero or any status other than
`2` is a failure.

Each original direct- or process-test binary provider must expose exactly one
target-owned executable in `DefaultInfo.files`, default runfiles, and data
runfiles. No additional file, data, workspace symlink, root symlink, empty
filename, or child executable is permitted. Analysis-time negative contracts
must make the unexpected-entry and runfiles-link classifiers live.
In particular, the direct `cc_test` must use static-link semantics so hermetic
C++ runtime libraries do not widen either runfiles set.

The implementation target's own compiled objects contain only the immutable
producer identity and composite preflight, and its only APGAR code dependency
is the existing capability-minimal Session-v5 execution preflight.

Both production-shaped process-runner links must retain the new producer
preflight and required shared Session-v5 authority, preimage, budget, and
activation symbols while excluding all:

- report artifact validators and builders;
- ordinary or same-run report runners;
- diagnostic executors;
- representative-case or fixture builders;
- candidate-pool preparers;
- workers, worker launchers, finalizers, and self-execution paths;
- allocators and route-query execution;
- Raw, report, or telemetry serializers and wire/file I/O;
- runfiles or fixture resolution;
- artifact installers and durable-output paths; and
- acquisition-capable child executables.

The fixtureless direct `cc_test` is the sole exception to that symbol
allowlist. Its direct C++ dependencies are exactly the new producer-preflight
test support and GoogleTest. The new support's only code dependency is the
existing `phase4_h4096_session_v5_execution_preflight_test_support`. An
analysis-time transitive dependency allowlist must freeze that exact graph and
reject every additional dependency. ASan or UBSan registration may retain
execution-capable symbols from the allowlisted support graph in the direct-test
ELF. The instrumented direct test is therefore structural sanitizer evidence,
not a capability-minimal producer boundary, and cannot satisfy link closure or
activation.

The direct test remains statically linked and fixtureless, with exactly its
executable in both runfiles sets and no path, descriptor, report, Raw,
telemetry, output, environment-selected input, or child executable. The two
process runners have no sanitizer exception. During ASan and UBSan invocations
their process tests and symbol audits must consume explicit unsanitized
transition artifacts and enforce the full exclusion list above.

The production runner
`phase4_confirmatory_h4096_session_v5_per_net_report_runner` remains absent.
No Raw, report, telemetry, or other evidence artifact is emitted.

## Required review evidence

Implementation review must prove:

- the canonical identity reaches exactly the shared activation barrier;
- each producer-identity field independently rejects before source or
  activation;
- ordinary/same-run, v1/v2/v3, configuration, Session, Plan, Execution,
  carrier, Raw/report schema, roster, budget, telemetry-presence, and authority
  substitutions cross-reject;
- clean source reaches the barrier while dirty, unstamped, malformed,
  uppercase, forced-unpublishable, and cross-commit source fails earlier;
- the canonical process exits exactly `2`, stdout is empty, stderr is exactly
  the activation invariant plus one newline, and no output artifact is
  created;
- every process-exposed source or argument rejection exits exactly `2` with
  empty stdout and exactly its one bounded invariant line plus one newline on
  stderr;
- unknown, abbreviated, duplicate, path, fixture, output, Raw, report,
  telemetry, descriptor, activation, and testing arguments reject before
  external state access;
- poisoned environment variables, FIFO paths, and inherited descriptors remain
  untouched;
- every original provider satisfies its exact runfiles contract, both process
  links satisfy the strict symbol allowlist, and the direct test satisfies its
  exact transitive dependency allowlist;
- ASan and UBSan instrument the direct identity/composite test while process
  and link tests consume explicit unsanitized runner artifacts; and
- historical Session-v4 behavior and the absence of the production runner are
  unchanged.

This preflight is not an operational publication or a complete-chain member.
It does not activate Raw, authorize case `10200`, establish calibration
non-regression, open the same-run producer, or support any Phase 4 or M1
completion claim.
