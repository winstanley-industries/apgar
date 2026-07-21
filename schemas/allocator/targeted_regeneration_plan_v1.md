# Targeted Regeneration Plan v1

Targeted Regeneration Plan v1 is the move-only CPU-reference decision between
one validated negotiated-price update and later column generation. Its replay
state is immutable. It does not mutate global occupancy, candidates, or prior
price state, but it owns one live CandidateStore retention lease so every
retained source selection and every next-price winner that influenced pool
sufficiency remains executable.

## Input and validation

The factory consumes the exact previous Negotiated Price State v1, complete
original One-World v2 request, completed source world, CandidateStore, and a
bounded configuration.

It first runs the production One-World request validation, canonical pool
construction, and score-minimizing selection pass without materializing atomic
occupancy. This authoritative selection evidence binds every pool alternative
and supplies the compressed source-winner footprint. If that footprint exceeds
the planning bound, the factory returns `WorkBoundExceeded` before negotiated-
price validation reconstructs atomic occupancy. Otherwise the source world's
selection projection must exactly match the evidence.

The factory then invokes the production negotiated-price update. That boundary
revalidates the authentic workload and capacity model, reruns every original
pool and alternative, and independently reconstructs source selection,
resource use, overuse, counters, and checksum. The resulting next price state
is owned by the plan.

Finally, the factory builds the exact next price snapshot and reruns the shared
selection-only pass over every complete pool. A pool is insufficient in this
iteration only when its minimum-score next-price winner still touches at least
one resource that is overused in the authenticated source world. An existing
alternative that avoids every current conflict suppresses regeneration even
when the stale source winner was conflicted.

Configuration requires positive target-net, per-net column, total-column,
per-net action, total-action, and expanded-resource-visit bounds. Target nets,
per-net/total columns, and per-net/total actions are each at most 1,000,000;
expanded visits are at most 100,000,000.

The planning visit count is the widened sum of source-winner compressed span
lengths, next-price-winner compressed span lengths, and the retained target
prefix's action-rescan span lengths. The source portion is checked before
atomic source-world reconstruction, the source-plus-next portion before the
first planner expansion, and every action-rescan addition before that rescan.
The source request's independent One-World bound still applies to validation
of all pool alternatives.

Column planning additionally reserves candidate-count headroom in the complete
source request. At most
`source_limits.maximum_candidates - source_candidate_count` columns may be
requested across the plan. Zero remaining headroom produces an empty target
list. For every retained target, requested columns are also at most one plus
the number of retained ordered resource actions: the first column is the
complete-price search and every later column has one distinct action to ban.

## Canonical request and pool manifests

Manifest construction occurs only after production request and exact-candidate
validation. Source order never affects a manifest.

All checksums defined by this schema are 64-bit FNV-1a with offset basis
`14695981039346656037` and prime `1099511628211`. Each byte is XORed before the
multiplication. The exact byte encoding uses fixed-width little-endian unsigned
integers, two's-complement unsigned encoding for signed integers, and no
padding. A domain string is encoded as an LE `u64` byte length followed by its
raw UTF-8 bytes. Every declared record/vector count is an LE `u64`. Physical
direction tags are east = `0`, north-east = `1`, north = `2`, and north-west =
`3`. No implicit separators, terminators, fields, or host representation enter
the encoding.

Starting from the offset basis, the per-pool manifest encodes:

1. domain string `APGAR-ONE-WORLD-POOL-MANIFEST-V1`;
2. canonical net ID `u64`, generation `u32`, and candidate count `u64`; and
3. for each candidate in ascending Candidate ID order, ID high `u64`, ID low
   `u64`, and payload checksum `u64`.

Starting from the offset basis, the global pool manifest encodes:

1. domain string `APGAR-ONE-WORLD-POOLS-MANIFEST-V1`;
2. canonical pool count `u64`; and
3. for each pool in ascending `(net ID, generation)` order, net ID `u64`,
   generation `u32`, and that pool's manifest checksum `u64`.

Starting from the offset basis, the full request manifest encodes:

1. domain string `APGAR-ONE-WORLD-REQUEST-MANIFEST-V1`;
2. request schema `u32`, Board content hash `u64`, compiler-profile fingerprint
   `u64`, geometry-compiler version `u32`, workload checksum `u64` (`0` when no
   workload is bound), and intrinsic-cost weight `u64`;
3. maximum nets, candidates, resource records, and expanded resource uses as
   four `u64` values in that order;
