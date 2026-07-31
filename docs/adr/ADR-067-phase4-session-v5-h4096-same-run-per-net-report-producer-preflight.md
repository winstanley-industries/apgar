# ADR-067: Phase 4 Session-v5 H=4096 Same-Run Per-Net Report Producer Preflight

**Status:** Accepted as an inactive producer-preflight contract; telemetry,
report production, execution, and acquisition remain closed
**Date:** July 30, 2026
**Applies to:** Development-only Session-v5/H=4096 same-run report capability
authentication before the complete consuming-publication chain exists

## Context

ADR-063 freezes the inactive Session-v5/H=4096 Raw boundary and its shared
`P4PAIR-H4096-SESSION-V5-ACTIVATION-001` closure. ADR-065 freezes the larger
same-run Raw/telemetry/report publication join and its strict offline
validator. ADR-066 separately freezes the smaller ordinary report-producer
preflight. These boundaries authenticate authority without producing or
observing an allocation outcome.

The same-run validator deliberately opens and authenticates Raw first, then
its same-invocation telemetry sidecar, and only then the report. That
three-artifact validator is not a producer boundary. It accepts paths and
links pure validation and canonical-input capabilities that a future report
producer must not inherit.

The next smallest acquisition-free step is a separately named compiled
identity for the future same-run report producer. It must positively bind the
atomic Raw Wire 2, Telemetry Wire 2, and report trust surface before stopping
at the existing shared activation barrier. It cannot widen the ordinary
producer identity with a carrier selector: doing so would make the two
different payload and authority surfaces caller-substitutable.

The historical H=4096 same-run report runner, diagnostic executor, telemetry
and report builders, serializers, and local authority selection remain
Protocol-v2/roster-v3/Session-v4 capabilities. Case `10100` is generated, but
executing it still observes allocator behavior. Fixturelessness therefore
does not make those surfaces acquisition-free, and linking them dormant
behind an always-taken barrier is not acceptable.

This contract freezes only the new immutable identity and the evidence a
later preflight-only implementation must provide. It does not add that
implementation, a production runner, a telemetry or report builder, or an
execution capability.

## Decision

- Freeze the compiled-only identity
  `Phase4H4096SessionV5SameRunPerNetReportProducerIdentity`. A separately
  named compiled boundary selects it. No caller carrier flag, case ID, source
  commit, environment variable, matching checksum, artifact field, sidecar
  path, or runtime option may select or reinterpret it.
- The identity has schema version `1` and exactly 61 equality-participating
  fields. It is not serialized evidence, gains no artifact checksum, and is
  not an eleventh Confirmatory Decision Protocol v3 substitution. Protocol v3,
  canonical roster v4, and all existing payload JSON remain byte-for-byte
  unchanged.
- Bind the complete common authority transition:
  - execution authority
    `Phase4TrialExecutionAuthority::kCorpusV2H4096SessionV5`;
  - configuration authority
    `phase4_confirmatory_corpus_v2_h4096_session_v5`, directly superseding
    `phase4_confirmatory_corpus_v2_h4096`;
  - Confirmatory Decision Protocol authority
    `phase4_confirmatory_decision_protocol_v3` with checksum
    `4963299999381388941`, directly superseding
    `phase4_confirmatory_decision_protocol_v2` with checksum
    `11520586171987743043`;
  - canonical algorithm-budget roster authority
    `phase4_confirmatory_canonical_algorithm_budget_roster_v4` with checksum
    `12316700735749461907`, directly superseding
    `phase4_confirmatory_canonical_algorithm_budget_roster_v3` with checksum
    `18429170436700418962`;
  - Representative Corpus version `2` and checksum `4182833841936446798`;
  - Representative Manifest schema/version `2` and checksum
    `9613362670139358355`; and
  - Workload-Net Roster Manifest schema/version `2` and checksum
    `14986327048461036142`.
- Bind all three artifact-authority ancestries without skipping their retained
  Protocol-v1 predecessors:
  - same-run Raw
    `phase4_confirmatory_same_run_raw_evidence_v3` directly supersedes
    `phase4_confirmatory_same_run_raw_evidence_v2`, whose retained predecessor
    is `phase4_confirmatory_same_run_raw_evidence_v1`;
  - same-run telemetry
    `phase4_confirmatory_same_run_decision_telemetry_v3` directly supersedes
    `phase4_confirmatory_same_run_decision_telemetry_v2`, whose retained
    predecessor is
    `phase4_confirmatory_same_run_decision_telemetry_v1`; and
  - same-run report
    `phase4_confirmatory_same_run_per_net_report_publication_join_v3` directly
    supersedes
    `phase4_confirmatory_same_run_per_net_report_publication_join_v2`, whose
    retained predecessor is
    `phase4_confirmatory_same_run_per_net_report_publication_join_v1`.
