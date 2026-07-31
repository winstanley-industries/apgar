# Phase 4 Confirmatory H=4096 Session-v5 Same-Run Per-Net Report Producer Preflight v1

This contract defines one compiled-only identity for a future same-run
Session-v5/H=4096 per-net report producer. It is not a serialized artifact,
does not add a Protocol-v3 substitution, and authorizes no Raw, telemetry, or
report production and no allocation observation.

The separately reviewed implementation must authenticate the complete
61-field identity and then terminate at the existing
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` barrier before any producer,
validator, artifact, diagnostic-execution, or acquisition capability is linked
or accessed.

## Canonical compiled identity

`Phase4H4096SessionV5SameRunPerNetReportProducerIdentity` has one canonical
value. Exact equality covers all 61 named fields below:

1. `identity_schema_version`: `1`;
2. `execution_authority`:
   `Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5`;
3. `configuration_authority`:
   `phase4_confirmatory_corpus_v2_h4096_session_v5`;
4. `superseded_configuration_authority`:
   `phase4_confirmatory_corpus_v2_h4096`;
5. `protocol_authority`: `phase4_confirmatory_decision_protocol_v3`;
6. `protocol_checksum`: `4963299999381388941`;
7. `superseded_protocol_authority`:
   `phase4_confirmatory_decision_protocol_v2`;
8. `superseded_protocol_checksum`: `11520586171987743043`;
9. `budget_roster_authority`:
   `phase4_confirmatory_canonical_algorithm_budget_roster_v4`;
10. `budget_roster_checksum`: `12316700735749461907`;
11. `superseded_budget_roster_authority`:
    `phase4_confirmatory_canonical_algorithm_budget_roster_v3`;
12. `superseded_budget_roster_checksum`: `18429170436700418962`;
13. `corpus_version`: `2`;
14. `corpus_checksum`: `4182833841936446798`;
15. `representative_manifest_schema_version`: `2`;
16. `representative_manifest_checksum`: `9613362670139358355`;
17. `workload_roster_manifest_schema_version`: `2`;
18. `workload_roster_manifest_checksum`: `14986327048461036142`;
19. `raw_authority`: `phase4_confirmatory_same_run_raw_evidence_v3`;
20. `superseded_raw_authority`:
    `phase4_confirmatory_same_run_raw_evidence_v2`;
21. `retained_raw_predecessor_authority`:
    `phase4_confirmatory_same_run_raw_evidence_v1`;
22. `telemetry_required`: `true`;
23. `telemetry_authority`:
    `phase4_confirmatory_same_run_decision_telemetry_v3`;
24. `superseded_telemetry_authority`:
    `phase4_confirmatory_same_run_decision_telemetry_v2`;
25. `retained_telemetry_predecessor_authority`:
    `phase4_confirmatory_same_run_decision_telemetry_v1`;
26. `report_authority`:
    `phase4_confirmatory_same_run_per_net_report_publication_join_v3`;
27. `superseded_report_authority`:
    `phase4_confirmatory_same_run_per_net_report_publication_join_v2`;
28. `retained_report_predecessor_authority`:
    `phase4_confirmatory_same_run_per_net_report_publication_join_v1`;
29. `case_id`: `10100`;
30. `requested_pool_size`: `4`;
31. `preparation_worker_count`: `4`;
32. `repetitions`: `20`;
33. `carrier`: `Phase4H4096SessionV5Carrier::kSameRun`;
34. `raw_schema_version`: `2`;
35. `raw_wire_schema_version`: `2`;
36. `raw_pair_attempt_count`: `20`;
37. `raw_arm_attempt_count`: `40`;
38. `telemetry_schema_version`: `1`;
39. `telemetry_wire_schema_version`: `2`;
40. `telemetry_pair_capture_count`: `20`;
41. `telemetry_arm_capture_count`: `40`;
42. `telemetry_workload_net_count_per_arm`: `6`;
43. `telemetry_total_per_net_row_count`: `240`;
44. `report_schema_version`: `1`;
45. `report_raw_wire_schema_version`: `2`;
46. `report_reference_repetition`: `0`;
47. `report_reference_execution_order`: `Phase4TrialOrder::kBaselineFirst`;
48. `report_arm_count`: `2`;
49. `workload_net_count_per_arm`: `6`;
50. `total_report_per_net_row_count`: `12`;
51. `workload_net_roster_checksum`: `12521697377381992336`;
52. `report_decision_eligible`: `false`;
53. `candidate_session_schema_version`: `5`;
54. `targeted_regeneration_plan_schema_version`: `3`;
55. `targeted_regeneration_execution_schema_version`: `6`;
56. `baseline_present_step_per_overuse_unit`: `1`;
57. `baseline_history_step_per_overuse_unit`: `4096`;
58. `candidate_present_step_per_overuse_unit`: `1`;
59. `candidate_history_step_per_overuse_unit`: `4096`;
60. `canonical_algorithm_budget_checksum`: `13645569624513409309`; and
61. `paired_semantic_budget_checksum`: `12493092620111240227`.

The identity is a public aggregate with exactly those 61 non-static data
members in that order. Execution authority, carrier, and report order use
`Phase4TrialExecutionAuthority`, `Phase4H4096SessionV5Carrier`, and
`Phase4TrialOrder`, respectively. Authority/configuration members use
`std::string_view`; the two decision/required members use `bool`; all schema,
version, case, pool, worker, repetition, attempt, capture, arm, net, and row
counts use `std::uint32_t`; and all checksums and present/history step values
use `std::uint64_t`.

There is no base, virtual member, static state, anonymous union, bit-field,
`[[no_unique_address]]` member, user-declared constructor/destructor/
conversion, or extra method. Numeric and boolean defaults are zero/false,
authority `std::string_view` values default empty, and the explicit enum
defaults are the listed execution authority, `kSameRun` carrier, and
zero-valued `kBaselineFirst` report order. The sole comparison declaration is the
non-template hidden-friend `operator==` over two const identity references,
explicitly `= default`; no custom or partial equality is permitted. The
builder explicitly initializes all 61 members rather than relying on an
omitted member's default.

The complete Protocol, roster, configuration, Raw, telemetry, and report
predecessor ancestry remains authoritative. The listed v3-to-v2 transitions
do not permit skipping the retained v1 artifact authorities or accepting an
identity assembled from unrelated individually valid rows.

The Raw, telemetry, and report cardinalities are separate fields even when
their canonical values are arithmetically related. The producer preflight
must reject each field independently; it must not normalize one cardinality
from another before comparison.

Predecessor Session-v4 canonical algorithm-budget checksum
`8829615204625848656` and paired semantic-budget checksum
`5851813264366095594` are explicitly foreign. Ordinary Session-v5 canonical
checksum `7657176792159702821`, ordinary paired checksum
`13340538727848385478`, ordinary roster checksum `718781758134362332`, Raw
Wire 1, an absent telemetry companion, and `(10200,8)` are also foreign. The
two same-run successor budget checksums are independent and cannot substitute
for one another.

Same-Run Raw Evidence schema 2, Raw Wire 2, Same-Run Decision Telemetry schema
1, Telemetry Wire 2, and Per-Net Report Artifact schema 1 remain unchanged.
The producer identity does not revise their canonical key order, hash domain,
source envelope, arm semantics, column partition, or checksum contract.

## Preflight order and terminal result

The internal builder
`BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity()`
constructs the one canonical value without caller input.

The internal composite
`PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer(...)`
accepts only the supplied producer identity and one
`Phase4H4096SessionV5ControllerSourceAssociation`. It uses this order:

1. compare the supplied identity field-for-field with the separately built
   canonical 61-field value;
2. return a `Phase4PairedTrialErrorCode::kMeasurementAssociation` error with
   exact invariant
   `P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-001` on any mismatch;
3. only after complete producer-identity authentication, invoke the existing
   Session-v5 controller preflight exactly once with:
   - `BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(`
     `Phase4H4096SessionV5Carrier::kSameRun,`
     `Phase4H4096SessionV5Endpoint::kController)`;
   - `BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(`
     `Phase4H4096SessionV5Carrier::kSameRun)`; and
   - the supplied controller source association; and
4. return the shared controller result unchanged as the composite's final
   operation.

The composite performs no work after the shared call. It does not recognize
an invariant, inspect an error, translate a result, set a complete-chain flag,
or continue to another capability.

For one clean stamped 40-character lowercase runtime commit exactly equal to
the embedded commit, the result is exactly
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001`. That error is terminal and cannot be
converted to success or replaced by a same-run report-specific barrier.

