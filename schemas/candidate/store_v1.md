# Deterministic Candidate Store Contract v1

The Phase 3 candidate store owns immutable accepted RouteCandidate v1 objects
and immutable CandidateRejection v1 records. It never mutates Board IR,
CompiledBoard, congestion, prices, or allocator state.

A store instance binds globally to the Board content hash, compiler-profile
fingerprint, and geometry-compiler version when its first exact-admitted
RouteCandidate enters publication. Independently, each exact net binds to its
routing-profile fingerprint and rule-bucket identity on that net's first exact
admission. Both bindings persist even if duplicate selection, retained-pool
budgets, or a multi-pool pinned rollback subsequently reject every candidate
in that publication. Later candidates with another global session or another
context for the same net are rejected. Distinct authentic nets may carry
distinct routing profiles and rule buckets, but stale snapshots or drifting
contexts are never mixed into a per-net pool or returned by `Enumerate(net)`.

## Budgets and admission order

Configuration supplies positive per-net accepted-candidate and accepted-byte
caps, a bounded rejection-record cap, a separate rejection-ingestion item cap,
and three independent admission-transaction caps: item count, aggregate input
logical bytes, and deterministic work units. A separate positive pin-lease item
cap bounds the identities examined by one atomic group acquisition. The default
rejection-ingestion and pin-lease caps are each 1,024 records per call; the
pin-lease cap cannot exceed the v1 hard maximum of 1,000,000 identities. The
default admission-transaction caps are 1,024 items, 64 MiB, and 100,000,000
work units. Checked logical candidate bytes
recomputed from the actual candidate fields are the byte-budget authority; a
generator's reported `logical_bytes` is not trusted. Pinned candidates consume
the same retained-pool budgets.

Transaction preflight occurs before exact admission and before its batch-sized
admission-result scratch is allocated. The item-count cap is checked before an
overload constructs any additional per-item scratch. For the overload that
applies one shared request to many generated candidates, the canonical
request-policy bytes and policy-entry work are multiplied by the item count and
checked in O(1). This multiplication is conservative logical/work accounting,
not a statement about implementation allocations: the request remains
caller-owned, its policy is normalized exactly once, and that immutable result
is reused for every candidate without per-item request or policy copies. Each
candidate's untrusted policy must compare exactly equal to that normalized
typed value to use the fast path; a differing candidate policy receives full
independent normalization and fails closed if it is invalid. Every generated
and request policy first receives the CandidateGenerationPolicy v1 O(1) shape
preflight. If any policy is over its resource-entry bound, the whole
transaction returns one candidate-less `InvalidInput` diagnostic with invariant
`candidate.store.transaction.policy_resource_entry_count.v1`; its actual value
is the maximum invalid aggregate count, so input permutation cannot change the
diagnostic. If those checks pass, v1 first checks O(1)-per-container minimum
serialized widths and the non-edge-count part of the work formula. Thus a
vector already larger than either cap is rejected without walking its elements.

Candidate store admission is explicit ownership transfer: the public single
and batch candidate APIs accept only rvalue candidates/vectors. Binding the API
therefore cannot implicitly deep-copy caller bulk. Before moving any element
into internal scratch, the store applies fixed shape precedence across the
transaction: policy entries,
geometry primitives, resource spans, then supported-device-class bytes. It
reports the maximum invalid count within the first failing field, independent
of input permutation. The corresponding transaction invariants are
`candidate.store.transaction.policy_resource_entry_count.v1`,
`candidate.store.transaction.geometry_primitive_count.v1`,
`candidate.store.transaction.resource_span_count.v1`, and
`candidate.store.transaction.device_class_bytes.v1`. Their limits are
1,000,000, 1,000,000, 1,000,000, and 1,024. These candidate-less diagnostics
omit payload checksum because invalid bulk is not inspected.
Inputs passing that structural bound receive exact recomputed logical-byte
accounting. That byte quantity is the sum of each generated candidate's
canonical v1 logical bytes and the canonical v1 bytes of the independently
supplied request policy for that item. These per-item logical quantities count
even when one physical shared-request object supplies every item. Work uses the
following conservative quantity, all with
checked unsigned arithmetic:

```text
1 + P + P*(P-1)/2 + P*O + P*T + R + B_generated + Q_generated
  + B_request + Q_request
  + sum(derived geometry lattice-edge steps)
  + sum(reported resource.edge_count)
```

Here `P` is primitive count, `O` is the authoritative Board IR obstacle count,
`T` is the count of all authoritative Board IR terminals, `R` is resource-span
count, `B_generated` and `Q_generated` are the generated
candidate's banned-resource and penalty counts, and `B_request` and `Q_request`
are the admission request's independently supplied counts. Derived lattice-edge
steps come from exact geometry and the associated compiler profile, so a
corrupt small reported footprint cannot hide a long expansion. `P*T`
conservatively covers the exact unintended-terminal scan for every
primitive; terminal work may not be inferred only from the routed net's two
intended terminals. A cap violation
rejects the complete transaction with exactly one candidate-less
`CandidateRejection` at the generated stage and performs no exact admission or
publication. Arithmetic outside the unsigned 64-bit accounting domain is
`MemoryAccountingOverflow`; a configured cap violation is `BudgetExhausted`; a
zero cap is `InvalidInput`. This transaction-level record is retained through
the same canonical bounded rejection store as all other diagnostics.

After preflight, batch admission normalizes work in stable `(net, candidate
ID)` order, then serializes publication under the store mutex. Accepted
candidates are held in deterministic per-net pools, and a separate ordered
global candidate-ID index enforces identity uniqueness. Publication copies and
recomputes only touched pools; unrelated pools are not scanned. Concurrent
callers observe linearizable snapshots. One explicit `AdmitBatch` is the
deterministic publication boundary for candidates produced concurrently: its
retained pool and per-item results do not depend on worker completion order.
Separate single-item calls are race-safe and deterministic for their mutex
linearization, but a bounded store does not promise the same final pool across
different linearizations of future calls. Callers requiring schedule-independent
publication must use one batch. Ranking and pruning use total stable keys and
never depend on hash-table iteration, pointer values, or item order within a
batch. Enumeration is sorted by the ranking key and then candidate ID.

The v1 retention rank compares this exact tuple, lower first:

```text
(intrinsic_base_cost,
 via_count,
 bend_count,
 orthogonal_step_count + diagonal_step_count as a mathematical nonoverflowing sum,
 axis_aligned_length_dbu,
 diagonal_projection_dbu,
 resource_signature high/low,
 geometry_signature high/low,
 policy_identity,
 scalar_policy_cost,
 candidate_id high/low)
```

If equal, it compares payload checksum, then the complete typed semantic
fallback: schema major/minor; candidate ID; net and both terminals as
ID/generation; the five associations; geometry/resource schema versions;
policy schema/objective/seed/ordinal/surcharges followed by lexicographic banned
resources and penalties; policy identity; provenance generator/version/backend,
ordinary bytewise lexical device class, seed, batch, query, and ordinal;
lexicographic tagged geometry fields; lexicographic resource-span fields; all
metric fields in RouteCandidate serialized order; all constraint fields;
geometry/resource signatures; payload checksum; and logical bytes. Sequence
comparison is elementwise then length. Numeric comparisons use their unsigned
or signed schema types and never wrapped arithmetic.

## Deduplication and diversity

- Duplicate candidate IDs are rejected, including an identical repeated
  payload. An ID already owned by an immutable incumbent in the same or another
  net is rejected through the global index without ranking, replacing, or
  changing either pool. If one previously unpublished batch contains multiple
  payloads with the same ID, its stable best payload is the sole member allowed
  to proceed to publication and every other payload is rejected; after that
  publication, the incumbent is immutable.
- Candidate IDs and geometry/resource signatures select deterministic ordered
  lookup buckets. A geometry/resource signature match triggers full
  collision-safe canonical equality only within that bucket before
  deduplication. Deliberate signature collisions may increase work inside the
  bounded bucket but cannot cause false deduplication.
- Exact duplicate geometry or resource footprints form an atomic group over
  the complete touched pool, including incumbents. A pinned incumbent wins as
  required by CAN-002; otherwise the better stable-ranked representative is
  the only member eligible for retention. Same-ID and exact duplicate
  representative selection both occur over the complete incoming plus touched
  incumbent pool before any incoming candidate is excluded for its individual
  logical-byte size. Logical byte size is not a ranking key. If the selected
  representative cannot survive retained-pool capacity, no lower-ranked,
  smaller group member is promoted on retry. Every excluded member receives a
  structured diagnostic.