- Bind exact cell `(10100,4)`, four preparation workers, 20 repetitions, and
  `Phase4H4096SessionV5Carrier::kSameRun`. Ordinary carrier, calibration cell
  `(10200,8)`, or any other case/pool is foreign even when the remaining
  fields are made self-consistent.
- Bind the complete unchanged payload shapes:
  - Raw Evidence schema `2` over Raw Wire `2`, with 20 paired attempts and 40
    arm attempts;
  - required Same-Run Decision Telemetry schema `1` over Telemetry Wire `2`,
    with 20 pair captures, 40 arm captures, six workload nets per arm, and 240
    ordered per-net rows; and
  - Per-Net Report Artifact schema `1` with
    `raw_wire_schema_version=2`, binding Raw repetition zero in baseline-first
    order through two arms, six workload nets per arm, 12 total per-net rows,
    workload-net roster checksum `12521697377381992336`, and literal
    `decision_eligible=false`.
- Bind the complete canonical configuration: Candidate-Allocation Session v5,
  Targeted Regeneration Plan v3, Targeted Regeneration Execution v6, and
  equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`.
- Require canonical algorithm-budget checksum `13645569624513409309` and the
  independently reconstructed paired semantic-budget checksum
  `12493092620111240227`. The identity and preflight must positively reject:
  - predecessor Session-v4 canonical checksum `8829615204625848656` and
    paired checksum `5851813264366095594`;
  - ordinary Session-v5 canonical checksum `7657176792159702821` and paired
    checksum `13340538727848385478`;
  - either same-run successor checksum substituted for the other; and
  - every mixed or reassembled identity carrying those values.
- Freeze the identity `RecordDecl` itself. It is a public aggregate with
  exactly the 61 non-static data members listed by the schema, in that order
  and with the schema's exact types. It has no base, virtual member, static
  state, anonymous union, bit-field, `[[no_unique_address]]` member,
  user-declared constructor/destructor/conversion, or extra method. Numeric and
  boolean defaults are zero/false, authority `std::string_view` values default
  empty, and the explicit enum defaults are the listed execution authority,
  `kSameRun` carrier, and zero-valued `kBaselineFirst` report order. Its sole
  comparison declaration is the non-template hidden-friend `operator==` over
  two const identity references, explicitly `= default`; no custom or partial
  equality is permitted. The builder must explicitly initialize all 61 members
  rather than relying on an omitted member's default.
- Reserve the internal builder
  `BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity()`
  and composite preflight
  `PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer(...)`.
  Neither API accepts a carrier selector, path, artifact, or sidecar. The
  builder takes no input and returns the one canonical same-run identity. The
  composite accepts only that identity and one controller source association
  so direct tests can prove field-by-field rejection.
- Fix the composite validation order:
  1. compare every one of the 61 report-producer identity fields to the
     separately built canonical same-run identity;
  2. on any mismatch, return a
     `Phase4PairedTrialErrorCode::kMeasurementAssociation` error with exact
     invariant
     `P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-001`;
  3. only after complete producer-identity authentication, delegate exactly
     once to the existing Session-v5 controller preflight with the compiled
     `kSameRun`/`kController` identity, the compiled canonical same-run cell,
     and the supplied controller source association; and
  4. return that result unchanged as the composite's final operation.
- Producer-identity failure precedes source validation. The shared controller
  then reconstructs the complete 20-repetition specification roster and both
  arms before source validation. Production source identity must be clean and
  stamped, and the 40-character lowercase runtime commit must exactly equal
  the embedded commit. Source failure precedes the activation barrier.
- Canonical clean input must terminate at the existing immutable
  `P4PAIR-H4096-SESSION-V5-ACTIVATION-001` error. No code may recognize,
  translate, suppress, catch as success, or continue after that error. No new
  same-run report barrier, complete-chain boolean, environment override,
  testing bypass, or alternate success return is permitted.
- The producer preflight reads no Raw, telemetry, report, case, fixture, path,
  descriptor, or environment-selected input. It does not call or link the
  strict same-run validator. That validator's Raw-before-sidecar-before-report
  open order remains authoritative for offline artifact authentication but is
  not executed at this compiled preflight boundary.
- A separately reviewed implementation may add only these five C++ targets:
  - private library
    `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight`;
  - private sanitizer-safe direct-test support library
    `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support`;
  - fixtureless static direct test
    `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test`;
  - fixtureless process test binary
    `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner`;
    and
  - fixtureless forced-unpublishable process test binary
    `phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner`.
  Non-C++ Starlark analysis rules and Python tests may inspect only those five
  targets and their derived audit artifacts. They add no APGAR production
  dependency or execution capability. The pinned compiler front end, semantic
  checker, negative fixtures, and every other audit-tool input remain test-only
  inputs of the audit targets and are absent from all five C++ targets'
  dependencies, providers, runfiles, compile inputs, and link inputs.
- Keep the future production runner
  `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_runner` absent.
  The committed ordinary producer-preflight targets and the existing same-run
  offline validator remain separate and unchanged.
- The two process surfaces accept exactly one required
  `--runtime_commit=<40 lowercase hex>` option and no positional arguments.
  They accept no carrier, case, pool, budget, authority, schema, Raw,
  telemetry, report, fixture, output, path, descriptor, request/response file
  descriptor, child, activation, or testing option. Identity mutations belong
  in the direct test, not in a caller-selectable process mode.
- Freeze exact process outcomes:
  - argument rejection prints exactly
    `P4PAIR-H4096-SESSION-V5-ARGUMENT-001` plus one newline;
  - forced-unpublishable source prints exactly
    `P4PAIR-H4096-SESSION-V5-SOURCE-001` plus one newline;
  - a well-formed but cross-commit runtime identity prints exactly
    `P4PAIR-H4096-SESSION-V5-SOURCE-002` plus one newline;
  - canonical clean input prints exactly
    `P4PAIR-H4096-SESSION-V5-ACTIVATION-001` plus one newline; and
  - an impossible success prints exactly
    `P4PAIR-H4096-SESSION-V5-BYPASS-001` plus one newline so it cannot be
    mistaken for activation evidence.
  Every invocation exits with exact status `2`, writes empty stdout, writes no
  second stderr line or unbounded detail, and creates no output artifact. A
  correct invariant with exit zero or any other status is invalid evidence.
- The implementation target's own objects may define only the immutable
  same-run producer identity builder, composite preflight, and the identity
  carrier's schema-frozen hidden-friend equality when codegen emits it. Its
  sole APGAR code dependency is the existing capability-minimal
  `phase4_h4096_session_v5_execution_preflight`. It compiles into independent
  function/data sections so the process runners' static
  `-Wl,--gc-sections` links expose the actual retained boundary.
- The direct-test support target must compile the identical producer-preflight
  source and header list without semantic defines. Its only code-dependency
  substitution is
  `phase4_h4096_session_v5_execution_preflight_test_support`; `testonly`,
  sanitizer compatibility, and compile/link options may differ only as needed
  to expose the same code to ASan and UBSan.
- Analysis must freeze the exact direct and transitive C++ dependency labels,
  `CcInfo` linker-input owners/artifacts, and resolved `deps`,
  `implementation_deps`, `dynamic_deps`, `malloc`, `link_extra_lib`,
  `additional_linker_inputs`, and `alwayslink` effects for the main library,
  test-support library, direct test, and both runners. The main library's only
  direct C++ dependency is the production execution preflight; each runner's
  only direct C++ dependency is the main library; the test-support and direct
  test have only the substitutions stated here. Only the exact frozen
  transitive APGAR closures and checksum-pinned C++ toolchain implicit
  libraries are permitted. A foreign repository library, custom allocator,
  extra object/archive, or always-linked constructor is forbidden even if all
  target-owned source ASTs remain unchanged. Final ELF startup/init/fini
  provenance must resolve only to those frozen linker inputs. In the direct
  test, the exact configuration-specific startup/teardown inventory may contain
  only pinned C++ runtime and sanitizer entries, the direct-test translation
  unit's exact GoogleTest registration initializers, and these three
  framework-owned initializers from the pinned
  `@googletest//:gtest_main` closure:
  `_GLOBAL__sub_I_gmock.cc`, `_GLOBAL__sub_I_gtest.cc`, and
  `_GLOBAL__sub_I_gtest_death_test.cc`. Their source-object provenance and
  multiplicity are exact in each configuration; no other GoogleTest framework
  startup or teardown is permitted. No APGAR production or test-support owner
  may contribute an initializer, destructor, init/fini entry, or pre-/post-test
  execution edge, even when that owner is already on the dependency allowlist.
