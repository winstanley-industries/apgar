# Phase 4 Confirmatory Same-Run Operational Measurement Publication v1

This authority selects the frozen Operational Measurement Capture v1, Worker
Output v1, Operational Profile v1, and Replay Authority v1 formats for a
domain-separated same-run publication in the Representative Corpus v2
confirmatory campaign. Corpus, Raw wire, telemetry wire, and publication
authority selection are explicit and must never be inferred from a case
identifier.

## Closed development scope

The publisher accepts only:

- explicit Representative Corpus version 2;
- Same-Run Raw Evidence schema 2 carried on Raw Wire 2;
- Same-Run Decision Telemetry schema 1 carried on Telemetry Wire 2;
- exact cell `(case_id=10100, requested_pool_size=4)`;
- four preparation workers and the frozen canonical cell budget;
- a complete 20-repetition Raw publication and its fully joined telemetry
  companion from one independently supplied clean 40-character commit; and
- one independently executed four-process operational capture from that exact
  source and configuration.

Ordinary Raw/Wire 1, absent or foreign telemetry, and every other development,
heldout, imported, fixed-query, or stress case/pool are outside this slice.
Capture and worker scope rejection occurs before fixture access,
representative-case construction, preparer creation, warmup, or replay.

## Execution and authority

The capture launches measured baseline, measured reusable candidate,
unmeasured baseline replay authority, and unmeasured candidate replay authority
as four distinct exec children in that order. Each performs one
uninstrumented warmup followed by one published diagnostic replay. Candidate
children create one persistent four-worker preparer before warmup and reuse it
for the published replay.

The parent retains the complete containment, pinned-worker, exact-child
`wait4`, resource-limit, affinity, cgroup, descendant-reaping, and provenance
contract of Operational Measurement Publication v1. Numeric CPU, peak-host,
and outer-wall measurements occur only on measured children. Authority
children carry the typed unmeasured-resource reason and recompute the complete
live result preimage. Both replays for an arm must have identical complete
Corpus v2 semantics; candidate witnesses must match field-for-field.

Worker resolution is restricted to the compiled public launcher's adjacent,
target-specific standalone Bazel runfiles tree. Compiled public launchers
clear ambient runfiles variables before entering isolated Python, and caller
working-directory fallbacks are forbidden. An inner Python authority must
consume the one-use file-descriptor handshake established by that compiled
launcher before it may parse arguments or emit an artifact; direct
inner-target execution is not a publication path. In a containing Bazel test,
an invocation path may traverse an enclosing runfiles symlink only when it
resolves to the canonical launcher; the enclosing tree is never an
execution-authority candidate. The entire standalone runfiles root, including
every external repository subtree, must be traversed completely before
delegation. Direct stage-one Python execution must disable `site`
initialization until the rules_python bootstrap establishes the authenticated
standalone root; stage-two site initialization may run only after that
selection.
The delegated Python authority must arm a parent-death relationship and close
the fork race before exec so terminating the exact public launcher cannot
leave capture or publication work running. The launcher must establish default
`SIGCHLD` reaping semantics before delegation rather than inherit an ignored
child signal that could permit authority output before an `ECHILD` launcher
failure. Its isolated bytecode-cache path must be absent before delegation so
launcher cancellation cannot leak the path. Capture and publication
independently hash the separately named same-run worker. The test worker always
retains its actual unstamped or dirty build envelope, and only the separately
named test publisher accepts it. An unstamped test build without an embedded
commit identity publishes the all-zero 40-character unavailable sentinel; it
never adopts the caller-supplied commit association.
A test worker determines whether it is a publishable clean build solely from
embedded source state; caller commit mismatch cannot downgrade that state. A
publishable clean test build fails before replay and emits no artifact.

Raw remains the sole outcome and paired-timing authority. The same-run
telemetry companion remains the sole exact-rejection guardrail authority.
Operational replays are diagnostic and cannot be substituted for either.

## Input ordering and publication

The publication CLI opens the bounded regular Raw file first and completely
validates it under the confirmatory Same-Run Raw authority and expected source.
It then enforces Raw Wire 2 and the exact `(10100,4)` scope before opening the
bounded regular telemetry companion. Only after complete telemetry validation
and its Raw join may the CLI resolve and hash the bundled worker and open the
bounded regular capture. Only after complete capture validation and the
Raw/telemetry/capture configuration, source, and semantic join may it open a
bounded regular validation publication or create an output.

Capture source and configuration must exactly equal Raw. Canonical Raw
repetition zero must be a successful baseline-first pair, and each measured
profile and authority semantics object must exactly equal the corresponding
Raw arm semantics. The publication carries `cell_role="exact"` and the fixed
flags:

- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`; and
- `cell_operational_telemetry_complete=true`.

The `same_run_telemetry_binding` records its authority, telemetry and Raw wire
versions, complete artifact/source checksums, and
`exact_rejection_guardrail_passed`. A structurally valid `false` guardrail is
authentic negative evidence: it remains publishable here and may fail only the
later aggregate decision. The operational publisher must not erase, convert,
or pre-judge it.

The publication must not claim or carry a legacy Operational Projection v1
identity. It directly binds the complete confirmatory Raw, telemetry, and
capture inputs and uses the domains
`APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V1`
and
`APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V1`.
Durable output is canonical, atomically installed without replacement, and
fsynced under the existing publication contract.

One publication proves neither complete matrix coverage nor the Phase 4
decision.

All stable hashes in this authority are non-cryptographic corruption and
association checks. Arbitrary malicious artifact rewriting with recomputation
of every public field is outside this trust model and would require an external
signature or platform attestation authority.
