# Phase 4 Confirmatory Exact-Small Oracle Publication v2

This configuration-specific authority binds the frozen exact-small payloads to
the H=4096 exact development cell in Representative Corpus v2. It supersedes
only `phase4_confirmatory_exact_small_oracle_v1`. Protocol-v2 authority
selection is explicit and out of band; it must never be inferred from Corpus
version, case identity, carrier shape, source commit, or an opaque matching
checksum.

The reviewed pre-authority development result has production objective
`(6,1,159000)`. That observation is motivation only. This contract requires
fresh same-commit inputs, independent candidate replay, complete fixed-pool
enumeration, and exact objective equality before it can emit an artifact.

## Frozen payloads and exact development scope

The authority retains, without payload-schema revision:

- Same-Run Raw Evidence schema 2 over Raw Wire 2;
- Same-Run Decision Telemetry schema 1 over Telemetry Wire 2;
- Per-Net Report Artifact schema 1 over Raw Wire 2;
- Exact-Small Final-Pool Snapshot schema 1, including its frozen DTO, bounds,
  canonical JSON order, artifact checksum domain, and source-envelope checksum
  domain; and
- final Oracle Artifact payload schema 1 and its fixed JSON key order.

The `v2` suffix selects the H=4096 publication authority, binding values, and
final Oracle checksum domains only. Confirmatory Decision Protocol v2 JSON,
the input payloads above, Snapshot v1 checksum domains, and all H=2250
authorities remain unchanged.

The production authority accepts exactly:

- publication authority `phase4_confirmatory_exact_small_oracle_v2`;
- configuration authority `phase4_confirmatory_corpus_v2_h4096`;
- explicit Representative Corpus version 2 and corpus checksum
  `4182833841936446798`;