Producer-identity validation precedes the shared controller and therefore
precedes source validation. The shared controller reconstructs the complete
20-repetition, alternating-order, both-arm Session-v5 specification roster
before validating source. Source validation precedes the activation barrier.

The preflight reads no Raw, telemetry, report, case, fixture, path,
descriptor, or environment-selected input. It does not call or link
`phase4_confirmatory_h4096_session_v5_same_run_per_net_report_validator`.
That validator's Raw-before-sidecar-before-report order remains authoritative
only for offline artifact authentication.

## Capability-minimal implementation boundary

A separately reviewed implementation may add only these five C++ targets:

- private library
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight`;
- private test-only library
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support`;
- direct `cc_test`
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test`;
- process runner
  `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner`;
  and
- forced-unpublishable process runner
  `phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner`.

All five targets are private or `testonly` as appropriate. Both runners are
fixtureless and test-only. Non-C++ Starlark analysis rules and Python tests
may inspect only those targets and their derived audit artifacts. They must
add no APGAR production dependency or execution capability. The pinned
compiler front end, semantic checker, negative fixtures, and every other
audit-tool input are test-only inputs of the audit targets and must be absent
from all five C++ targets' dependencies, providers, runfiles, compile inputs,
and link inputs.

The main library may depend only on the existing capability-minimal
`phase4_h4096_session_v5_execution_preflight` and the minimum standard-library
support needed for immutable value comparison. Its own object defines only the
immutable identity builder, composite preflight, and the identity carrier's
schema-frozen hidden-friend equality when codegen emits it. It uses independent
function/data sections, and both process binaries use static linking with
`-Wl,--gc-sections`.

The test-support library compiles the identical source and header list without
semantic defines. Its only code-dependency substitution is the exact existing
`phase4_h4096_session_v5_execution_preflight_test_support`. The forced-source
define
`APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING`
may affect only the separately named forced process-runner source boundary. It
must not alter the main or test-support library semantics.

Analysis freezes the exact direct and transitive C++ dependency labels,
`CcInfo` linker-input owners/artifacts, and resolved `deps`,
`implementation_deps`, `dynamic_deps`, `malloc`, `link_extra_lib`,
`additional_linker_inputs`, and `alwayslink` effects for the main library,
test-support library, direct test, and both runners. The main library's only
direct C++ dependency is the production execution preflight; each runner's
only direct C++ dependency is the main library; the test-support and direct
test have only the substitutions stated here. Only the exact frozen transitive
APGAR closures and checksum-pinned C++ toolchain implicit libraries are
permitted. A foreign repository library, custom allocator, extra object/
archive, or always-linked constructor is forbidden even if all target-owned
source ASTs remain unchanged. Final ELF startup/init/fini provenance resolves
only to those frozen linker inputs. In the direct test, the exact
configuration-specific startup/teardown inventory contains only pinned C++
runtime and sanitizer entries, the direct-test translation unit's exact
GoogleTest registration initializers, and these three framework-owned
initializers from the pinned `@googletest//:gtest_main` closure:
`_GLOBAL__sub_I_gmock.cc`, `_GLOBAL__sub_I_gtest.cc`, and
`_GLOBAL__sub_I_gtest_death_test.cc`. Their source-object provenance and
multiplicity are exact in each configuration; no other GoogleTest framework
startup or teardown is permitted. No APGAR production or test-support owner
contributes an initializer, destructor, init/fini entry, or pre-/post-test
execution edge, even when that owner is already on the dependency allowlist.