4. capacity schema `u32`, default capacity `u32`, override count `u64`, then
   each override in canonical resource order as layer `u32`, lattice x `i64`,
   lattice y `i64`, direction byte, and capacity `u32`;
5. price schema `u32`, iteration `u32`, price-record count `u64`, then each
   record in canonical resource order as layer `u32`, lattice x `i64`, lattice
   y `i64`, direction byte, and price `u64`; and
6. global pool-manifest checksum, source pool count, and source candidate count
   as three `u64` values.

Thus even a non-winning alternative or nonbinding request-limit change affects
replay identity.

The manifest golden fixture uses request schema `2`, associations
`(11254834395910409746, 15213476683192819267, 1)`, no workload, weight `19`,
limits `(3, 5, 7, 1009)`, capacity schema/default `(1, 1)`, and one override
`(layer 0, x -127, y -131, north-west, capacity 0)`. Its price state has schema
`1`, iteration `17`, and one explicit zero-price record
`(layer 0, x -109, y -113, east, price 0)`. The first canonical pool is empty
net `(9, 4)` and hashes to `4665966256940749526`. The second is net `(10, 0)`
with these candidates in canonical order:

```text
ID (9094886488943244682, 6553997329807649295), payload 1481378841828407483
ID (16905596407620462294, 13442482538882174867), payload 12239829871499795965
```

That pool hashes to `964588783859553204`; the global pool manifest hashes to
`9837338791036826863`; and the full request manifest hashes to
`14547234282927751318`. Any byte encoding or semantic field change requires a
new schema version.

## Hotset metrics and order

Metrics use each complete pool's next-price winner. For every atomic winner
resource:

```text
resource_conflict_impact = source_world_overuse * candidate_usage
resource_price_exposure  = next_total_price * candidate_usage
```

Price exposure includes historical prices on resources that are not currently
overused. Conflict count, impact, eligibility, and actions include only
positive source-world overuse. A next-winner edge absent from source occupancy
has zero current overuse. Every positive-overuse edge has a corresponding next
price record.

Per-net metrics use widened arithmetic and must fit unsigned 64-bit replay
fields. Resource actions retain the configured best bounded set in this stable
order: descending conflict impact, descending observed overuse, descending
negotiated price, then ascending canonical resource key. Eligible nets rank by
descending conflict impact, descending full next-price exposure, descending
conflict count, then ascending `(net id, net generation, next candidate id)`.

Scratch is bounded as well as output. The first pass retains at most
`min(maximum_target_nets, maximum_total_columns,
maximum_total_resource_actions)` target summaries. Only that ranked prefix is
rescanned for actions. Each action heap is capped by the smaller of the per-net
limit and remaining global action budget.

Ranked nets are retained until a target, total-column, or total-action budget
is exhausted. A retained net requests
`min(maximum_columns_per_net, conflict_resource_count,
resource_action_count + 1)` columns, truncated by the remaining total-column
and source-request candidate-headroom budgets, and retains at least one action.
Aggregate conflict counts and impacts cover retained targets and must fit
unsigned 64-bit fields.

Each target records its pool manifest/count, source-selected ID/checksum,
next-price winner ID/checksum/score, metrics, budget, and ordered actions.

## CPU policy synthesis

`BuildTargetedRegenerationPoliciesV1` is the deterministic CPU-reference
translation from one retained target to exactly `requested_columns` normalized
Candidate Generation Policy v1 values. It accepts only the target's authentic
Prepared Net Routing Context, the plan's next Negotiated Price State, the
One-World intrinsic-cost weight, and explicit deterministic seed/first-ordinal
scheduling inputs. Board/compiler/routing/net associations must match, the
first ordinal plus the column count must fit `u32`, every retained action
resource must exist in the prepared Compiled Board, and the aggregate generated
policy resource-entry count is bounded by the Candidate Policy v1 maximum.

Every policy represents the complete target-legal projection of the nonzero
next-price field. Every global price record is first checked for canonical
order and exact present/history/clamped-total consistency. A record whose
resource is absent from this target's authentic Compiled Board is then omitted:
that edge cannot occur in any candidate for this target and contributes zero
to every target route. Every retained target-legal resource with price
`total_price` receives that exact additional edge cost. Intrinsic routing
cost is scaled to the One-World objective by checked surcharges
`(intrinsic_cost_weight - 1) * base_cost` independently for orthogonal,
diagonal, and bend costs. Thus an unbanned CPU route minimizes the same
`intrinsic_cost_weight * intrinsic_base_cost + price_cost` scalar used by
One-World selection. Present and historical price components are not treated
differently; a history-only nonzero total remains a policy penalty.

