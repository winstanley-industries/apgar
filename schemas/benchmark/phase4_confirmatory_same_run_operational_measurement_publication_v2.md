# Phase 4 Confirmatory Same-Run Operational Measurement Publication v2

This configuration-specific authority binds the frozen operational payloads to
the H=4096 exact development cell in Representative Corpus v2. It supersedes
only
`phase4_confirmatory_same_run_operational_measurement_publication_v1`.
Protocol-v2 authority selection is explicit and out of band; it must never be
inferred from Corpus version, case identity, carrier shape, source commit, or
an opaque budget checksum.

## Frozen payload and development scope

The authority retains, without payload-schema revision:

- Same-Run Raw Evidence schema 2 over Raw Wire 2;
- Same-Run Decision Telemetry schema 1 over Telemetry Wire 2;
- Worker Output v1;
- Operational Profile v1;
- Replay Authority v1;
- Operational Measurement Capture v1; and
- final operational-publication payload schema 1.

The `v2` suffix selects the H=4096 authority binding and final publication
checksum domains only. Confirmatory Decision Protocol v2 JSON, every payload
shape above, and all capture, worker, profile, and replay checksum domains
remain unchanged.

The production authority accepts exactly:

- configuration authority `phase4_confirmatory_corpus_v2_h4096`;
- explicit Representative Corpus version 2;
- exact cell `(case_id=10100, requested_pool_size=4)`;
- four preparation workers and 20 Raw repetitions;
- equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`;
- canonical algorithm-budget checksum `8829615204625848656`;
- one complete H=4096 Raw-v2 publication and its fully joined H=4096 same-run
  telemetry companion from one independently supplied clean 40-character
  commit; and
- one independently executed four-process operational capture from that exact
  source and configuration.

Protocol v2 and canonical algorithm-budget roster v3 are authenticated before
this scope can open. H=2250, ordinary Raw/Wire 1, absent or foreign telemetry,
every other development cell, and heldout, imported, fixed-query, stress,
matrix, and decision roles are outside this authority. Capture and worker
scope rejection occurs before fixture access, representative-case
construction, preparer creation, warmup, or replay.

## Execution and pinned authority

`phase4_confirmatory_h4096_same_run_operational_capture` launches, in order,
measured baseline, measured reusable candidate, unmeasured baseline replay
authority, and unmeasured candidate replay authority as four distinct exec
children. Each child performs one uninstrumented warmup and one diagnostic
replay. Candidate children create one persistent four-worker preparer before
warmup and reuse it for the published replay.

The H=4096 capture retains Operational Measurement Publication v1's complete
exact-child `wait4`, pidfd, resource-limit, affinity, cgroup,
descendant-reaping, and provenance contract. It also retains the compiled
launcher's one-use inherited handshake, parent-death coupling, default
`SIGCHLD` normalization, isolated-bytecode cleanup, complete adjacent
standalone-runfiles traversal, and hermetic interpreter selection.

The public capture fixes
`phase4_confirmatory_h4096_same_run_operational_replay_worker`. The worker is
opened, inode-pinned for execution, and hashed from only the compiled
launcher's adjacent target-specific standalone runfiles tree. Publication
independently resolves and hashes that same bundled production worker. Caller
paths, ambient runfiles, an enclosing Bazel test tree, a case ID, a checksum,
or a worker argument cannot substitute another executable or authority.

The separately compiled test worker and test publication path retain their
actual nonpublishable source envelope and the existing unstamped-source
sentinel rules. Embedded source state alone decides publishability. A clean
publishable test build fails before replay and emits no artifact.

Measured children alone publish numeric CPU, peak-host, and controller
fork-to-reap wall observations. Authority children retain the typed unmeasured
resource reason and recompute the complete live-result preimage. Both replays
for an arm must have identical complete Corpus-v2/H=4096 semantics, and the
candidate witnesses must match field-for-field.

## Fail-closed input ordering and publication

`phase4_confirmatory_h4096_same_run_operational_measurement_validator` must:

1. read the Raw input as a bounded regular file and completely validate
   H=4096 Same-Run Raw schema 2/Wire 2, canonical budget roster v3, exact
   `(10100,4)` scope, and the independently supplied clean source commit;
2. only then read the bounded regular telemetry companion and completely join
   its source, Raw cell, environment, controller, process, dispatch, pair, arm,
   outcome, six-net roster, column partition, artifact, and source-envelope
   identities;
3. only then resolve and hash the pinned H=4096 production worker and read and
   completely validate the bounded regular Operational Measurement Capture v1;
   and
4. only after rebuilding the complete Raw/telemetry/capture source,
   configuration, process, replay, and semantic join may it open a bounded
   regular publication-validation input or create an output.

Capture source and complete configuration must exactly equal Raw. Canonical
Raw repetition zero must be a successful baseline-first pair. Each measured
profile and replay-authority semantics object must exactly equal its
corresponding Raw arm semantics. The publication binds H=4096 Raw and telemetry
authorities, all three input artifact/source checksums, the pinned worker and
capture provenance, and every existing operational association carried by the
v1 payload.

The final payload keeps `schema_version=1`, `cell_role="exact"`, and:

- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`; and
- `cell_operational_telemetry_complete=true`.

Raw remains the only allocation-outcome and paired-timing authority. Same-run
telemetry remains the only exact-rejection guardrail authority. A structurally
valid `exact_rejection_guardrail_passed=false` is authentic negative evidence:
it remains publishable and may fail only a later complete aggregate decision.
The operational join must not erase, convert, offset, or pre-judge it.

The final publication uses the domains
`APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2`
and
`APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2`.
Durable output remains canonical, atomically installed without replacement,
and fsynced under the existing v1 publication contract.

## Evidence limits and source ancestry

After this implementation is adversarially reviewed and committed, exact Raw,
its telemetry sidecar, its sibling per-net report, and this operational
capture/publication must be reacquired from that same clean commit to form a
mutually joinable development bundle. Earlier artifact content cannot be
copied, relabeled, rechecksummed, or accepted through a cross-commit exception.

This authority opens no ordinary H=4096 operational cell, standalone replay,
snapshot, exact-small Oracle, fixed-query, stress, aggregation, full-matrix,
decision, heldout, or imported path. One publication is diagnostic,
non-standalone, non-statistical, and incomplete coverage. It proves neither
fixed-pool optimality nor route-pool completeness, resource feasibility, final
legality, or Phase 4 completion.

All stable hashes in this authority are non-cryptographic corruption and
association checks. Arbitrary malicious artifact rewriting with recomputation
of every public field remains outside this trust model and requires an
external signature or platform attestation authority.