- The direct `cc_test` must have empty `args`, `env_inherit`, and `tags`; empty
  `env` in normal and ASan; exact UBSan `env`
  `{"UBSAN_OPTIONS": "halt_on_error=1"}`; exact size `small`; and default
  non-flaky, non-local, unsharded execution attributes. Its compatibility
  constraints must resolve runnable in normal, ASan, and UBSan. Its source
  must contribute no `main` declaration or
  definition, and its linked `main` must resolve to the pinned
  `@googletest//:gtest_main` boundary. Every registered test name must be
  nonempty and must not contain a `DISABLED_` suite or case component. The
  semantic audit must freeze the exact assertion inventory, enumerate a
  nonempty exact registration set, and prove an unfiltered invocation of the
  binary ran that complete set and its assertions under normal, ASan, and
  UBSan configurations. Aside from compiler-added sanitizer instrumentation,
  the normalized registered test bodies, control-flow graphs, and required
  builder/composite/assertion call inventories must be identical across all
  three configurations. In each resolved configuration, the audited
  control-flow graphs must prove that, across the complete unfiltered
  registration set, the required builder/composite call sites and all 61
  independent mutation/assertion paths are reachable and unavoidable before
  every successful direct-test binary exit. An early return, skip, false/dead
  guard, empty parameterization, exception, sanitizer-conditioned omission, or
  other path that can report success without executing them is forbidden.
