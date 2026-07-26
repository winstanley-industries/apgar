# Targeted Regeneration Plan v3

**Status:** Reserved successor; not executable until its implementation is
separately reviewed and activated.

Targeted Regeneration Plan v3 is a bounded target-retention policy
supersession of
[`targeted_regeneration_plan_v2.md`](targeted_regeneration_plan_v2.md). It
preserves v2 input validation, source and next-price selection, conflict
metrics, action ranking, target severity ranking, CandidateStore lease,
policy synthesis, configuration fields, and hard limits.

At this contract freeze, production still accepts schema version `2`.
Conforming activation advances production to schema version `3` and rejects
versions `1` and `2` before mutation or input consumption.

## Provisional primary conflict

The first resource scan of every eligible next-price winner retains exactly
one action in addition to computing the complete v2 metrics. That action is the
winner under the existing stable action order:

```text
conflict_impact descending,
observed_overuse_units descending,
negotiated_price descending,
canonical resource ascending
```

The retained action is the target's provisional primary conflict. Pool
cardinality remains checksum evidence and does not participate in target or
coverage ordering.

The ordinary v2 target order remains:

```text
conflict_impact descending,
negotiated_price_exposure descending,
conflict_resource_count descending,
net id / generation / next-price candidate id ascending
```

## Coverage reservation

Let `candidate_headroom` retain its v2 definition and let:

```text
K = min(maximum_target_nets,
        maximum_total_columns,
        maximum_total_resource_actions,
        candidate_headroom)
```

If `maximum_columns_per_net >= 2`, define:

```text
C = min(maximum_target_nets,
        floor(maximum_total_columns / 2),
        maximum_total_resource_actions,
        floor(candidate_headroom / 2))
```

Otherwise `C = 0`.

Conceptually, eligible targets are grouped by the exact resource key of their
provisional primary action. The best target under the ordinary target order is
the representative of each group. Sort those representatives by the same
order and retain the first `C`.

A streaming implementation MUST produce that exact result while retaining no
more than `C` live resource groups. When the bounded group set is full, a new
group replaces the current worst representative only when the new
representative ranks before it. A better later representative for an existing
group replaces that group's current representative. The retained threshold
can only improve, so an evicted worse representative does not require an
unbounded restoration roster. Canonical ordered lookup/ranking or an
equivalent deterministic bounded structure is required.

The retained representatives are coverage seeds. In target order, reserve for
each seed:

- one price-only column;
- one hard-ban column for its provisional primary action; and
- one resource-action slot.

All seed reservations occur before any secondary action or another target
whose primary action names an already represented resource consumes remaining
target, action, column, or candidate-headroom opportunity.

Build the exact-net-deduplicated union of the coverage seeds and the top-`K`
fallback targets. A seed wins the role when it is also in the fallback; two
different summaries for one net are an internal-invariant failure. Sort the
union by the ordinary target order.

Let `S` be the actual seed count, which can be smaller than `C`. Initialize,
in widened arithmetic:

```text
used_targets = S
used_columns = 2 * S
used_actions = S
```

Those reservations consume source-candidate headroom through `used_columns`
and MUST fit every corresponding cap before materialization. Then visit every
union target in the global ordinary target order. This allocation order is
normative even though final output uses the two lanes below.

For a seed, compute:

```text
action_limit =
    min(maximum_resource_actions_per_net,
        1 + (maximum_total_resource_actions - used_actions))
```

The one added before the minimum is its already reserved primary action.
Perform the retained-target rescan with `action_limit`, apply the replay checks
below, and let:

```text
extra_actions = resource_action_count - 1
used_actions += extra_actions

desired_total_columns =
    min(maximum_columns_per_net,
        conflict_resource_count + 1,
        resource_action_count + 1)

extra_columns =
    min(desired_total_columns - 2,
        maximum_total_columns - used_columns,
        candidate_headroom - used_columns)

requested_columns = 2 + extra_columns
used_columns += extra_columns
```

The seed is materialized in place. Its complete retained action roster is
kept, including any actions made unreachable by a tighter remaining-column
cap, preserving v2 action-retention behavior.

For an unseeded target, first skip it when any of these is exhausted:

```text
used_targets == maximum_target_nets
used_columns == maximum_total_columns
used_columns == candidate_headroom
used_actions == maximum_total_resource_actions
```

Otherwise apply the v2 materialization formulas using the corresponding
remaining values:

```text
action_limit =
    min(maximum_resource_actions_per_net,
        maximum_total_resource_actions - used_actions)

desired_columns =
    min(maximum_columns_per_net,
        conflict_resource_count + 1,
        resource_action_count + 1)

requested_columns =
    min(desired_columns,
        maximum_total_columns - used_columns,
        candidate_headroom - used_columns)
```

The retained-target rescan occurs between the first and second calculations
and supplies `resource_action_count`. It MUST reproduce the provisional
primary action. On retention:

```text
used_targets += 1
used_columns += requested_columns
used_actions += resource_action_count
```

A one-column unseeded tail remains permitted and retains the scanned primary
action exactly as v2 does.

An unseeded skip is `continue`, not `break`: a lower-ranked seed still requires
mandatory rescan and materialization. Every deduplicated retained target is
rescanned at most once. Any replay mismatch or work-bound failure rejects the
whole plan rather than dropping a seed.

Targets are emitted as two canonical lanes:

1. coverage seeds sorted by the ordinary target order;
2. nonseed targets sorted by the ordinary target order.

`coverage_seed_target_count` is the length of the first lane. Seed primary
resource keys MUST be pairwise distinct. This contract guarantees only action
zero coverage; secondary actions do not make a target the representative of
another group.

After lane assembly, replay the final target count and all column, action,
conflict, and impact aggregates. They MUST equal `used_targets`,
`used_columns`, `used_actions`, and the corresponding widened sums over the
final output.

When `C = 0`, the seed lane is empty and the remainder preserves Plan-v2
target allocation, including its bounded one-column tail.

## Materialization and work bounds

Every retained target MUST have a primary action. Its retained-target scan MUST
reproduce the provisional primary action
field-for-field: resource key, observed overuse units, selected-candidate
usage units, negotiated price, and conflict impact. It MUST also reproduce the
complete provisional conflict count, total impact, and price exposure. Any
drift fails with an internal-invariant error before policy synthesis.

The fallback scratch is bounded by `K`; coverage grouping and ranking scratch
are each bounded by `C`; their deduplicated materialization union is at most
`K + C`. Every retained-target resource rescan remains charged to
`maximum_expanded_resource_visits`. Host allocation failure retains the
structured resource-exhausted boundary.

## Replay identity

V3 adds one `u64` plan field, `coverage_seed_target_count`. It MUST be no
greater than target count. Targets before that index are seeds; no per-target
role flag or duplicate primary-resource field is encoded.

V3 uses domain `APGAR-TARGETED-REGENERATION-PLAN-V3`. Encoding preserves the
v2 order except that `coverage_seed_target_count` is inserted immediately
after the encoded target count and before aggregate totals:

```text
existing v2 header through source_world_checksum
target_count
coverage_seed_target_count
existing aggregate totals
existing ordered target records and action records
```

The v1 and v2 encoders and their fixtures remain unchanged. The v2 fixture is
`8958480360901484544`. With schema version `3`, seed count `1`, and otherwise
the same prime-valued fixture, the v3 representation is
`11681155673263320325`.

This plan is accepted only by Targeted Regeneration Execution v6 or a later
explicit authority. Any further field, bound, order, width, grouping, or
decision-rule change requires another schema version.