The direct `cc_test` has empty `args`, `env_inherit`, and `tags`; empty `env` in
normal and ASan; exact UBSan `env`
`{"UBSAN_OPTIONS": "halt_on_error=1"}`; exact size `small`; and default
non-flaky, non-local, unsharded execution attributes. Its compatibility
constraints resolve runnable in normal, ASan, and UBSan. Its source contributes
no `main` declaration or definition, and its linked `main` resolves to pinned
`@googletest//:gtest_main`. Every registered test name is nonempty and contains
no `DISABLED_` suite or case component. The semantic audit freezes the exact
assertion inventory, enumerates a nonempty exact registration set, and proves
an unfiltered invocation of the binary ran that complete set and its assertions
under normal, ASan, and UBSan configurations. Aside from compiler-added
sanitizer instrumentation, the normalized registered test bodies, control-flow
graphs, and required builder/composite/assertion call inventories are identical
across all three configurations. In each resolved configuration, the audited
control-flow graph proves that, across the complete unfiltered registration
set, the required builder/composite call sites and all 61 independent
mutation/assertion paths are reachable and unavoidable before every successful
direct-test binary exit. An early return, skip, false/dead guard, empty
parameterization, exception, sanitizer-conditioned omission, or other path that
can report success without executing them is forbidden.