- UBSan evidence must be fail-live. The new test-support and direct-test
  compilations must contain exact `-fno-sanitize-recover=all` in the UBSan
  configuration and no recovery-enabling flag. The sole permitted
  suppression-bearing compile flag is exact
  `-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt`, injected
  by the checksum-pinned LLVM toolchain. The referenced file has SHA-256
  `46f903f898f41e3424c773b9de9efc659cc2de3a00cfb214c840ac96e2c22bf7`
  and, after its comment, exactly these two entries in this order:
  `src:external/llvm+/3rd_party/libc/glibc/csu/elf-init-2.31.c` and
  `src:external/llvm++musl+musl_libc/src/env/__libc_start_main.c`. The semantic
  audit must parse that exact file using Clang ignorelist semantics, reject
  path, digest, content, order, or multiplicity drift, and prove neither entry
  matches any target-owned or transitive APGAR source, the direct-test source,
  or the live-probe source. Any other ignore/suppression flag, file, entry, or
  effective audited-source match is forbidden. The exact target-level
  `UBSAN_OPTIONS=halt_on_error=1` applies to the complete linked
  execution-preflight support graph. No caller-selected or inherited sanitizer
  environment or ignore/suppression option, user-defined
  `__ubsan_default_options`, `__ubsan_on_report`, sanitizer report hook, or
  runtime interposition may weaken termination or diagnostics. Any ASan or
  UBSan diagnostic invalidates evidence regardless of process status; a
  passing sanitizer test must be diagnostic-free.
  The negative semantic-audit action must hermetically compile and run an
  audit-only signed-overflow fixture with the exact UBSan compiler/link/runtime
  context. It must emit a UBSan diagnostic, terminate nonzero before a sentinel
  write, and be treated as an expected negative probe. The fixture and binary
  remain audit-tool inputs and are absent from all five C++ targets'
  dependencies, providers, runfiles, and link inputs.
- Result-only tests are insufficient to authenticate the composite call edge:
  the ordinary and same-run controller paths intentionally terminate at the
  same activation invariant. The audit contract must therefore obtain the
  production library's, test-support library's, direct test's, and both process
  runners' exact target-owned source inventory from their Bazel providers and
  run a hermetic compiler-front-end semantic audit over those actual sources.
  At analysis time it must bind the audited `srcs`, `hdrs`, and `textual_hdrs`
  to the files compiled by each target and freeze `defines`, `local_defines`,
  `copts`, `nocopts`, `features`, `linkopts`, `linkstamp`,
  `additional_compiler_inputs`, `additional_linker_inputs`, the resolved C++
  toolchain features and arguments, predefined feature macros, sanitizer
  ignorelists, and the include graph. Unlisted source-bearing or link-time
  attributes are forbidden. The audit must reject substituted, additional,
  shadow, or path-selected source input and any unresolved compile-context
  difference.
- The normal production compilation is the reference semantic AST for the
  61-field identity carrier, its sole explicitly defaulted hidden-friend
  equality, the builder, and the composite. The audit must parse the actual
  test-support compilation separately in normal, ASan, and UBSan
  configurations using each configuration's resolved compiler context. The
  identity/equality, builder, and composite normalized semantic ASTs and
  external callable edges must be identical to the production reference in
  all three configurations.
  The builder must remain a single pure return of the complete canonical
  aggregate. Its only construction edges are the frozen trivial or `constexpr`
  field-construction nodes required by that aggregate; it has no APGAR or
  external capability edge. No conditional-preprocessing directive, compiler
  feature test, sanitizer opt-out attribute or pragma, ignored function, or
  sanitizer-disabling compile feature/flag in the producer implementation
  source or resolved compilation may make test-support semantics
  configuration-dependent. The producer header may contain no conditional
  beyond its one include guard.
