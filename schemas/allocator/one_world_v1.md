# Deterministic One-World Allocation Contract v1

One-World Allocation v1 is the Phase 4 CPU reference selection seam. It chooses
one immutable RouteCandidate v1 from every nonempty per-net pool without
rerunning geometric pathfinding.

## Input

The request contains:

- schema version `1`;
- one nonzero Board IR content hash, compiler-profile fingerprint, and geometry
  compiler version shared by all candidate pools;
- Resource Capacity Model v1;
- Price Snapshot v1;
- one positive unsigned 64-bit intrinsic-cost weight;
- positive configured input/work bounds no larger than the v1 hard maxima; and
- zero or more distinct net pools.

Each net pool names an exact `EntityRef` and contains immutable accepted
RouteCandidate v1 handles. Every candidate must:

- name the pool's exact net;
- match the shared Board IR and compiler association;
- have nonzero identity, compatible candidate/resource schemas, satisfied
  supported hard constraints, exactly two connected intended terminals, and a
  passed exact-validation result; and
- share one routing-profile fingerprint and rule-bucket identity with the other
  candidates in that net's pool.

Different net pools may use different routing-profile fingerprints and rule
buckets over the common compiled resource lattice. Candidate identity must be
unique across the complete request. Empty pools are valid and required when a
routable net has no admissible candidate.

Pool, candidate, capacity, and price arrival order is not semantic. Duplicate
nets, resources, or candidate identities fail explicitly. The capacity model
and price snapshot are constructed as immutable values at their versioned
creation-factory boundary. Their associations and records have no public mutation seam
and must match the request's exact Board IR/compiler association. Capacity
identity is derived from an authenticated Board Snapshot/Compiled Board pair,
and price identity is inherited from that immutable capacity model; rewriting
the outer request cannot relabel stale state.

## Reference score and selection

For candidate `c` and immutable snapshot `p`:

```text
intrinsic_component(c) = intrinsic_cost_weight * c.intrinsic_base_cost
price_component(c, p)  = sum(p[r] * c.usage[r])
selection_score(c, p)  = intrinsic_component(c) + price_component(c, p)
```

All operations are exact unsigned integer operations with checked 64-bit
output. A nonempty pool selects the minimum tuple:

```text
(selection_score, candidate_id)
```

Candidate ID is a deterministic total tie-breaker, not legality or canonical
payload evidence. The selected immutable handle and payload checksum retain the
binding to exact candidate geometry, associations, policy, and provenance.

An empty pool emits one `no_admissible_candidate` outcome. It is not dropped,
converted into a fabricated candidate, or treated as an allocator execution
error.

## Output

The canonical world contains:

- schema/shared associations, price iteration, default capacity, and intrinsic
  weight;
- selections sorted by exact net ID/generation;
- selected candidate ID and payload checksum, raw intrinsic cost, price cost,
  and total selection score, or an explicit no-candidate status;
- Resource Usage v1 records sorted by canonical physical resource;
- selected/no-candidate counts, overused-resource count, total overuse, total
  raw intrinsic cost, and total selection score; and
- deterministic scoring-span queries and matching priced records, selected
  logical resource-use volume, unique accounting edges actually materialized,
  and selected compressed-span boundary events; and
- a stable 64-bit FNV-1a checksum over that externally visible canonical
  content.

The checksum is a replay/determinism checksum, not a cryptographic integrity
claim. Pointer values, input order, and the `world_checksum` field itself never
enter it. FNV-1a starts at offset basis `14695981039346656037` and multiplies by
prime `1099511628211` after XORing each byte. Its exact v1 byte encoding uses
fixed-width little-endian unsigned integers, two's-complement unsigned encoding
for signed coordinates, and no padding. A string is an LE `u64` byte length
followed by its raw UTF-8 bytes. Vectors begin with an LE `u64` element count.
Boolean and optional-presence bytes are `false`/absent = `0` and `true`/present
= `1`. Net-selection status tags are selected = `0` and no-admissible-candidate
= `1`. Physical-direction tags are east = `0`, north-east = `1`, north = `2`,
and north-west = `3`. Starting from the offset basis, encode:

1. length-prefixed UTF-8 domain string `APGAR-ONE-WORLD-V1`;
2. world schema `u32`, Board hash `u64`, compiler-profile fingerprint `u64`,
   compiler version `u32`, price iteration `u32`, default capacity `u32`, and
   intrinsic weight `u64`;
3. selection count `u64`, then for each selection: net ID `u64`, generation
   `u32`, status byte, candidate-ID presence byte and optional `(high u64, low
   u64)`, payload-checksum presence byte and optional checksum `u64`, intrinsic
   cost `u64`, price cost `u64`, and selection score `u64`;
4. resource count `u64`, then for each resource: layer `u32`, source x `i64`,
   source y `i64`, direction byte, capacity-override presence byte, effective
   capacity `u32`, usage `u64`, overuse `u64`, explicit-price presence byte, and
   effective price `u64`; and
5. selected-net count `u64`, no-candidate-net count `u64`, overused-resource
   count `u64`, total overuse `u64`, total intrinsic cost `u64`, total selection
   score `u64`, scoring span-query count `u64`, scoring price-match count `u64`,
   selected logical resource-use count `u64`, accounting materialized-edge
   count `u64`, and accounting span-boundary-event count `u64`.

The discriminating golden compatibility fixture checksum is
`7843367258925855534`. It is a representation-level encoder fixture, not a
claim that the counts describe a feasible allocation. In field order it uses
association values `(2, 3, 5)`, iteration `7`, default capacity `1`, weight
`11`; one present selection with net `(13, 17)`, ID `(19, 23)`, payload `29`,
and costs `(31, 37, 41)`; one absent selection with net `(43, 47)` and costs
`(53, 59, 61)`; two resource records carrying distinct values from `67` through
`113` and both optional-flag branches; and trailing counters
`(127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179)`. This makes adjacent
same-width field permutations observable and covers both optional branches.
Any encoding change requires a schema version change.

## Errors

Stable invariant identifiers distinguish unsupported schemas, invalid or
over-limit configuration, noncanonical/duplicate resources, duplicate nets,
null candidate handles, net/association drift, duplicate candidate identity,
candidate-contract violations, and checked cost/usage overflow.
Host allocation or container-size failure within the declared bounds is a
structured resource-exhausted error rather than partial world publication.
Its diagnostic envelope uses stable non-owning static text so the fallback does
not allocate while handling exhaustion.

## Deliberate boundary

V1 is one deterministic selection/accounting pass over a supplied price
snapshot. It does not update present or historical prices, request targeted
columns, bind a canonical multi-net workload roster, retain multiple worlds,
refine resources, claim combined exact board legality, or establish Phase 4
benchmark success.