Column zero has no action ban. Column `i > 0` bans ordered resource action
`i - 1`. Candidate Policy v1 forbids one resource from being both banned and
penalized, so that absent edge's otherwise complete price entry is removed;
all other nonzero target-legal next-price records remain. Target action
resources are always required to exist in the target's Compiled Board even
when unrelated global price records are projected away. Policies retain the
supplied seed and use consecutive candidate ordinals. Any association,
resource, canonical-order, price-total, policy-normalization, arithmetic, or
aggregate-work failure is structured and no partial policy batch is returned.

## CandidateStore lease

After every deterministic field is computed, the factory atomically acquires
one store-issued group lease over all source-world winners plus every non-null
next-price winner that influenced pool sufficiency, deduplicated by candidate
ID. CandidateStore checks the diagnostic net, ID, and payload checksum and then
the complete immutable candidate value for the complete group under one lock.
Missing, detached, stale, semantically mismatched (including an ID/checksum
collision), duplicate, or over-bound input fails without changing any pin
count.

When both validated selections contain zero candidates, the factory instead
acquires an active zero-candidate identity lease from the exact CandidateStore.
It pins no candidate and leaves `pinned_candidate_count` equal to zero, but its
active/store-ownership checks preserve the same executor binding as a nonempty
retention lease. Thus an all-empty authentic workload can reach its structured
no-work outcome without accepting a plan against a different store.

Independent acquisitions receive independent opaque lease identities and are
reference-counted even when they retain the same candidate. Releasing either
overlapping plan therefore cannot unpin the other. The plan owns the move-only
RAII lease; destruction releases it at most once. CandidateStore must outlive a
plan that remains executable. A lease that outlives store destruction is
harmless, reports inactive, and releases as a no-op. Runtime lease identity is
deliberately excluded from replay state and equality, while deterministic
pinned-candidate count is encoded.

## Output and replay encoding

The checksum uses the common FNV-1a and byte encoding defined above. Starting
from the offset basis, first encode the length-prefixed domain string
`APGAR-TARGETED-REGENERATION-PLAN-V1`, then the following fields in order.

Encode schema `u32`; Board hash `u64`; compiler fingerprint `u64`; compiler
version `u32`; workload checksum `u64`; source-request manifest, global pool
manifest, source pool count, source candidate count, and pinned candidate count
as five `u64`; configuration maximum targets, columns per net, total columns,
actions per net, total actions, and expanded visits as six `u64`; next price-
state checksum `u64`; price iteration `u32`; source-world checksum `u64`;
target count `u64`; and total columns, actions, conflicts, impact, and expanded
visits as five `u64`.

For each target encode net ID `u64`, generation `u32`, pool manifest and pool
candidate count as two `u64`, source candidate ID high/low and payload checksum
as three `u64`, next candidate ID high/low, payload checksum, and selection
score as four `u64`, conflict count, impact, price exposure, and requested
columns as four `u64`, then action count `u64`. For each action encode layer
`u32`, lattice x/y `i64`, direction `u8`, and overuse, candidate usage, price,
and conflict impact as four `u64`.

The representation fixture has header values, in the exact order above:
schema/associations `(2, 3, 5, 7)`, workload/request/pool manifests and counts
`(11, 13, 17, 19, 23, 29)`, configuration `(31, 37, 41, 43, 47, 53)`, price
state/iteration/source world `(59, 61, 67)`, target count `1`, and aggregate
fields `(71, 73, 79, 83, 89)`. Its one target encodes net `(97, 101)`, pool
manifest/count `(103, 107)`, source ID/payload `(109, 113, 127)`, next
ID/payload/score `(131, 137, 139, 149)`, metrics/budget
`(151, 157, 163, 167)`, and action count `1`. The action is
`(layer 173, x -179, y 181, north-west)` with fields `(191, 193, 197, 199)`.
The resulting checksum is `10960306375439121819`. Any byte encoding or
decision-rule change requires a new schema version.

## Deliberate boundary

This slice decides a bounded, store-backed hotset and column/action budgets and
defines deterministic CPU-reference policy synthesis. Targeted Regeneration
Execution v1 composes this plan with authentic CPU generation, conditional
publication, refreshed selection, and stall diagnostics. Neither schema retains
multiple worlds or claims Phase 4 evidence-gate success.