- exact cell `(case_id=10100, requested_pool_size=4)`;
- four preparation workers and 20 Raw repetitions;
- Same-Run Raw Evidence schema 2 over Raw Wire 2;
- Same-Run Decision Telemetry schema 1 over Telemetry Wire 2;
- equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`;
- roster-v3 canonical algorithm-budget cell checksum
  `8829615204625848656`;
- Raw/Snapshot paired semantic `budget_checksum`
  `5851813264366095594`; and
- one independently supplied clean 40-character lowercase source commit shared
  by every external input and the publisher.

The two budget identities are independent. The roster-v3 cell checksum hashes
the complete H=4096 canonical algorithm configuration preimage; the paired
semantic budget checksum is the opportunity/configuration identity carried by
Raw candidate semantics and Snapshot v1. A validator must positively require
both literal values and must not derive, alias, relabel, or substitute either
one for the other.

The frozen Artifact-v1 `config` object remains schema 1, case 10100, pool four,
four workers, 20 repetitions, setup / prepared / cold caps of
`300000000000` ns, address-space cap `68719476736`, peak-host cap
`17179869184`, and corpus limits `(4096, 100000000, 8589934592, 250000,
100000)` in field order. The standalone validator requires exact structural
equality to those values while separately authenticating the complete H=4096
canonical budget through the joined inputs and snapshot. No new config field
or payload-schema revision is implied.

H=2250, Raw Wire 1, ordinary evidence, absent or foreign telemetry, another
case or pool, a rechecksummed configuration alias, and every heldout, imported,
fixed-query, stress, matrix, or decision role are outside this authority.

## Bounded inputs and fail-closed authority order

Raw input is capped at 64 MiB, the telemetry companion at 32 MiB, the per-net
report at 32 MiB, and the snapshot at 64 MiB. Every input must be a bounded
regular file containing one strict canonical UTF-8 JSON object followed by
exactly one LF. Duplicate or reordered keys, non-finite numbers, wrong scalar
types, excessive nesting, trailing bytes, FIFO/device inputs, and oversize
files fail the invocation without an output prefix.

`phase4_confirmatory_h4096_exact_small_oracle_validator` enforces this exact
open, validation, and execution order:

1. before runfiles authentication, Python delegation, or any input open, its
   compiled launcher requires a clean generated source stamp and exactly one
   well-formed `--expected-commit` equal to the embedded build commit;
2. open and completely authenticate H=4096 Same-Run Raw under Corpus 2,
   Confirmatory Decision Protocol v2, and canonical algorithm-budget roster
   v3;
3. enforce Raw Evidence schema 2, Raw Wire 2, exact cell `(10100,4)`,
   equal-arm `present=1,history=4096`, canonical algorithm-budget cell checksum
   `8829615204625848656`, paired semantic budget checksum
   `5851813264366095594`, and the independently supplied clean commit;
4. only then open and completely join Same-Run Decision Telemetry schema 1 /
   Telemetry Wire 2;
5. only after that join succeeds, open and structurally join the Corpus-v2
   H=4096 Wire-2 per-net report;
6. only after the report join succeeds, open and completely validate the
   bounded regular Snapshot v1;
7. only after all four input authorities join, resolve and invoke the fixed
   H=4096 replay-v3 helper; and
8. only after replay succeeds, enumerate the complete bounded Cartesian
   product.

Missing, duplicate, abbreviated, malformed, unstamped, dirty, or
built-commit-mismatched launcher identity fails without input access. The
compiled boundary rejects abbreviated expected-commit prefixes and the Python
publisher disables long-option abbreviation, so an alternate spelling cannot
replace the commit checked before delegation.
Fixed-source clean, dirty, and unstamped test launchers are test-only,
preflight-only authorities. Their lightweight inner targets stop immediately
after handshake and argument parsing, depend on neither this validator nor the
replay helper, and cannot open an input, replay, enumerate, construct, or emit
an Oracle Artifact. They do not relax or replace the production source stamp.

The join requires exact equality of clean source, complete canonical
configuration, corpus and built-case identities, cell plan, Raw artifact and
source envelope, repetition-zero pair and arm references, complete candidate
arm semantics, candidate telemetry, six-net EntityRef roster, every per-net
final pool and selection field, report artifact and source envelope, candidate
session and pool manifests, capacity identity, both budget identities, and
production outcome. Each budget checksum must match its own producer and
consumer fields. Checksum equality never substitutes for structural
validation.

Raw remains allocation-outcome and paired-timing authority. Same-run telemetry
remains exact-rejection guardrail authority. The report and snapshot remain
diagnostic authorities joined independently to Raw.

## H=4096 snapshot authority

`phase4_confirmatory_h4096_exact_small_snapshot_runner` is a separately
compiled fixtureless production authority. It accepts no fixture path and
declares no board fixture or fixture runfile capability because case 10100 is
generated. It requires explicit Corpus 2, Raw Evidence schema 2, Raw Wire 2,
exact `(10100,4)`, four workers, 20 repetitions, repetition zero in
baseline-first order, complete nonzero Raw references, nonzero Raw/report
artifact and source-envelope checksums, equal-arm
`present=1,history=4096`, canonical algorithm-budget cell checksum
`8829615204625848656`, and paired semantic
`budget_checksum=5851813264366095594`.

All scope, association, source, and H=4096 canonical-spec rejection occurs
before representative-case construction, preparer creation, or candidate
execution. The producer uses only the explicit H=4096 Corpus-v2 canonical-spec,
candidate-snapshot, case, roster, cell-plan, budget, and semantic-validation
entry points.

The production artifact keeps Exact-Small Snapshot v1's complete payload
shape, component bounds, canonical key order, artifact checksum domain, and
source-envelope checksum domain. Its existing `budget_checksum` field must
equal `5851813264366095594`; complete candidate semantics, Raw/report
associations, capacity model, pools, and production selections bind the H=4096
execution. The separate canonical algorithm-budget cell checksum must equal
`8829615204625848656` at the producer boundary but is not a renamed Snapshot
field. A separately selected H=4096 builder and validator are mandatory even
though the payload schema and domains do not change.

`phase4_confirmatory_h4096_exact_small_snapshot_test_runner` is permanently
preflight-only. It has no fixture capability and cannot construct a
representative case, create a preparer, execute a candidate arm, serialize a
snapshot, or emit evidence under any source-state or testing escape.

Raw and report fields within Snapshot v1 are claimed associations until the
publication validator loads and joins their external documents. H=2250,
Corpus-v1, Raw-v1/Wire-1, a foreign or rechecksummed report, another case, or
another pool is invalid. Existing H=2250 snapshot producers and validators
remain unchanged and continue to reject the H=4096 authority.

## Private replay wire v3

After the four external inputs join, the validator invokes only the fixed Bazel
runfile
`phase4_confirmatory_h4096_exact_small_candidate_admission_replay`. The helper
accepts at most 64 MiB on stdin, emits no stdout, and requires exact EOF.

Its private little-endian replay wire v3 extends the existing bounded candidate
roster with a fixed authority header. Before corpus construction or candidate
traversal, the helper must positively require:

- the existing eight-byte exact-replay magic;
- replay wire version 3;
- Corpus version 2;
- case ID 10100 and requested pool size 4;
- `present_step_per_overuse_unit=1`;
- `history_step_per_overuse_unit=4096`;
- canonical algorithm-budget cell checksum `8829615204625848656`;
- paired semantic budget checksum `5851813264366095594`; and
- the exact frozen Corpus-v2 limits joined from the publication.

The compiled target independently rebuilds the H=4096 canonical cell spec and
requires the same price fields and both budget identities before rebuilding
only Corpus-v2 case 10100. It independently pins each checksum to its proper
preimage and must not derive or accept one from the other. Caller bytes do not
select the corpus, configuration, case, helper, or executable authority. Wire
v1 remains the legacy Corpus-v1 format, wire v2 remains the H=2250 Corpus-v2
format, and neither may be accepted by this target.

The remaining pool and candidate encoding, component limits, six-pool roster,
at most six candidates per pool, 36-candidate total, candidate reconstruction,
source-private `ValidateCandidatePayloadWithoutProducerEvidence` replay, and
intrinsic-cost comparison retain the bounded exact-small replay contract. A
trailing byte, malformed row, inconsistent roster, failed candidate
reconstruction, or exceeded bound fails the helper and therefore the complete
publication.

The public publisher remains a compiled launcher that authenticates its
canonical executable, complete adjacent target-specific standalone runfiles
tree, and hermetic Python interpreter before delegating to a fixed private
Python target through a one-use parent-bound handshake. An enclosing Bazel
runfiles tree may contain an invocation symlink but is never a Python/data
authority. The inner target cannot run directly. Replay resolves only from the
authenticated standalone `.runfiles/_main` tree; ambient runfiles directories,
manifests, caller paths, and helper arguments cannot substitute another
executable. Direct stage-one Python execution disables `site` initialization
until the rules_python bootstrap establishes that tree, and stage-two site
initialization occurs only after that selection.

## Independent fixed-pool proof

After replay succeeds, the validator reuses every strict shape, component,
product, canonical-pool, candidate reconstruction, resource-capacity,
selection, and outcome check in Exact-Small Snapshot v1. It preflights the
exact six pool sizes before candidate traversal and rejects a Cartesian product
greater than the case bound or 4096. Empty pools contribute one explicit
no-candidate sentinel; no prefix or truncated pool is admissible.

The independent oracle enumerates every world in
`prod(max(1, pool.candidates.size()))`, expands compressed spans to atomic
resources, applies the serialized binary capacity model, and ranks worlds only
by:

1. maximum selected-net count;
2. minimum total overuse units; and
3. minimum total unweighted intrinsic base cost.

Overused-resource count is independently recomputed for the production world
and canonical witness but never ranks worlds. Equal objective values establish
production optimality even when witnesses differ. The artifact retains the
number of equal-objective optima and the lowest ordered candidate-ID witness.

Production and optimum objective triples must be exactly equal before artifact
construction. A mismatch fails without stdout and supplies no
`exact_small_oracle_complete=true` authority. It leaves the confirmatory
campaign incomplete and is never recorded as an allocator loss.

The reviewed pre-authority production objective is `(6,1,159000)`, but the
publisher must derive it from freshly reacquired, same-commit inputs and
freshly enumerate the optimum. No test fixture, expected value, earlier
H=2250 optimum, or earlier H=4096 document may substitute for that proof.

One overused resource and one total overuse unit are compatible with exact
fixed-pool optimality when no captured world has a better objective. Equality
therefore does not establish resource feasibility, combined-board legality, or
global optimality outside the frozen pools.

A structurally valid `exact_rejection_guardrail_passed=false` is copied into
the binding and remains publishable authentic negative evidence when every
fixed-pool proof condition otherwise completes. It is not a malformed Oracle,
does not alter enumeration, and may fail only a later complete aggregate
decision.

## Oracle Artifact payload schema 1 under authority v2

Output is one compact canonical UTF-8 JSON object at most 1 MiB with exactly one
trailing LF. The final payload remains schema 1 with this unchanged fixed
top-level key order:

```text
source_commit, source_stamped, source_tree_dirty, source_envelope_checksum,
schema_version, campaign_id, cell_role,
eligible_input_to_phase4_aggregation, standalone_decision_eligible,
statistical_timing_eligible, coverage_complete, exact_small_oracle_complete,
config, corpus_version, corpus_checksum, raw_binding,
same_run_telemetry_binding, per_net_report_binding, snapshot_binding,
candidate_semantic_checksum, cartesian_product, production_objective,
optimum_objective, production_overused_resource_count,
canonical_witness_overused_resource_count, production_is_optimal,
optimum_count, canonical_witness, artifact_checksum
```

The fixed flags and identities are:

- `schema_version=1`;
- `campaign_id="phase4_confirmatory_corpus_v2"`;
- `cell_role="exact"`;
- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`;
- `exact_small_oracle_complete=true`;
- `corpus_version=2`; and
- `corpus_checksum=4182833841936446798`.

