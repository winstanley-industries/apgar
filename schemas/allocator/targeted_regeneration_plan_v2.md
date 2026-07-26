# Targeted Regeneration Plan v2

> Superseded for production planning by Targeted Regeneration Plan v3.

Targeted Regeneration Plan v2 is a narrow decision-rule supersession of
[`targeted_regeneration_plan_v1.md`](targeted_regeneration_plan_v1.md). It
preserves the v1 input validation, hotset and action ranking, bounds,
CandidateStore lease, policy synthesis, field order, and field widths.

Historical production accepted schema version `2`. The production Plan-v3
factory rejects versions `1` and `2` before mutation or input consumption.

## Corrected column cardinality

For every retained target, v2 requests:

```text
min(maximum_columns_per_net,
    conflict_resource_count + 1,
    resource_action_count + 1)
```

Both additions are evaluated in widened unsigned arithmetic before the
bounded result is converted to `u64`. Remaining total-column and
source-candidate-headroom limits then truncate that result exactly as in v1.

Column zero remains the complete next-price policy. Column `i > 0` remains the
complete next-price policy with ordered resource action `i - 1` banned. The
additional one therefore makes the final retained action reachable: a target
with one conflict and one action may produce both its price-only column and
its sole hard-ban column. V1's `conflict_resource_count` cap made that hard-ban
column unreachable.

## Replay identity

V2 uses the domain `APGAR-TARGETED-REGENERATION-PLAN-V2`. The complete field
order and encoding are identical to v1, including schema version, all
configuration fields, every target's requested-column count and action
roster, and all aggregate counters.

The v1 representation fixture remains
`10960306375439121819`. With schema version `2` and the v2 domain, the same
prime-valued fixture hashes to `8958480360901484544`.

This plan remains replay identity for historical Targeted Regeneration
Execution v5 evidence. Production Execution v6 requires Plan v3. Any further
field, bound, order, width, or decision-rule change requires another schema
version.