- The semantic audit covers each complete producer translation unit, not only
  the two named function bodies. Each production and test-support translation
  unit must define exactly the identity builder, the composite, and the
  identity carrier's one inline, explicitly defaulted hidden-friend
  `operator==`; the `.cc` source remains restricted to the builder and
  composite definitions. No other function, method, operator, or callable
  definition is permitted. The translation units may have no dynamic
  initialization or destruction, target-owned init/fini section, alias,
  indirect function, inline assembly, extra callable/reference edge, or other
  executable translation-unit state, and must have an exact reviewed external
  declaration/call-edge inventory. A byte-identical builder/composite beside a
  malicious global initializer is invalid.
  The audit must resolve every direct or rewritten equality use to that one
  canonical hidden-friend declaration and freeze, for each configuration, its
  emission or non-emission, linkage, owner, and multiplicity. Compiler
  inlining does not authorize an unreviewed equality body or call edge.
- The audit must separately parse the actual direct-test compilation in its
  resolved normal, ASan, and UBSan contexts and both actual runner
  compilations in their normal and sanitizer-reset contexts. The two runner
  ASTs may differ only in the exact forced-source constants selected by
  `APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING`;
  their parsing, identity construction, producer call, error reporting, and
  return control flow must otherwise be semantically identical. Target-owned
  runner code may have no dynamic startup or teardown, allocation/deallocation,
  file, network, environment, process, dynamic-loading, inline-assembly, or
  other external-state edge. Its only I/O edge is writing the one selected
  invariant and newline to inherited standard error. The semantic audit must
  cover all potentially evaluated explicit and implicit call/reference edges,
  including constructors, destructors, allocation/deallocation, default and
  global initializers, and compiler-generated startup/teardown.
- The semantic audit must prove that the composite contains the complete
  61-field identity-rejection branch followed by a final statement that is its
  sole direct call to
  `PreflightPhase4ConfirmatoryH4096SessionV5Controller`, returned without an
  alias, wrapper, function pointer, conditional, result inspection, or
  continuation. The first argument must be the direct result of
  `BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity` with literal
  `Phase4H4096SessionV5Carrier::kSameRun` and
  `Phase4H4096SessionV5Endpoint::kController`; the second must be the direct
  result of `BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell` with literal
  `Phase4H4096SessionV5Carrier::kSameRun`; and the third must be the
  composite's original source parameter, not a copy, reconstruction,
  substitution, or selected value. The composite body may not reference
  `kOrdinary`.
- The direct test's sanitizer support graph retains execution-capable code, so
  dependency closure alone is not sufficient. The same semantic audit must
  traverse the entire direct-test semantic AST in each configuration and
  examine every potentially evaluated explicit or implicit callable
  declaration reference and call edge. This includes namespace, static, and
  thread-local initializers; default member and default-argument initializers;
  attributes and other evaluated declaration contexts; constructors,
  destructors, allocation/deallocation, functions, methods, operators,
  lambdas, local helpers, and translation-unit startup/teardown. The audit must
  freeze the exact reviewed semantic identity, qualified name, signature, and
  call-edge inventory of every external declaration used by the test,
  including C++ standard-library and GoogleTest declarations; neither namespace
  is category-exempt. The only permitted APGAR callable declarations are the
  new same-run producer identity builder and composite preflight. Direct or
  indirect references to
  execution, case, preparer, worker, allocator, route-query, artifact,
  serializer, I/O, environment, process, dynamic-lookup, or inline-assembly
  capability are forbidden. Local helpers must be traversed rather than
  treated as opaque.
  GoogleTest calls are not category-exempt. The audit must freeze the exact
  allowed GoogleTest declaration and call-edge inventory arising from the
  pinned safe macro surface `TEST`, `EXPECT_EQ`, `EXPECT_TRUE`, `EXPECT_FALSE`,
  `ASSERT_TRUE`, and `SCOPED_TRACE`; no other GoogleTest declaration or call
  edge is permitted. The direct test may not define, undefine, alias, or
  conditionally replace those macros. `EXPECT_DEATH`, `ASSERT_DEATH`,
  `EXPECT_EXIT`, `ASSERT_EXIT`, every other death/exit-test surface, and direct
  calls to GoogleTest death-, subprocess-, or re-execution internals are
  forbidden because they create a child or re-execution edge.