`exact_small_oracle_complete=true` means only that this one cell has a complete
admitted fixed-pool proof. It does not mean the cell is resource-feasible or
that exact-cell, matrix, campaign, Phase 4, or M1 coverage is complete.

`raw_binding` retains fixed order:

```text
authority, raw_evidence_schema_version, wire_schema_version,
cell_plan_checksum, artifact_checksum, source_envelope_checksum
```

Its authority is `phase4_confirmatory_same_run_raw_evidence_v2`, with Raw
Evidence schema 2 and Wire 2.

`same_run_telemetry_binding` retains fixed order:

```text
authority, schema_version, raw_evidence_schema_version,
raw_wire_schema_version, telemetry_wire_schema_version,
raw_artifact_checksum, raw_source_envelope_checksum, artifact_checksum,
source_envelope_checksum, exact_rejection_guardrail_passed
```

Its authority is
`phase4_confirmatory_same_run_decision_telemetry_v2`, with telemetry schema 1
and Raw/telemetry Wire 2.

`per_net_report_binding` retains fixed order:

```text
authority, schema_version, corpus_version, raw_wire_schema_version,
raw_artifact_checksum, raw_source_envelope_checksum, artifact_checksum,
source_envelope_checksum
```

Its authority is
`phase4_confirmatory_same_run_per_net_report_publication_join_v2`, with report
schema 1, Corpus 2, and Raw Wire 2.

