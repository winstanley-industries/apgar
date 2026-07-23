# Phase 4 Statistical Decision Protocol v1

This frozen protocol is incorporated unchanged by the authority-only v2
supersession. Its statistical decisions remain normative; its Raw-v1 authority
for exact, held-out, and imported cells is superseded by Same-Run Raw v2 so the
outcome and rejection partition originate in one Wire-v2 execution.

The canonical machine-readable protocol is
`phase4_statistical_decision_protocol_v1.json`. It freezes analysis before
observing decision results; `protocol_frozen=true` and
`decision_not_evaluated=true` are its only state claims. It contains no winner,
observed commit, hardware, confidence result, or pass claim. Its compact JSON
must exactly reconstruct the validator literal and authenticate all preceding
fields with byte-stable FNV-1a domain
`APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V1`.

## Matrix and authority

The protocol binds representative-manifest checksum `7311872938254494931`, all
38 successful case fingerprints and pool schedules, excluded fixed-query
fingerprints `2000=8203613321943675931` and
`2001=11000562598404360444`, and compiled-work-bound stress fingerprints for
`3001/3002`. Canonical expansion is 104 unique `(case,pool)` cells: 100 can
produce successful Raw v1, 18 are calibration, and the noncalibration closure
contains 86 cells, of which 82 are Raw-success eligible. Role counts are exact
3, calibration 18, held-out 72, fixed-query 5, stress 3, and imported 3.
Missing required evidence, an unexpected failure, or a wrong disposition is
incomplete and is never converted into an allocator loss. Cases `2000/2001`
close only through their dedicated fixed-query artifacts; the expected typed
compiled-work-bound results for `3001/3002` close only through their stress
artifacts. Neither special disposition is an allocator win or loss.
Each checksummed cell group also fixes its evidence disposition, decision use,
closure membership, and complete required-artifact list; a downstream tool may
not substitute a diagnostic artifact for same-run decision telemetry.

## Outcome and inference

Each successful cell compares the candidate to the sequential baseline by the
ordered vector: maximize `selected_net_count`, minimize
`total_overuse_units`, then minimize `total_intrinsic_cost`. The experimental
unit is one of eight held-out cases in a synthetic family at primary pool
`K=8`. Twenty timing repetitions and pools `4/8/16` are strata, not additional
samples. Pools 4 and 16 are mandatory sensitivity disclosures and cannot
rescue the primary result.

For each family, discard lexicographic ties and compute the one-sided exact
sign probability `sum(C(n,k), k=w..n)/2^n` as a reduced rational. Apply
Holm-Bonferroni over family IDs 0, 1, and 2 at alpha `1/20`, sorting by exact
rational p-value then ascending family ID and stopping at the first failure.
At least two primary families must qualify and family 1,
`pin_field_crossbar_v1`, must be one. Publish wins, losses, ties, and the
inclusive tie-bounded win-fraction range as exact rationals
`[wins/8,(wins+ties)/8]`; do not pool families.

The descriptive two-sided 95% Clopper-Pearson interval applies to the non-tie
win probability. Endpoints solve the exact binomial-tail equations at `1/40`
and are deterministically rounded outward to parts per billion. With zero
non-tie observations the interval is tagged unavailable rather than invented.
This interval does not replace the exact rational sign decision.

## Guardrails

- No held-out cell at K 4, 8, or 16, and no imported cell, may lose a selected
  net. No pooling may offset a failing cell.
- Imported overuse must be no greater than baseline in every pool.
- Imported mean intrinsic cost per selected net must be at most `11/10` of
  baseline in every pool, checked by exact cross multiplication. When baseline
  selects zero and candidate selects more, this mean comparison is not
  applicable; when both select zero, both total costs must be zero.
- Whenever selected count and overuse tie, total intrinsic cost must not exceed
  baseline.
- Exact-validation rejection columns must be zero in both arms of exact,
  held-out, and imported decision cells, sourced from future same-run decision
  telemetry. Calibration, fixed-query, and stress diagnostics are outside this
  guardrail. A diagnostic rerun cannot decide it.

## Timing diagnostic

Parent-observed outer elapsed time is primary; prepared and cold times retain
their narrower scopes. Keep all 20 pairs with no outlier deletion. For each,
publish candidate-minus-baseline nanoseconds and candidate/baseline ratio in
ppm rounded half up; a zero denominator is forbidden. Publish the round-half-
up median of the middle two ratios, the exact n=20 median interval from sorted
order statistics 6 and 15 (coverage `125647/131072`, or `958611` ppm after
round-half-up), and separate AB and BA medians.
Make no direction claim when order strata disagree. Timing never decides Phase
4.

## Completion boundary

The protocol itself does not close Section 29.2. Completion additionally
requires exact-small exhaustive-oracle evidence, fixed-query controls, the
typed stress ladder, future same-run decision telemetry, complete stage timing,
CPU/GPU utilization context, compatible batch fill, prepared-view cache
behavior, and complete toolchain/hardware provenance.
