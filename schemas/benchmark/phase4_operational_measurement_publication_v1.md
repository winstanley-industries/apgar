# Phase 4 Operational Measurement Publication v1

This specification defines the diagnostic capture and final publication that
supply the operational telemetry and reproducibility provenance required by
the frozen Phase 4 completion protocol. It does not replace or reinterpret Raw
Evidence, Same-Run Decision Telemetry, or Per-Net Report authority. Protocol v4
uses this publication as the required operational artifact for every successful
cell; the join still reconstructs and binds the superseded Operational
Projection v1/v2 semantics and artifact identity.

## Execution boundary

One cell capture launches exactly four distinct exec children, in this order:

1. measured sequential baseline;
2. measured reusable-candidate contender;
3. unmeasured sequential-baseline replay authority; and
4. unmeasured reusable-candidate replay authority.

Each child constructs canonical repetition zero in baseline-first order,
performs one uninstrumented warmup, and then performs exactly one published
replay. Candidate children create one persistent preparer before warmup and
reuse it for the published replay. The measured and authority executions are
distinct diagnostic replays; neither is Raw decision authority.

The parent requires default `SIGCHLD` semantics, opens a pidfd for the exact
child, authenticates a process-instance identity from controller identity,
PID, dispatch ordinal, and `/proc/<pid>/stat` start ticks, and uses exact-child
`wait4`. It also requires an empty direct-child roster, temporarily becomes a
Linux child subreaper, and enumerates every adopted descendant. Descendants are
terminated through their own pidfds, exactly reaped under a bounded cleanup,
and make the capture fail even if they changed session/process group and closed
all output descriptors. The subreaper state is restored only after containment.
If exact cleanup cannot prove an empty direct-child roster, the capture fails,
the subreaper state remains enabled, and the controller exits without another
dispatch or publication.
Before dispatch it resolves, opens, hashes, and records one executable
worker descriptor; every child executes that descriptor through
`/proc/self/fd`, so pathname replacement cannot select different worker bytes.
The bytes are rehashed after all four runs. Timeout or oversized-output
termination sends `SIGKILL` through the pidfd and to the isolated process
group. Every child is rejected when its exact reap observation exceeds the
absolute setup-plus-twice-cold deadline or its `ru_maxrss` exceeds the peak
cap. Any signal, nonzero exit, timeout, stderr byte, malformed stdout, missing
resource observation, limit mismatch, or later validation failure invalidates
the capture. The controller snapshots its affinity, cgroup namespace/mount
identity, unified path, and visible controls before and after every dispatch;
the exact child must match the controller affinity and unified path both after
fork and immediately before exact reap. Any observed drift rejects the cell.

The measured process observation contains:

- controller, dispatch, and process-instance identities;
- exact configured address-space limit;
- controller-monotonic fork-to-exact-`wait4`-reap outer nanoseconds;
- child user, system, and closed total CPU nanoseconds from `wait4`;
- Linux `ru_maxrss` converted from KiB to bytes;
- raw wait status, zero exit/signal values, watchdog state, isolation state;
  and
- the exact measurement-scope string.

The two authority process observations carry the same identity, limit, exit,
watchdog, and isolation fields. Their resource field is exactly
`not_used/unmeasured_full_preimage_authority_replay`; it contains no numeric
CPU, RSS, or wall value.

## Worker payloads and replay authority

Worker Output v1 is canonical JSON with schema, kind, exact source envelope,
compiler identity, payload, artifact checksum, and source-envelope checksum.
Kind `0` is a measured Operational Profile v1. Kind `1` is an unmeasured Replay
Authority v1.

Replay Authority v1 contains:

- complete arm semantics;
- candidate-preparer lifecycle, or an all-zero baseline lifecycle;
- the session checksum recomputed while the complete live result preimage still
  exists;
- a candidate replay witness, or JSON null for the baseline; and
- the authority checksum.

Baseline recomputation covers its config, batch/workload/capacity identities,
terminal reason, counters, complete columns and sweeps, final pools and world,
and successor price state. Candidate recomputation covers its config,
board/workload/capacity/preparation identities, terminal reason, counters,
complete epoch records, pool and rejection manifests, final price state, and
single- and multi-world results.

