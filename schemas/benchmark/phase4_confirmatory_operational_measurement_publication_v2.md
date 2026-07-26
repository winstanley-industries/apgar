# Phase 4 Confirmatory Operational Measurement Publication v2

This configuration-specific authority binds the frozen ordinary operational
payloads to the H=4096 calibration development cell in Representative Corpus
v2. It supersedes only
`phase4_confirmatory_operational_measurement_publication_v1`. Protocol-v2
authority selection is explicit and out of band; it must never be inferred
from Corpus version, case identity, carrier shape, source commit, or an opaque
matching checksum.

## Frozen payload and development scope

The authority retains, without payload-schema revision:

- ordinary Raw Evidence schema 1 over Raw Wire 1;
- Worker Output v1;
- Operational Profile v1;
- Replay Authority v1;
- Operational Measurement Capture v1; and
- final operational-publication payload schema 1.

The `v2` suffix selects the H=4096 authority binding and final publication
checksum domains only. Confirmatory Decision Protocol v2 JSON, every payload
shape above, and all worker, profile, replay, and capture checksum domains
remain unchanged.

The production authority accepts exactly:

- publication authority
  `phase4_confirmatory_operational_measurement_publication_v2`;
- configuration authority `phase4_confirmatory_corpus_v2_h4096`;
- Confirmatory Decision Protocol v2 checksum `11520586171987743043`;
- canonical algorithm-budget roster v3 checksum `18429170436700418962`;
- explicit Representative Corpus version 2;
- calibration cell `(case_id=10200, requested_pool_size=8)`;
- four preparation workers and 20 Raw repetitions;
- ordinary Raw Evidence schema 1 over Raw Wire 1 under authority
  `phase4_confirmatory_raw_evidence_v2`;
- equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`;
- canonical algorithm-budget checksum `8230401457668518004`;
- paired semantic `budget_checksum=12108149041077564710`;
- one complete H=4096 ordinary Raw publication from one independently supplied
  clean 40-character lowercase commit; and
- one independently executed four-process operational capture from that exact
  source and configuration.

The canonical algorithm-budget checksum authenticates the complete H=4096
canonical algorithm configuration preimage. The paired semantic budget
checksum authenticates the opportunity/configuration identity carried by Raw
arm semantics and the operational replay. Both literal values are required;
one cannot be derived from, aliased to, relabeled as, or substituted for the
other.

The frozen schema-1 `config` remains case 10200, pool eight, four workers, 20
repetitions, setup / prepared / cold caps of `300000000000` ns,
address-space cap `68719476736`, peak-host cap `17179869184`, and corpus limits
`(4096, 100000000, 8589934592, 250000, 100000)` in field order. The validator
requires exact structural equality while independently authenticating both
budget identities.

H=2250, pool four, Same-Run Raw/Wire 2, any Same-Run Decision Telemetry
sidecar, every other development cell, and heldout, imported, fixed-query,
stress, matrix, and decision roles are outside this authority. Capture and
worker scope rejection occurs before fixture access, representative-case
construction, preparer creation, warmup, or replay.

## Execution and pinned authority

`phase4_confirmatory_h4096_operational_capture` launches, in order, measured
baseline, measured reusable candidate, unmeasured baseline replay authority,
and unmeasured candidate replay authority as four distinct exec children. Each
child performs one uninstrumented warmup and one diagnostic replay. Candidate
children create one persistent four-worker preparer before warmup and reuse it
for the published replay.

The H=4096 capture retains Operational Measurement Publication v1's complete
exact-child `wait4`, pidfd, resource-limit, affinity, cgroup,
descendant-reaping, bounded-diagnostic, deadline, and provenance contract. It
also retains the compiled launcher's one-use inherited handshake,
parent-death coupling, default `SIGCHLD` normalization, isolated-bytecode
cleanup, complete adjacent standalone-runfiles traversal, and hermetic
interpreter selection.

The public capture fixes
`phase4_confirmatory_h4096_operational_replay_worker`. The worker is opened,
inode-pinned for execution, and hashed from only the compiled launcher's
adjacent target-specific standalone runfiles tree. Publication independently
resolves and hashes that same bundled production worker. Caller paths, ambient
runfiles, an enclosing Bazel test tree, a case ID, a checksum, or a worker
argument cannot substitute another executable or authority.

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

`phase4_confirmatory_h4096_operational_measurement_validator` must:

1. authenticate Confirmatory Decision Protocol v2, canonical
   algorithm-budget roster v3, and the exact reserved
   `ordinary_operational_measurement` substitution;
2. read the Raw input as a bounded regular file and completely validate
   H=4096 ordinary Raw schema 1/Wire 1 under
   `phase4_confirmatory_raw_evidence_v2`, exact `(10200,8)` scope, both budget
   identities, and the independently supplied clean source commit;
3. only then resolve and hash the pinned H=4096 production worker and read and
   completely validate the bounded regular Operational Measurement Capture v1;
   and
4. only after rebuilding the complete Raw/capture source, configuration,
   process, replay, and semantic join may it open a bounded regular
   publication-validation input or create an output.

No telemetry sidecar is accepted or opened. Capture source and complete
configuration must exactly equal Raw. Canonical Raw repetition zero must be a
successful baseline-first pair. Each measured profile and replay-authority
semantics object must exactly equal its corresponding Raw arm semantics. The
publication binds the H=4096 Raw authority, both input artifact/source
checksums, the pinned worker and capture provenance, both budget identities,
and every existing operational association carried by the schema-1 payload.
Checksum equality never substitutes for complete structural validation.

The final payload keeps `schema_version=1`, `cell_role="calibration"`, and:

- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`; and
- `cell_operational_telemetry_complete=true`.

Raw remains the only allocation-outcome and paired-timing authority. The
operational publication remains diagnostic replay evidence and cannot repair,
reinterpret, or replace Raw.

The final publication binds Raw authority
`phase4_confirmatory_raw_evidence_v2` and uses the domains
`APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2`
and
`APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2`.
Durable output remains canonical, atomically installed without replacement,
and fsynced under the existing v1 publication contract.

## Evidence limits and source ancestry

Raw, capture, bundled worker, publication, and the independently supplied
expected commit must identify one clean stamped 40-character lowercase source
commit. After this implementation is adversarially reviewed and committed,
ordinary H=4096 Raw `(10200,8)`, its sibling per-net report, and this
operational capture/publication must be reacquired from that same clean commit
to form a mutually joinable calibration bundle. Earlier artifact content
cannot be copied, relabeled, rechecksummed, or accepted through a cross-commit
exception.

This authority opens no same-run cell or telemetry sidecar, standalone replay,
fixed-query, stress, aggregation, full-matrix, decision, heldout, or imported
path. One publication is calibration-only, diagnostic, non-standalone,
non-statistical, and incomplete coverage. It proves neither route-pool
completeness, fixed-pool optimality, resource feasibility, final legality,
Phase 4 completion, nor M1 completion.

All stable hashes in this authority are non-cryptographic corruption and
association checks. Arbitrary malicious artifact rewriting with recomputation
of every public field remains outside this trust model and requires an
external signature or platform attestation authority.
