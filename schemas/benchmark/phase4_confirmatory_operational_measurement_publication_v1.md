# Phase 4 Confirmatory Operational Measurement Publication v1

This authority selects the frozen Operational Measurement Capture v1, Worker
Output v1, Operational Profile v1, and Replay Authority v1 formats for a new
domain-separated final publication in the Representative Corpus v2
confirmatory campaign. Selection is out of band and must never be inferred
from a case identifier.

## Closed development scope

The ordinary confirmatory publisher accepts only:

- explicit Representative Corpus version 2;
- Raw Evidence schema 1 carried on Wire 1;
- calibration cell `(case_id=10200, requested_pool_size=4)`;
- four preparation workers and the frozen canonical cell budget;
- a complete 20-repetition Raw publication from one independently supplied
  clean 40-character commit; and
- one independently executed four-process operational capture from that same
  exact source and configuration.

It rejects Same-Run Raw v2 and any Same-Run Decision Telemetry companion.
Every other development, heldout, imported, fixed-query, or stress case/pool
is outside this slice. Scope rejection in the capture and worker occurs before
fixture access, representative-case construction, preparer creation, warmup,
or replay.

## Execution and authority

The capture launches measured baseline, measured reusable candidate,
unmeasured baseline replay authority, and unmeasured candidate replay authority
as four distinct exec children in that order. Each performs one uninstrumented
warmup followed by one published diagnostic replay. Candidate children create
one persistent four-worker preparer before warmup and reuse it for the
published replay.

The parent retains the complete containment, pinned-worker, exact-child
`wait4`, resource-limit, affinity, cgroup, descendant-reaping, and provenance
contract of Operational Measurement Publication v1. Numeric CPU, peak-host,
and outer-wall measurements occur only on measured children. Authority
children carry the typed unmeasured-resource reason and recompute the complete
live result preimage. Both replays for an arm must have identical complete
Corpus v2 semantics; candidate witnesses must match field-for-field.
Worker resolution is restricted to the Bazel runfiles tree containing the
confirmatory authority itself; compiled public launchers clear ambient
runfiles variables before entering Python, and caller working-directory
fallbacks are forbidden. An inner Python authority must consume the one-use
file-descriptor handshake established by that compiled launcher before it may
parse arguments or emit an artifact; direct inner-target execution is not a
publication path. In a containing Bazel test, the launcher accepts the
declared enclosing runfiles tree only after the invoked path resolves to the
launcher itself and the inner target resolves to its canonical Bazel output.
The entire selected runfiles root, including every external repository
subtree, must be traversed completely; traversal failure rejects the tree, and
a rejected enclosing tree must not fall back to an adjacent standalone tree.
The delegated Python authority must arm a parent-death relationship and close
the fork race before exec so terminating the exact public launcher cannot leave
capture or publication work running.
The launcher must establish default `SIGCHLD` reaping semantics before
delegation rather than inherit an ignored child signal that could permit
authority output before an `ECHILD` launcher failure.
Its isolated bytecode-cache path must be absent before delegation so launcher
cancellation cannot leak the path.
Capture and publication independently hash the bundled worker. The test worker
always retains its actual unstamped or dirty build envelope, and only the
separately named test publisher accepts it. An unstamped test build with no
embedded commit identity publishes the all-zero 40-character unavailable
sentinel; it never relabels that envelope with the caller-supplied commit
association. A test worker determines whether it is a publishable clean build
solely from embedded source state; caller commit mismatch cannot downgrade
that state. A publishable clean test build fails before replay and emits no
artifact.

Raw remains the sole outcome and paired-timing authority. These operational
replays are diagnostic and cannot be substituted into Raw.

## Input ordering and publication

The publication CLI reads the bounded regular Raw file first and completely
validates it under the confirmatory Raw authority and expected clean commit. It
then enforces Wire 1 and the exact `(10200,4)` scope before opening the bounded
regular capture. Only after complete capture validation and the Raw/capture
configuration, source, and semantic join may it open a bounded regular
validation publication or create an output.

Capture source/configuration must exactly equal Raw. Canonical Raw repetition
zero must be a successful baseline-first pair, and each measured profile and
authority semantics object must exactly equal the corresponding Raw arm
semantics. The publication carries `cell_role="calibration"` and the fixed
flags:

- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`; and
- `cell_operational_telemetry_complete=true`.

The final publication must not claim or carry a legacy Operational Projection
v1 identity: that projection authenticates Corpus v1 Raw. Instead, the
publication directly binds the complete confirmatory Raw and capture inputs,
explicit Corpus 2/Wire 1 authority, and uses the domains
`APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V1`
and
`APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V1`.
Durable output is canonical, atomically installed without replacement, and
fsynced under the existing publication contract.

One publication proves neither complete matrix coverage nor the Phase 4
decision.

All stable hashes in this authority are non-cryptographic corruption and
association checks. Arbitrary malicious artifact rewriting with recomputation
of every public field is outside this trust model and would require an external
signature or platform attestation authority.