- The audit negative contract must independently demonstrate rejection of at
  least: `kOrdinary` substituted for either carrier literal; a wrapped,
  indirect, duplicated, conditional, or non-terminal controller call; code
  after the shared call; a copied or substituted source argument; a direct
  allocator/execution call from the direct test; the same call hidden in a
  local wrapper; an allocator/execution address captured and invoked
  indirectly; an external APGAR method, constructor, and overloaded operator
  reference; an allocator call hidden behind
  `__has_feature(address_sanitizer)`,
  `__has_feature(undefined_behavior_sanitizer)`, or an equivalent predefined
  configuration macro in the producer source; a production/test-support AST
  mismatch; `no_sanitize`, `no_sanitize_address`,
  `disable_sanitizer_instrumentation`, a sanitizer opt-out pragma or
  source-matching ignorelist entry, or a sanitizer-disabling flag; a missing,
  substituted, additional, reordered, or digest-drifted pinned UBSan
  ignorelist, or another ignore/suppression flag, file, entry, or effective
  audited-source match; missing
  `-fno-sanitize-recover=all`, added `-fsanitize-recover`, a missing or weakened
  `UBSAN_OPTIONS`, runtime sanitizer suppression/default/report hook, runtime
  interposition, a live UB probe that exits zero or reaches its sentinel, or a
  zero-status sanitizer diagnostic; a missing configuration audit;
  `EXPECT_DEATH`, `ASSERT_DEATH`,
  `EXPECT_EXIT`, `ASSERT_EXIT`, and a local macro alias to any of them; a
  direct-test redeclaration or definition shadowing either allowlisted producer
  function; an allocator call in a namespace-scope initializer; an implicit
  acquisition-capable destructor or allocation edge; a sanitizer-conditioned
  allocator call in the direct test; a direct call to a GoogleTest
  death/subprocess internal; a direct-test call or reference to a process,
  file/filesystem, environment, dynamic-loading, raw-syscall, or inline-assembly
  capability; a direct-test `main`; an extra producer function, alias, indirect
  function, target-owned init/fini section, or global initializer calling
  `system`; a process or file call in either runner, including one hidden in a
  startup initializer or forced-source-only branch; an added `linkstamp` or
  `additional_linker_inputs` object; an unexpected link option or feature; an
  extra or substituted target source; a `--gtest_filter` argument or
  `GTEST_FILTER` environment entry; a `manual` or otherwise skipping tag; a
  non-default flaky, local, or sharding attribute; a `DISABLED_` test
  registration; an early-return or dead-branch test body; a foreign always-link
  dependency with a global initializer; an initializer or teardown added to an
  otherwise allowlisted APGAR support owner; a missing, duplicated, substituted,
  or additional pinned GoogleTest framework initializer; an extra ignored
  identity member; a missing, out-of-line, non-defaulted, duplicated,
  custom/partial, or additional equality/operator definition; a semantic-audit
  stamp with the wrong owner, basename, content, configuration, or an
  undeclared/stale input inventory; and an unrequested or missing
  semantic-audit output.
  Substring matching alone is not acceptable evidence. The audit must use the
  repository's checksum-pinned hermetic Bazel LLVM compiler front end and a
  repository-owned structured semantic checker; ambient or alternate compilers
  are forbidden. It must run in normal, ASan, and UBSan test invocations.
- The later implementation's reserved
  `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract`
  owns the positive semantic audit in addition to provider and dependency
  checks. Its paired `_audit_negative_contract` owns all semantic negative
  tripwires. For both the builder and composite, each producer compilation
  must expose exactly one canonical semantic declaration identity, one
  target-owned definition in the producer source, and only the expected header
  declaration in its Clang redeclaration chain. The direct-test source must
  contribute zero declarations, redeclarations, or definitions of either
  function, and its calls must resolve to those expected header declarations.
  Both audits must fail closed on any compiler diagnostic, missing or ambiguous
  semantic declaration identity, missing or extra target-owned definition,
  unresolved or indirect external APGAR callable, unexpected AST shape,
  skipped target source, or mismatch between audited and compiled
  source/header/define/option/link/toolchain inventories. Missing production,
  normal-test-support, ASan-test-support, UBSan-test-support, direct-test, or
  either runner's semantic evidence is a hard failure. Missing or extra
  direct-test registration, a filtered or skipped invocation, a non-GoogleTest
  `main`, or an audit-tool input leaked into a C++ target is also a hard
  failure. Each positive and negative semantic-audit action must expose exact
  target-owned success stamps in `DefaultInfo.files`, created only after every
  hermetic semantic check succeeds:
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
  target's resolved settings; both sanitizer settings enabled is invalid.
  Each stamp owner is its reserved audit target. The producing action must
  declare the hermetic compiler/checker, all negative fixtures, every audited
  source/header/include/tool input, and a canonical serialization of every
  resolved compile/link/runtime/dependency inventory as inputs so Bazel's
  action key invalidates stale evidence.
  The normal, ASan, and UBSan process and link tests must request, consume, and
  validate both stamps' owner, basename, exact content, and matching
  configuration from their own configuration; merely declaring an orphan
  audit action or listing an audit target in `data` is not evidence.
