# Phase 4 Matrix Decision Publication v1

This contract closes the frozen 104-cell Statistical Decision Protocol v4
without changing its observation, inference, guardrail, timing, or completion
rules. A publication is emitted only after every required authority is present
and fully rebuilt. Missing, malformed, wrongly disposed, foreign-source, or
failed evidence is incomplete and produces no decision publication; it is
never converted into an allocator loss. An authentic complete matrix may
publish either `phase4_exit_status=passed` or `failed`.

## External evidence bundle

Evidence is collected outside the Git worktree so source stamping remains
clean. The aggregator accepts one resolved directory whose authoritative
inventory is exactly:

```text
cells/<four-digit-case>/k<pool>/
  raw.json
  same-run.json                         # Raw-v2 cells only
  per-net-report.json
  operational-capture.json
  operational-publication.json
  exact-snapshot.json                   # exact cells only
  exact-oracle.json                     # exact cells only
special/
  fixed-query.json
  stress-work-bound-probe.json
  stress.json
```

The layout is derived from Protocol v4 rather than an observed manifest. It
contains 100 Raw files, 78 same-run companions, 100 reports, 100 captures, 100
operational publications, three exact snapshots, three exact Oracle v2
artifacts, and the three shared special authorities: 487 files total. Missing,
extra, symlinked, hard-linked, duplicated, or noncanonical files reject the
bundle. Each file is read once into bounded memory, parsed as compact canonical
JSON with one terminal LF, and recorded by relative path, byte count, SHA-256,
artifact checksum, and optional source-envelope checksum.
The runner also creates a commit-bound `.phase4-root.json` ownership marker and
may create top-level `logs/`, `matrix/`, and `.phase4-run.lock` operator state.
Those paths are outside the authoritative inventory and cannot contribute
evidence. A nonempty unmarked root, a marker for another commit, or unknown
top-level state is rejected before observation or aggregation.

## Authority validation

The aggregator first validates the complete protocol ancestry:

- v1 checksum `6007340189832929403`;
- v2 checksum `16250482876258734537`;
- v3 checksum `14444535493088350158`;
- v4 checksum `10222448264116898730`;
- representative corpus checksum `7311872938254494931`; and
- the exact 102-entry canonical budget roster checksum
  `13115713216042861392`.

Each Raw-v1 cell is fully validated against an independently supplied clean
commit and rejects a same-run companion. Each Raw-v2 cell is validated through
its exact Same-Run Decision Telemetry join. The version-appropriate per-net
report is structurally joined to Raw. Operational Measurement Publication v1
is rebuilt from Raw, the optional same-run companion, its capture, and the
publication; parsing or a self-consistent checksum alone is insufficient.

Each exact cell additionally rebuilds its supplied Oracle v2 from Raw-v2,
same-run telemetry, the Wire-v2 report, and the exact snapshot and requires
`production_is_optimal=true`. Fixed-Query Control v1 is rebuilt from cases
2002-2004 and their legacy projections and closes descriptor-only cases
2000/2001. Stress Evidence v1 is rebuilt from case 3000, its legacy projection,
and the work-bound probe and closes cases 3001/3002. The shared fixed and stress
artifacts do not replace Protocol v4's separate Operational Measurement
requirement on their executed cells.

All 100 successful cells must share one complete Raw host environment and one
Operational Measurement reproducibility-provenance object. All evidence must
name the independently supplied clean commit.

## Normalized cells

Cells are serialized in ascending `(case_id, requested_pool_size)` order. Every
row binds its frozen role, evidence disposition, closure membership, required
authority checksums, and a per-cell checksum. Successful rows derive their
board outcome and all 20 paired timing observations only from validated Raw
authority. Descriptor-only and compiled-work-bound rows carry typed
not-applicable outcomes and never enter a win/loss count.

The board comparison is lexicographic: maximize selected-net count, minimize
overuse units, then minimize intrinsic cost. Raw repetitions must agree on the
deterministic board outcome. Timing retains every parent-observed outer pair,
AB/BA order, differences, half-up ppm ratios, median, order-statistic interval,
and order-stratum medians. Operational diagnostic replay timing is explicitly
not statistical timing authority.

## Guardrails and family decision

The unscoped equal-selected-and-overuse intrinsic-cost guardrail is evaluated
on all 100 successful cells. Selected-net non-regression is evaluated on all
held-out and imported pools. Imported cells additionally apply per-pool overuse
and exact `11/10` mean selected-cost rules. Exact-validation rejection is
evaluated only from Same-Run Decision Telemetry on all exact, held-out, and
imported cells. Authentic nonzero rejection is a guardrail failure, not
incomplete evidence, and no failing cell may be offset by another.

Primary family units are the eight held-out cases at pool eight. Pools four and
sixteen are sensitivity disclosures and repetitions are timing strata, not
samples. The publication records wins, losses, ties, exact reduced sign
probability, deterministic Holm rank and threshold, tie-bounded exact
fractions, and outward-rounded 95% Clopper-Pearson bounds. At least two
families must qualify and family one, the globally coupled pin-field family,
must be among them.

`phase4_complete=true` requires the family rule, every guardrail, and all nine
completion authorities. Timing never contributes to that boolean. Passing this
publication closes the Phase 4 global-allocation evidence gate only; it does
not claim legalization, host-CAD validation, specialty routing, or M1
completion.

## Serialization

The publication is compact canonical UTF-8 JSON with one terminal LF and a
16 MiB bound. It embeds the exact 487-file evidence registry, 104 normalized
cells, family results, guardrail failures, completion rows, host environment,
and reproducibility provenance.

`artifact_checksum` uses FNV-1a domain
`APGAR-PHASE4-MATRIX-DECISION-PUBLICATION-ARTIFACT-V1` over the complete
canonical payload excluding both root checksums. `source_envelope_checksum`
uses domain `APGAR-PHASE4-MATRIX-DECISION-PUBLICATION-SOURCE-V1`, schema,
source identity, and artifact checksum. Named output is installed atomically
without replacement and validation rebuilds the entire publication from the
external evidence bundle.