The candidate compact witness binds the recomputed session checksum,
board/workload/capacity/preparation identities, regeneration bound and
terminal reason, all session counters, epoch count and association checksum,
and both final manifests. Publication requires this witness to equal the
separately measured profile's witness field-for-field. Structural FNV-1a
checksums are corruption and association checks, not cryptographic
attestations.

## Capture artifact

Operational Measurement Capture v1 is canonical compact UTF-8 JSON with one
terminal LF and a 32 MiB bound. Its fixed-order root contains:

- schema and exact source envelope;
- fixed false standalone-publication eligibility and true cell-capture
  completeness;
- controller and capture-run identities;
- the complete canonical cell configuration;
- reproducibility provenance;
- baseline then candidate arm objects, each containing measured process/worker
  and authority process/worker; and
- artifact and source-envelope checksums.

All four process-instance identities must be distinct. All workers must share
one source and compiler identity. Both replays for an arm must have identical
complete semantics. Candidate witnesses must match exactly; baseline witnesses
must both be absent. The recomputed full-preimage checksum must equal the
semantic algorithm-session checksum.

The provenance object binds the pinned worker executable descriptor identity,
size, mode, modification time, and SHA-256; Bazel version and
the sizes and SHA-256 digests of `.bazelversion`, `.bazelrc`,
`MODULE.bazel`, and `MODULE.bazel.lock`; compiler, C++ standard, Python,
kernel, OS, libc, CPU, affinity, page, memory, and cgroup context; and explicit
CPU-only applicability for GPU, device memory, upload, compact readback,
compatible batch fill, and prepared-view cache. Cgroup context binds the
cgroup namespace and mount device/inode identity, exact canonical unified-v2
path from `/proc/self/cgroup`, leaf effective cpuset, and complete
namespace-visible leaf-to-root `cpu.max` and `memory.max` control ancestry.
This does not claim visibility above a cgroup namespace root; the explicit
scope is `namespace_visible_unified_v2_leaf_to_root`. Its checksum
authenticates the complete canonical object excluding only that checksum
field.

Capture artifact and source envelopes use the domains
`APGAR-PHASE4-OPERATIONAL-MEASUREMENT-CAPTURE-ARTIFACT-V1` and
`APGAR-PHASE4-OPERATIONAL-MEASUREMENT-CAPTURE-SOURCE-V1`.

## Final publication join

The publisher first authenticates the frozen Statistical Decision Protocol v4
role and evidence disposition. A Raw-v1 cell is accepted only for the 22
legacy success cells and rejects a same-run companion. A Raw-v2 cell is
accepted only for the 78 same-run success cells and requires complete
Same-Run Decision Telemetry validation. The publisher then reconstructs the
corresponding Operational Projection v1 or v2.

Capture source and full cell configuration must exactly equal Raw. Canonical
Raw repetition zero must be a successful baseline-first pair. Each measured
profile and authority semantics object must exactly equal the corresponding
Raw arm semantics. The publication binds:

- Raw and optional same-run artifact/source checksums;
- the legacy operational projection artifact/source checksums;
- canonical cell identity and role;
- every Raw pair, arm, semantic, record, and external-authority checksum used;
- measured and authority process identities, dispatch ordinals, and worker
  artifact/source checksums;
- the recomputed full-preimage session checksum and replay-authority checksum;
- measured process CPU, peak host bytes, and outer wall scope/value;
- the complete operational profile;
- capture artifact/source and run/controller identities; and
- complete reproducibility provenance.

The fixed flags are
`eligible_input_to_phase4_aggregation=true`,
`standalone_decision_eligible=false`,
`statistical_timing_eligible=false`, `coverage_complete=false`, and
`cell_operational_telemetry_complete=true`.

Final publication artifact and source envelopes use
`APGAR-PHASE4-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V1` and
`APGAR-PHASE4-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V1`. Validation
rebuilds the entire publication from its already validated inputs and requires
exact recursive key, type, order, and value equality.

The CLI may emit to stdout or atomically install a named output without
replacement. File installation writes and fsyncs a private sibling temporary,
links it into the final name only after complete construction, fsyncs the
directory, removes the temporary, and fsyncs the directory again. A
post-link failure rolls back the final name when it still names that private
inode. A pre-existing final name is an error.

One publication proves neither complete matrix coverage nor the Phase 4
decision.