- Both production-shaped process-runner links must retain these symbol
  boundaries in addition to `main`:
  - `BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity`;
  - `PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer`;
  - `BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity`;
  - `BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell`;
  - `PreflightPhase4ConfirmatoryH4096SessionV5Controller`;
  - `BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5`;
  - `ComputePhase4CanonicalAlgorithmBudgetChecksumV1`;
  - `ComputePhase4PairedBudgetChecksumForAuthorityV1`; and
  - `PreflightPhase4CorpusV2SessionExecutionAuthority`.
- Both links must exclude the ordinary producer identity, builder, and
  composite preflight; all report and telemetry builders or validators;
  Raw/report/telemetry artifact libraries; ordinary or same-run report
  runners; diagnostic executors; representative-case or fixture builders;
  candidate-pool preparers; workers and worker launchers; finalizers;
  self-execution paths; allocators and route-query execution; serializers;
  wire/file I/O; runfiles resolution; artifact installers; durable-output
  paths; and acquisition-capable child executables. Representative forbidden
  symbol classes include
  `BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity`,
  `PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer`,
  `BuildPhase4PerNetReportArtifact`,
  `ValidatePhase4PerNetReportArtifact`, `ExecutePhase4`,
  `BuildPhase4RepresentativeCase`, `PrepareInitialCpuCandidatePools`,
  `CpuCandidatePoolPreparer`,
  `PreflightPhase4ConfirmatoryH4096SessionV5Worker`, `AllocateOneWorld`,
  `AllocateMultiWorld`, `SerializePhase4`, Phase 4 wire read/write/encode/
  decode functions, runfiles helpers, and child-launch functions. A linked but
  unreachable capability violates this contract.
- The fixtureless direct `cc_test` is the sole narrow exception to the process
  symbol closure. It may depend directly only on the new sanitizer-safe
  producer-preflight test support and GoogleTest. The new support may depend
  only on the exact existing
  `phase4_h4096_session_v5_execution_preflight_test_support` graph. An
  analysis-time transitive APGAR dependency allowlist must reject any wider
  graph while permitting only GoogleTest and required C++ toolchain implicit
  dependencies outside APGAR. The direct test remains statically linked and
  fixtureless, with no path, descriptor, Raw, telemetry, report, output,
  environment-selected input, or child executable. Its retained transitive
  symbols are not authority to call them; the semantic call-edge audit above
  is mandatory in addition to the dependency allowlist. Its analysis-time
  execution-attribute and linked-`main` audit is mandatory in addition to a
  successful test exit.
- Each original direct- or process-test binary provider, before audit
  repackaging, must expose exactly one target-owned executable in
  `DefaultInfo.files`, default runfiles, and data runfiles. No additional file,
  data, workspace symlink, root symlink, empty filename, or child executable
  is permitted. Analysis-time negative contracts must prove unexpected-file,
  missing-executable, workspace-symlink, root-symlink, and empty-filename
  classifiers are live.
- Reserve the derived audit/test target names:
  - `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract`;
  - `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract`;
  - `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_process_artifacts`;
  - `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact`;
  - `phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact`;
  - `phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_process_test`;
    and
  - `phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test`.
- Every executable process, link, and audit-consuming test above must resolve
  runnable in normal, ASan, and UBSan and have no manual/skipping tag,
  compatibility exclusion, filter-bearing argument or environment, inherited
  filter environment, non-default flaky/local/sharding behavior, or other
  target-level path that can turn required evidence into a zero-check pass.
- ASan and UBSan must instrument and run the direct identity/composite test
  through the named test-support seam. Sanitizer registration may retain
  execution-capable symbols from that exact allowlisted support graph, so the
  direct-test ELF is structural sanitizer evidence only. It is not a
  capability-minimal producer boundary and cannot count as closure or
  activation evidence. The compiler-front-end audit must nevertheless prove
  that the direct-test source cannot invoke those retained capabilities.
- The main producer library and both process runners have no sanitizer
  exception. Under ASan and UBSan, provider audits, process tests, and symbol
  inspection must consume explicit sanitizer-reset unsanitized artifacts for
  both original runners. They must never consume the instrumented direct test
  or silently skip the process/link checks.