UBSan evidence is fail-live. The new test-support and direct-test compilations
contain exact `-fno-sanitize-recover=all` in the UBSan configuration and no
recovery-enabling flag. The sole permitted suppression-bearing compile flag is
exact
`-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt`, injected by
the checksum-pinned LLVM toolchain. The referenced file has SHA-256
`46f903f898f41e3424c773b9de9efc659cc2de3a00cfb214c840ac96e2c22bf7`
and, after its comment, exactly these two entries in this order:
`src:external/llvm+/3rd_party/libc/glibc/csu/elf-init-2.31.c` and
`src:external/llvm++musl+musl_libc/src/env/__libc_start_main.c`. The semantic
audit parses that exact file using Clang ignorelist semantics, rejects path,
digest, content, order, or multiplicity drift, and proves neither entry matches
any target-owned or transitive APGAR source, the direct-test source, or the
live-probe source. Any other ignore/suppression flag, file, entry, or effective
audited-source match is forbidden. The exact target-level
`UBSAN_OPTIONS=halt_on_error=1` applies to the complete linked
execution-preflight support graph. No caller-selected or inherited sanitizer
environment or ignore/suppression option, user-defined
`__ubsan_default_options`, `__ubsan_on_report`, sanitizer report hook, or
runtime interposition may weaken termination or diagnostics. Any ASan or UBSan
diagnostic invalidates evidence regardless of process status; a passing
sanitizer test is diagnostic-free.

The negative semantic-audit action hermetically compiles and runs an audit-only
signed-overflow fixture with the exact UBSan compiler/link/runtime context. It
must emit a UBSan diagnostic, terminate nonzero before a sentinel write, and be
treated as an expected negative probe. The fixture and binary remain audit-tool
inputs and are absent from all five C++ targets' dependencies, providers,
runfiles, and link inputs.

The ordinary and same-run controller paths intentionally expose the same
terminal activation invariant, so result-only tests cannot authenticate the
required carrier call edge. The audit contract must obtain the production
library's, test-support library's, direct test's, and both process runners'
exact target-owned source inventory from Bazel providers and apply a hermetic
compiler-front-end semantic audit to those actual sources. At analysis time it
must bind the audited `srcs`, `hdrs`, and `textual_hdrs` to the files compiled
by each target and freeze `defines`, `local_defines`, `copts`, `features`,
`nocopts`, `linkopts`, `linkstamp`, `additional_compiler_inputs`,
`additional_linker_inputs`, the resolved C++ toolchain features and arguments,
predefined feature macros, sanitizer ignorelists, and the include graph.
Unlisted source-bearing or link-time attributes are forbidden. The audit must
fail on any source substitution, extra or shadow source, caller-selected path,
compile-setting drift, or unresolved compiler context.

The normal production compilation is the reference semantic AST for the
61-field identity carrier, its sole explicitly defaulted hidden-friend
equality, the builder, and the composite. The audit must parse the actual
test-support compilation separately in normal, ASan, and UBSan configurations
with each configuration's resolved compiler context. In every configuration,
the test-support identity/equality, builder, and composite normalized semantic
ASTs and external callable edges must be identical to the production reference.
The builder is one pure return of the complete canonical aggregate and has no
other control flow. Its only construction edges are the frozen trivial or
`constexpr` field-construction nodes required by that aggregate; it has no
APGAR or external capability edge. No conditional-preprocessing directive,
compiler feature test, sanitizer opt-out attribute or pragma, ignored
function, or sanitizer-disabling compile feature/flag in the producer
implementation source or resolved compilation may make test-support semantics
configuration-dependent. The producer header may contain no conditional beyond
its one include guard.

The semantic audit covers each complete producer translation unit, not only
the two named function bodies. Each production and test-support translation
unit defines exactly the identity builder, the composite, and the identity
carrier's one inline, explicitly defaulted hidden-friend `operator==`; the
`.cc` source remains restricted to the builder and composite definitions. No
other function, method, operator, or callable definition is permitted. The
translation units have no dynamic initialization or destruction, target-owned
init/fini section, alias, indirect function, inline assembly, extra
callable/reference edge, or other executable translation-unit state, and have
an exact reviewed external declaration/call-edge inventory. A byte-identical
builder/composite beside a malicious global initializer is invalid.
The audit resolves every direct or rewritten equality use to that one canonical
hidden-friend declaration and freezes, for each configuration, its emission or
non-emission, linkage, owner, and multiplicity. Compiler inlining does not
authorize an unreviewed equality body or call edge.

