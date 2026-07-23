# Phase 4 Same-Run Raw Evidence v2

Same-Run Raw Evidence v2 is the ordinary measured-cell half of one
telemetry-aware Wire-v2 controller execution. It is never standalone
publication evidence: decision publication requires the matching Same-Run
Decision Telemetry v1 companion from the same invocation.

The isolated-cell payload, attempts, records, external authority, and paired
results remain exactly those specified by `phase4_raw_evidence_v1.md`. The
Raw-v2 cell artifact uses domain `APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V2`,
hashes Raw Evidence schema `2` and Wire schema `2`, then hashes the same cell
fields and ordered pair-attempt checksums as the v1 cell artifact. This makes
the actual worker carrier part of the authenticated cell without changing the
frozen Raw-v1 checksum. The distinct root envelope likewise prevents a Wire-v2
measurement from masquerading as Raw Evidence v1. Its canonical root adds
`raw_evidence_schema_version=2` before `wire_schema_version=2`; all remaining
fields retain Raw v1 order and meaning. Raw-v1 serializers and validators must
reject this extra field and carrier version, and each serializer must reject
an in-memory result produced by the other worker carrier.

The source-envelope checksum uses byte-stable FNV-1a with domain
`APGAR-PHASE4-SOURCE-ENVELOPE-V2`, followed by:

1. raw-evidence schema version;
2. wire schema version;
3. exact source commit;
4. stamped flag;
5. dirty-tree flag; and
6. the complete isolated-cell artifact checksum.

The telemetry-aware controller must preserve this Raw artifact even when a
timeout, typed child failure, protocol error, signal, nonzero exit, external
budget failure, output violation, or teardown failure makes the companion
unavailable. Such a cell exits nonzero and remains incomplete evidence; it is
never converted into an allocator loss. The companion final path is installed
without replacement only after the Raw stream is fully written and flushed,
the complete companion has been written and synchronized under an unpublished
sibling name, and the synchronized inode can be atomically linked at the final
name. The containing directory is then synchronized. A failed Raw write or
interrupted companion write must leave no final companion path.

The total-attempt validator accepts both complete and incomplete Raw-v2 cells.
It must traverse every pair and arm, validate any nested record or typed
failure, reconstruct every arm and pair checksum, enforce cell association and
unique positive dispatch identities, and finally reconstruct the cell and
source-envelope checksums before reporting incompleteness. Publication
validation is a separate stricter mode and still requires all 20 completed
pairs.

Candidate lifecycle continuity includes controller-authenticated invocations
whose records were discarded only after finalization or pair assembly. The
next successful record's before-counters must advance by exactly the number of
those intervening contender invocations; an arbitrary controller failure does
not authorize a lifecycle gap.

A worker-lifetime, teardown, protocol, or process-exit failure is process-wide:
no attempt sharing that persistent process may remain successful after the
controller invalidates its retained successes. Typed child failures and the
exact enumerated post-execution finalization witnesses remain attempt-local.
Serialized Linux wait status is restricted to the exact unsigned 16-bit
terminal-status representation before exit or signal decoding; negative or
high-bit aliases are corrupt evidence.
A processless setup or launch failure must retain canonical absent child state:
zero peak, wait status, and signal, exit code `-1`, and no watchdog-kill claim.
When exact reap authority is lost after an earlier setup or typed child
failure, the original disposition and typed payload remain intact while the
same checksummed attempt carries the exact reap-authority invariant as a
secondary controller witness. A typed child failure is fatal at dispatch and
must be the final positive dispatch ordinal.

Incomplete cells do not relax deterministic semantics: every retained
successful arm must agree with the other retained successes for that arm after
normalizing only repetition and execution-order identity, and every retained
completed pair must agree on the board-level comparison.