- Direct and process tests for the later implementation must:
  - prove the exact canonical identity reaches only the shared activation
    barrier;
  - mutate all 61 identity fields independently and prove the producer error
    precedes source validation and activation;
  - cross-reject ordinary/same-run, v1/v2/v3, Raw/telemetry/report, carrier,
    payload, cardinality, schema, roster, configuration, Session, Plan,
    Execution, and both budget substitutions;
  - prove clean source reaches the barrier while dirty, unstamped, malformed,
    uppercase, cross-commit, and forced-unpublishable source fails earlier;
  - reject unknown, abbreviated, duplicate, path, fixture, output, Raw,
    telemetry, report, descriptor, activation, and testing arguments before
    external-state access;
  - poison environment variables, independent Raw, telemetry, report, and
    fixture FIFOs, output paths, and inherited request/response descriptors
    and prove they remain untouched;
  - verify exact status, stdout, stderr, and no-output behavior for every
    process invocation;
  - prove through the structured semantic audit and its live negative
    tripwires that the composite directly tail-delegates through the two
    literal `kSameRun` builders, that production and test-support ASTs and
    callable edges are identical in normal/ASan/UBSan contexts, that the entire
    direct-test AST has no callable acquisition edge in any configuration, and
    that both runner ASTs have only the frozen parsing/reporting/preflight
    surface;
  - prove the direct test has the frozen non-skipping execution attributes, no
    `main`, only non-disabled registrations, a pinned GoogleTest `main`, and
    complete unfiltered execution in normal, ASan, and UBSan;
  - prove the resolved sanitizer compile/codegen contexts contain no opt-out
    and instrument the producer builder/composite and direct-test assertions;
  - prove UBSan uses exact nonrecovering compile/runtime settings across the
    linked direct-test support graph, the audit-only live UB probe terminates
    before its sentinel, and successful ASan/UBSan evidence contains no
    sanitizer diagnostic;
  - prove the target-owned positive and negative semantic-audit stamps are
    requested and consumed in all three test configurations;
  - prove the required and forbidden symbol classes in both process runners
    normally and through the ASan/UBSan transition artifacts; and
  - preserve historical Protocol-v2/roster-v3/Session-v4 behavior, the
    committed ordinary producer boundary, and production-runner absence.
- Keep both operational publications, exact-small snapshot/Oracle, every
  other development cell, heldout, imported, fixed-query, stress,
  aggregation, matrix, decision, campaign, and acquisition paths closed. This
  preflight does not produce Raw, telemetry, report, or any other artifact. It
  is not an operational publication, complete-chain member, development
  observation, Phase 4 claim, or M1 claim.

## Consequences

The repository has a complete contract for authenticating the compiled
identity boundary a future same-run report producer must cross. A later
implementation can prove its authority, source, runfiles, dependency, and
link closure without opening Raw, a telemetry sidecar, a report, generated
case `10100`, or allocator execution.

This contract does not add the preflight implementation, a telemetry or
report producer, a production runner, a publication artifact, or any
acquisition capability. It does not satisfy the complete consuming-publication
chain or authorize either Session-v5 development cell to run.

## Rejected alternatives

- **Parameterize or widen the ordinary producer preflight.** Ordinary has a
  two-artifact surface and an explicit absence-of-telemetry contract. Same-run
  has an atomic three-artifact surface and positive telemetry cardinality.
  Caller selection would make those authorities substitutable.
- **Reuse the strict same-run offline validator.** It accepts artifact paths
  and links canonical input and validation capabilities. A producer preflight
  must authenticate compiled identity without reading any artifact.
- **Reuse the Raw controller identity.** That identity binds the current and
  superseded Raw/telemetry execution surface but not retained-v1 ancestry,
  report authority, complete report shape, or all independent cardinalities.
- **Omit retained-v1 telemetry ancestry.** Protocol v2 directly superseded the
  v1 telemetry authority. Skipping it would make the same-run chain weaker
  than the ordinary producer precedent and the frozen protocol history.
- **Derive sidecar cardinality only from report rows or repetitions.** Raw,
  telemetry, and report cardinalities are independently validated contracts;
  each must be present and independently mutable in the compiled identity.
- **Reuse or hide the historical H=4096 same-run report runner behind the
  barrier.** Its diagnostic, preparer, allocator, telemetry/report-builder,
  serializer, and output capabilities remain Session-v4 acquisition surfaces.
- **Treat generated case `10100` as acquisition-free.** Executing diagnostics
  on a generated case still observes allocator behavior.
- **Add a Protocol-v3 producer substitution or serialized producer artifact.**
  The producer identity is a compiled boundary over already reserved Raw,
  telemetry, and report authorities.
- **Accept the activation error and continue.** The barrier remains terminal
  until a future activation contract proves the complete successor chain in
  one clean stamped commit.
- **Count validators or producer preflights as the complete chain.** They
  authenticate boundaries but do not provide either operational publication
  or the exact-small snapshot/Oracle successor.