The audit must separately parse the actual direct-test compilation in its
resolved normal, ASan, and UBSan contexts and both actual runner compilations
in their normal and sanitizer-reset contexts. The two runner ASTs may differ
only in the exact forced-source constants selected by
`APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING`;
their parsing, identity construction, producer call, error reporting, and
return control flow are otherwise semantically identical. Target-owned runner
code has no dynamic startup or teardown, allocation/deallocation, file,
network, environment, process, dynamic-loading, inline-assembly, or other
external-state edge. Its only I/O edge is writing the one selected invariant
and newline to inherited standard error. The semantic audit covers all
potentially evaluated explicit and implicit call/reference edges, including
constructors, destructors, allocation/deallocation, default and global
initializers, and compiler-generated startup/teardown.

The audit must prove that the composite contains the exact complete
identity-rejection branch followed by a final statement whose sole value is
its one direct call to
`PreflightPhase4ConfirmatoryH4096SessionV5Controller`, returned unchanged. Its
arguments must be, in order:

1. the direct result of
   `BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(` with literal
   `Phase4H4096SessionV5Carrier::kSameRun` and
   `Phase4H4096SessionV5Endpoint::kController`;
2. the direct result of
   `BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(` with literal
   `Phase4H4096SessionV5Carrier::kSameRun`; and
3. the composite's original controller-source parameter.

The source argument may not be copied, reconstructed, substituted, or selected
through another value. The controller call may not be aliased, wrapped,
indirect, duplicated, conditional, inspected, or followed by any operation.
The composite body may not reference
`Phase4H4096SessionV5Carrier::kOrdinary`.

The direct test depends directly only on the new test-support library and
GoogleTest. It is statically linked and fixtureless. An analysis-time
transitive APGAR dependency allowlist freezes the new support plus the exact
existing execution-preflight test-support graph and rejects every wider APGAR
code dependency. Only GoogleTest and required C++ toolchain implicit
dependencies are permitted outside APGAR.

That support graph retains callable execution and allocator code. Retention is
not authorization. The semantic audit must traverse the entire direct-test
semantic AST in each configuration and inspect every potentially evaluated
explicit or implicit callable declaration reference and call edge. This
includes namespace, static, and thread-local initializers; default member and
default-argument initializers; attributes and other evaluated declaration
contexts; constructors, destructors, allocation/deallocation, functions,
methods, operators, lambdas, local helpers, and translation-unit
startup/teardown. The audit freezes the exact reviewed semantic identity,
qualified name, signature, and call-edge inventory of every external
declaration used by the test, including C++ standard-library and GoogleTest
declarations; neither namespace is category-exempt. The only permitted APGAR
callable declarations are
`BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity` and
`PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer`.
Direct calls, address-taking, or indirect references to case construction,
execution, preparers, workers, allocators, route queries, artifacts,
serializers, file or wire I/O, environment or process access, dynamic symbol
lookup, or inline assembly are forbidden. GoogleTest calls are not
category-exempt. The audit freezes the exact allowed GoogleTest declaration
and call-edge inventory arising from the pinned safe macro surface `TEST`,
`EXPECT_EQ`, `EXPECT_TRUE`, `EXPECT_FALSE`, `ASSERT_TRUE`, and `SCOPED_TRACE`;
no other GoogleTest declaration or call edge is permitted. The direct-test
source may define, undefine, alias, or conditionally replace none of those
macros. Death-test and exit-test macros, including `EXPECT_DEATH`,
`ASSERT_DEATH`, `EXPECT_EXIT`, and `ASSERT_EXIT`, every other death/exit-test
surface, and direct calls to GoogleTest death-, subprocess-, or re-execution
internals are forbidden because they create a child or re-execution edge.

The negative audit contract must prove its semantic classifiers reject:

- either `kSameRun` carrier literal replaced by `kOrdinary`;
- a wrapped, indirect, duplicated, conditional, or non-terminal shared
  controller call;