`snapshot_binding` retains fixed order:

```text
authority, schema_version, corpus_version, raw_artifact_checksum,
raw_source_envelope_checksum, per_net_report_artifact_checksum,
per_net_report_source_envelope_checksum, artifact_checksum,
source_envelope_checksum
```

Its authority remains `phase4_exact_small_snapshot_v1`, with snapshot schema 1
and explicit Corpus 2. The H=4096 builder is selected out of band; no Snapshot
v2 authority or checksum domain is minted.

Each objective retains fixed order `selected_net_count`,
`total_overuse_units`, `total_intrinsic_base_cost`. The production and optimum
objectives must be equal, `production_is_optimal` is literal `true`, and
`optimum_count` is within the nonzero Cartesian product. The canonical witness
contains exactly six strictly ordered Corpus-v2 net EntityRefs, each followed
by a candidate ID or `null`; selected IDs are unique and reproduce the optimum
selected-net count.

`artifact_checksum` uses Board IR v1 FNV-1a under
`APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-ARTIFACT-V2`. It hashes the
compact canonical JSON object containing, in fixed order, every field from
`schema_version` through `canonical_witness`. Source stamping and both
top-level checksums are excluded from that preimage.

`source_envelope_checksum` uses
`APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-SOURCE-V2` over source commit,
stamp state, dirty state, and artifact checksum. Every bound external source
envelope must also be independently derivable from that same clean source and
its bound artifact checksum. These stable hashes are deterministic association
and corruption checks, not cryptographic signatures.

## Evidence ancestry and decision boundary

After this implementation is adversarially reviewed and committed, exact Raw,
its telemetry companion, its sibling per-net report, and Snapshot v1 must be
reacquired from that exact clean commit before replay and enumeration. Earlier
content cannot be copied, relabeled, rechecksummed, or accepted through a
cross-commit exception. The sibling operational publication is not an Oracle
input, but a mutually joinable full exact-cell development bundle must
reacquire it from the same commit.

This publication proves exact optimality only inside the captured final
candidate pools. It is not Raw outcome or paired-timing authority, not
statistical timing evidence, not proof that the pools contain every legal
route, not resource feasibility, not combined-board legalization, and not a
standalone decision.

Existing H=2250 snapshot, replay, Oracle, and Protocol-v1 paths remain
unchanged. Confirmatory Decision Protocol v2 JSON remains byte-for-byte
unchanged. Exact cases 10101 and 10102, every other development cell, heldout
and imported cases, fixed-query, stress, complete aggregation, campaign
execution, the Phase 4 decision, Phase 4 completion, and M1 completion remain
closed.