- Resource Jaccard is intersection cardinality divided by union cardinality of
  expanded canonical physical-edge keys. Two empty sets are invalid candidates,
  not a special similarity value.
- Geometric overlap v1 is shared physical-edge DBU projection divided by the
  smaller candidate's physical-edge DBU projection.
- Metric dominance requires no worse intrinsic base cost,
  orthogonal/diagonal steps, bends, or vias and at least one strict
  improvement. Request-local scalar policy costs are retained for provenance
  and differential validation but are not compared across policy identities.

Pruning first preserves every valid retention pin, then useful nondominated
representatives, then the best representative of each unique resource
signature, and finally fills remaining capacity by stable rank. When all
requirements cannot fit, admission fails closed with `BudgetExhausted`; pins
are never silently evicted.

All pools touched by one batch are staged before publication. If a touched
pool's pins cannot fit, the batch's candidate publication rolls back and the
incoming candidates receive `BudgetExhausted`; existing pools and the global ID
index remain unchanged. Builder-side structured rejections may be submitted
through the public single or batch rejection-retention seam and use the same
canonical order and cap. This seam applies Candidate Rejection v1 canonical
ingestion before publication: it validates schema and enum tags, enforces the
three 1,024-byte UTF-8 bounds by reference before copying caller text,
recomputes logical bytes, and substitutes the bounded
`candidate.rejection.ingestion.v1` diagnostic for malformed records. Malformed
caller text or overflow is never retained with a zero byte count.
The batch seam accepts a non-owning span and checks its item count in O(1)
before copying, inspecting, canonicalizing, or taking the store mutex. An input
above `maximum_rejection_items_per_transaction` is not inspected and produces
exactly one candidate-less generated-stage `BudgetExhausted` diagnostic with
invariant `candidate.store.rejection_transaction.item_budget.v1`, expected cap,
and actual input count. The batch API returns that diagnostic directly and also
submits it to the ordinary stable retained-record order and cap; a preexisting
record may outrank it in bounded history without erasing the call result. A
within-cap call returns no transaction diagnostic. The result therefore does
not depend on the contents or order of the uninspected records.
Within the cap, one rejection is inserted at its canonical lower bound rather
than re-sorting retained history. A rejection batch is canonicalized and
sorted once, then deterministically merged with retained history up to the
configured cap.
One admission publication collects all exact-admission diagnostics and all
diagnostics produced while staging duplicate, retention, or rollback outcomes.
Under the publication mutex, that complete canonical set is sorted once and
merged/truncated against retained history once; it is not published through a
sequence of shifting single-record insertions.

## CAN-002 retention seam

Retention pins are store metadata separate from immutable candidates. A
nonzero owner ID may pin one stored candidate idempotently. Unpinning names the
same owner/candidate pair. Pin acquisition that would violate configured
budgets fails. This original single-candidate seam remains source-compatible,
but a caller must not use one owner/candidate pair as multiple independent
lifetimes.

The collision-safe group seam accepts one nonempty bounded set of immutable
candidate handles plus their diagnostic `(net, candidate ID, payload checksum)`
identities. It canonicalizes the set, rejects duplicate candidate IDs, and
under one store lock validates that every ID is currently present, matches all
three diagnostic fields, and is exactly equal to the supplied immutable
candidate. Missing, detached, stale-net, stale-payload, and semantically
mismatched inputs (including an ID/checksum collision) fail the complete
acquisition; no prefix is pinned. Only after all validation and allocation
succeeds does the store issue a fresh internal lease identity and increment
each candidate's pin reference count. Every acquisition receives an independent
identity even when its candidate group is identical to another live lease.

The returned move-only scoped lease owns exactly one acquisition. Destruction
or explicit `Release` decrements its complete group at most once; repeated
release is a no-op. Releasing either of two overlapping leases cannot remove the
other's retention. A lease reports active only while its store is alive. It may
safely outlive store destruction, after which it reports inactive and release
is a no-op because the store and its candidate pools no longer exist.
The store-issued lease identity is runtime metadata, not replay identity and not
caller-controlled allocator state. Phase 3 does not define allocator worlds,
prices, selection, or column generation.