- a copied or substituted source argument;
- any operation after the shared call;
- a direct allocator or execution call from the direct test;
- an allocator or execution call hidden in a local wrapper;
- an allocator or execution function address captured and invoked indirectly;
- an external APGAR method, constructor, and overloaded operator reference;
- an allocator call guarded by `__has_feature(address_sanitizer)`,
  `__has_feature(undefined_behavior_sanitizer)`, or an equivalent predefined
  configuration macro in the producer implementation;
- `no_sanitize`, `no_sanitize_address`,
  `disable_sanitizer_instrumentation`, a sanitizer opt-out pragma or
  source-matching ignorelist entry, or a sanitizer-disabling flag;
- a missing, substituted, additional, reordered, or digest-drifted pinned
  UBSan ignorelist, or another ignore/suppression flag, file, entry, or
  effective audited-source match;
- missing `-fno-sanitize-recover=all`, added `-fsanitize-recover`, a missing or
  weakened `UBSAN_OPTIONS`, runtime sanitizer suppression/default/report hook,
  runtime interposition, a live UB probe that exits zero or reaches its
  sentinel, or a zero-status sanitizer diagnostic;
- `EXPECT_DEATH`, `ASSERT_DEATH`, `EXPECT_EXIT`, `ASSERT_EXIT`, and a local
  macro alias to a death/exit-test macro;
- a direct-test declaration, redeclaration, or definition shadowing either
  allowlisted producer function;
- an allocator call in a namespace-scope initializer;
- an implicit acquisition-capable destructor or allocation edge;
- a sanitizer-conditioned allocator call in the direct test;
- a direct call to a GoogleTest death/subprocess internal;
- a direct-test call or reference to a process, file/filesystem, environment,
  dynamic-loading, raw-syscall, or inline-assembly capability;
- a direct-test `main`;
- an extra producer function, alias, indirect function, target-owned init/fini
  section, or global initializer calling `system`;
- a process or file call in either runner, including one hidden in a startup
  initializer or forced-source-only branch;
- a production/test-support semantic-AST mismatch;
- a missing normal, ASan, or UBSan test-support audit;
- an added `linkstamp` or `additional_linker_inputs` object;
- an unexpected link option or feature;
- an additional or substituted target source;
- a `--gtest_filter` argument or `GTEST_FILTER` environment entry;
- a `manual` or otherwise skipping tag;
- a non-default flaky, local, or sharding attribute;
- a `DISABLED_` test registration;
- an early-return or dead-branch test body;
- a foreign always-link dependency with a global initializer;
- an initializer or teardown added to an otherwise allowlisted APGAR support
  owner;
- a missing, duplicated, substituted, or additional pinned GoogleTest
  framework initializer;
- an extra ignored identity member;
- a missing, out-of-line, non-defaulted, duplicated, custom/partial, or
  additional equality/operator definition;
- a semantic-audit stamp with the wrong owner, basename, content,
  configuration, or an undeclared/stale input inventory; and
- an unrequested or missing semantic-audit output.

Substring matching alone is insufficient. The audit must use the repository's
checksum-pinned hermetic Bazel LLVM compiler front end and a repository-owned
structured semantic checker; ambient or alternate compilers are forbidden.
The positive and negative semantic audits must run in normal, ASan, and UBSan
invocations.

The named
`phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract`
owns the positive semantic audit as well as the provider and dependency
checks. Its `_audit_negative_contract` owns all semantic negative tripwires.
For both the identity builder and composite, each producer compilation must
expose exactly one canonical semantic declaration identity, one target-owned
definition in the producer source, and only the expected header declaration in
its Clang redeclaration chain. The direct-test source contributes zero
declarations, redeclarations, or definitions of either function, and its calls
resolve to those expected header declarations. Both audits fail closed on any
compiler diagnostic, missing or ambiguous semantic declaration identity,
missing or extra target-owned definition, unresolved or indirect external
APGAR callable, unexpected AST shape, skipped target source, or difference
between audited and compiled source/header/define/option/link/toolchain
inventories. Missing production, normal-test-support, ASan-test-support, or
UBSan-test-support semantic evidence is a hard failure. Missing normal/ASan/
UBSan direct-test or either runner's semantic evidence, missing or extra
direct-test registration, a filtered or skipped invocation, a non-GoogleTest
`main`, or an audit-tool input leaked into a C++ target is also a hard failure.
Each positive and negative semantic-audit action exposes exact target-owned
success stamps in `DefaultInfo.files`, created only after every hermetic
semantic check succeeds:

- positive basename
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.ok`
  with exact two-line content
  `P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-001\n`
  followed by `configuration=<configuration>\n`; and
- negative basename
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok`
  with exact two-line content
  `P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-NEGATIVE-001\n`
  followed by `configuration=<configuration>\n`.

`<configuration>` is exactly `normal`, `asan`, or `ubsan` from the producing
target's resolved settings; both sanitizer settings enabled is invalid. Each
stamp owner is its reserved audit target. The producing action declares the
hermetic compiler/checker, all negative fixtures, every audited source/header/
include/tool input, and a canonical serialization of every resolved compile/
link/runtime/dependency inventory as inputs so Bazel's action key invalidates
stale evidence.

The normal, ASan, and UBSan process and link tests must request, consume, and
validate both stamps' owner, basename, exact content, and matching
configuration from their own configuration; merely declaring an orphan audit
action or listing an audit target in `data` is not evidence.

The only derived audit/test target names are:

- `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract`;
- `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract`;
- `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_process_artifacts`;
- `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact`;
- `phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact`;
- `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_process_test`;
  and
- `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test`.

Every executable process, link, and audit-consuming test above resolves
runnable in normal, ASan, and UBSan and has no manual/skipping tag,
compatibility exclusion, filter-bearing argument or environment, inherited
filter environment, non-default flaky/local/sharding behavior, or other
target-level path that can turn required evidence into a zero-check pass.

Each original binary provider—the direct test and both process runners—must
expose exactly one target-owned executable in `DefaultInfo.files`, default
runfiles, and data runfiles before any audit repackaging. No extra file, data,
workspace symlink, root symlink, empty filename, or child executable is
allowed. Analysis-time negative contracts must exercise and distinguish:

- unexpected file;
- missing executable;
- workspace symlink;
- root symlink; and
- empty filename.

Both process-runner links must contain `main` and the defined symbols for:

- `BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity`;
- `PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer`;
- `BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity`;
- `BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell`;
- `PreflightPhase4ConfirmatoryH4096SessionV5Controller`;
- `BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5`;
- `ComputePhase4CanonicalAlgorithmBudgetChecksumV1`;
- `ComputePhase4PairedBudgetChecksumForAuthorityV1`; and
- `PreflightPhase4CorpusV2SessionExecutionAuthority`.

Both links must reject the ordinary producer identity, builder, and composite
preflight; report or telemetry builders and validators; Raw, telemetry, or
report artifact libraries; ordinary or same-run report runners; diagnostic
executors; representative-case and fixture builders; candidate-pool
preparers; workers; worker launchers; finalizers; self-execution; allocators;
route-query execution; serializers; wire/file I/O; runfiles resolution;
artifact installation; durable-output paths; and acquisition-capable child
executables. The forbidden matcher must have an independent test covering at
least one representative symbol from every closed capability class.

ASan and UBSan instrument and execute the direct identity/composite test
through the test-support seam. The direct-test ELF may retain symbols from its
exact execution-preflight support graph and is structural sanitizer evidence
only; it is not capability-minimal link evidence. The semantic call-edge audit
must still prove that the direct-test source cannot invoke those retained
capabilities.

The main producer library and process runners remain unavailable in
instrumented configurations. The provider audits, process test, and link
surface test must still run under ASan and UBSan by consuming explicit
sanitizer-reset unsanitized artifacts for both original process runners. They
must never consume the instrumented direct test or silently omit either
runner.

## Process contract

Each process runner accepts exactly one required option:

```text
--runtime_commit=<40 lowercase hexadecimal characters>
```

There are no positional arguments and no abbreviated or duplicate option.
The process accepts no carrier, case, pool, schema, authority, budget, Raw,
telemetry, report, fixture, path, output, request/response descriptor, child,
activation, environment-selected input, or testing mode.

Every invocation exits with exact status `2`, writes empty stdout, emits
exactly one bounded invariant line plus one newline to stderr, and creates no
output artifact. Exact process-visible invariants are:

- malformed, missing, duplicate, abbreviated, unknown, positional, or extra
  arguments: `P4PAIR-H4096-SESSION-V5-ARGUMENT-001`;
- forced unpublishable embedded source:
  `P4PAIR-H4096-SESSION-V5-SOURCE-001`;
- well-formed runtime commit unequal to the embedded commit:
  `P4PAIR-H4096-SESSION-V5-SOURCE-002`;
- canonical clean source: `P4PAIR-H4096-SESSION-V5-ACTIVATION-001`; and
- impossible success: `P4PAIR-H4096-SESSION-V5-BYPASS-001`.

Printing an expected invariant with exit status zero or any status other than
`2` is invalid evidence. A second stderr line, stdout content, an output file,
or external-state access is also a failure.

## Required review evidence

Implementation review must prove:

- the no-argument builder returns exactly the 61-field canonical identity;
- the canonical identity reaches exactly the shared activation barrier;
- every field independently rejects with
  `P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-001` before source or
  activation;
- Raw, telemetry, and report cardinality mutations reject independently rather
  than being normalized from repetitions or each other;
- ordinary/same-run, v1/v2/v3, configuration, Session, Plan, Execution,
  carrier, Raw/telemetry/report schema, roster, canonical-budget,
  paired-budget, and predecessor substitutions cross-reject;
- predecessor values, ordinary successor values, swapped budget domains, and
  coordinated mixed identities reject;
- clean source reaches the activation barrier while dirty, unstamped,
  malformed, uppercase, cross-commit, and forced-unpublishable source fails
  earlier;
- canonical process execution exits `2`, stdout is empty, stderr is exactly
  the activation invariant plus newline, and no output is created;
- every argument and source rejection has the exact same status/output
  discipline;
- poisoned environment variables, independent Raw, telemetry, report, and
  fixture FIFOs, output paths, and inherited request/response descriptors
  remain untouched;
- every original provider satisfies its exact files/runfiles contract;
- all five C++ targets satisfy their exact dependency/link-input closures and
  startup provenance;
- the compiler-front-end semantic audit authenticates the actual target-owned
  source inventory, production/test-support AST and callable-edge identity in
  normal/ASan/UBSan contexts, the literal `kSameRun` direct tail delegation,
  the unchanged source parameter, the complete direct-test callable and
  control-flow whitelist, and both runners' frozen parsing/reporting/preflight
  surface;
- the direct test has frozen non-skipping execution attributes, no `main`, only
  non-disabled registrations, pinned GoogleTest `main`, and complete
  unfiltered assertion execution in normal, ASan, and UBSan;
- the resolved sanitizer compile/codegen contexts contain no opt-out and
  instrument the producer builder/composite and direct-test assertions;
- UBSan uses exact nonrecovering compile/runtime settings across the linked
  direct-test support graph, the audit-only live UB probe terminates before its
  sentinel, and successful ASan/UBSan evidence contains no sanitizer
  diagnostic;
- the target-owned positive and negative semantic-audit stamps are requested
  and consumed in all three test configurations;
- every required semantic negative tripwire fails for its intended reason in
  normal, ASan, and UBSan invocations;
- both process links satisfy the required and forbidden symbol contracts in
  normal, ASan, and UBSan invocations;
- ASan and UBSan instrument the direct test while process and link tests use
  the explicitly transitioned unsanitized runners;
- the strict same-run offline validator is not a dependency of the preflight
  library or either runner;
- historical Protocol-v2/roster-v3/Session-v4 and the committed ordinary
  producer-preflight behavior remain unchanged; and
- the future production runner remains absent.

## Closed capabilities and non-goals

The production runner
`phase4_confirmatory_h4096_session_v5_same_run_per_net_report_runner` remains
absent. No Raw, telemetry, report, or other evidence artifact is read,
constructed, serialized, validated, or emitted.

This contract does not authorize diagnostic execution or case construction,
even though case `10100` is generated and requires no board fixture. It does
not authorize a preparer, worker, allocator, finalizer, self-execution path,
report or telemetry builder, serializer, output installer, or child process.

Protocol v3, roster v4, existing authority JSON, Same-Run Raw Evidence schema
2, Raw Wire 2, Same-Run Decision Telemetry schema 1, Telemetry Wire 2, and
Per-Net Report Artifact schema 1 remain unchanged. The compiled identity does
not become a payload or a checksum domain.

Both operational publications, exact-small snapshot/Oracle, every other
development cell, heldout, imported, fixed-query, stress, aggregation,
matrix, decision, campaign, and acquisition remain closed. This preflight is
not an operational publication or complete-chain member and supports no
Phase 4 or M1 completion claim.
