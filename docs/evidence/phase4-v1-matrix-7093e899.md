# Phase 4 v1 Matrix Observation at `7093e899`

## Result

The first complete Statistical Decision Protocol v4 acquisition is an
authentic negative Phase 4 result.

| Field | Observed value |
| --- | --- |
| Source commit | `7093e8994dc86d0086813bf618bc41b68da05e0b` |
| Matrix evidence status | `complete` |
| Phase 4 exit status | `failed` |
| Phase 4 complete | `false` |
| Logical cells | 104 |
| Successful execution cells | 100 |
| Evidence files | 487 |
| Completion authorities | 9 of 9 complete |
| Decision artifact checksum | `5398643261125745587` |
| Source envelope checksum | `14015900436540636994` |
| Decision file SHA-256 | `3223b6ee9e5d1666ebe816888915957bd0caa1a1557e46ddc2196a9723feb933` |

The complete decision publication, including its 487-file content-hash
registry, is retained beside this report as
`phase4-v1-matrix-7093e899-decision-publication.json`. The raw authority bundle
is intentionally not checked into Git; complete independent revalidation still
requires that external bundle.

The acquisition root used for this observation was:

```text
/home/adam/apgar-phase4-evidence/7093e8994dc86d0086813bf618bc41b68da05e0b
```

The acquisition completed successfully. The runner's zero exit status means
that it produced a complete authentic decision, not that the decision passed.

## Protocol and environment bindings

The publication binds:

- Protocol schema versions 1 through 4 with artifact checksums
  `6007340189832929403`, `16250482876258734537`,
  `14444535493088350158`, and `10222448264116898730`;
- Representative Corpus v1 checksum `7311872938254494931`;
- the 102-cell canonical budget roster checksum
  `13115713216042861392`;
- Linux kernel `6.18.33.2-microsoft-standard-WSL2`;
- `x86_64`;
- AMD Ryzen 9 9950X3D 16-Core Processor;
- clang 22.1.8; and
- host-environment checksum `13806960821335572206`.

The Phase 4 decision is CPU-only. GPU measurements are not used.

## Guardrails

The selected-net, equal-selected-and-overuse-cost, and imported non-regression
guardrails have no failures.

The zero exact-validation rejection guardrail fails in 75 cells: all three
synthetic exact cells and all 72 synthetic held-out cells. The three imported
cells pass. Across the 78 Same-Run Decision Telemetry artifacts and all 20
repetitions:

- the sequential baseline reports 188,880 exact-validation rejections; and
- the reusable-candidate contender reports 1,602,280 exact-validation
  rejections.

Representative authenticated totals are:

| Cell | Baseline | Candidate |
| --- | ---: | ---: |
| exact case 100, pool 4 | 60 | 240 |
| held-out case 1000, pool 4 | 1,720 | 6,880 |
| imported case 4000, pool 4 | 0 | 0 |

The validator and aggregator correctly apply the frozen zero-rejection rule.
These counts must be fixed at their routing, geometry, or corpus source rather
than reinterpreted.

Ephemeral adversarial debug probes suggested
`candidate.geometry.swept_self_overlap.v1` in synthetic portal routes and
`candidate.terminals.unintended.v1` in synthetic pin-field routes. These
invariants are remediation hypotheses only. The probes and invariant IDs are
not authenticated fields in the decision publication and are not presented as
durable causal evidence.

## Family decision

Primary pool size is 8.

| Family | Wins | Losses | Ties | Holm-qualified |
| --- | ---: | ---: | ---: | --- |
| `portal_channels_v1` | 0 | 0 | 8 | no |
| `pin_field_crossbar_v1` | 8 | 0 | 0 | yes |
| `fragmented_maze_v1` | 0 | 0 | 8 | no |

Portal candidate outcomes are exactly equal to the baseline outcomes. For
example, case 1000 selects 170 nets with 42 overuse units and intrinsic cost
3,422,800 in both arms. Candidate generation requests many columns but retains
no useful alternative for the overused portal.

Fragmented-maze outcomes select 192 nets with zero overuse and intrinsic cost
1,766,400 in both arms. Diversity among already-routable nets cannot improve
the primary outcome while the other 192 pools remain empty.

Pin-field improves selected-net count from 128 to 256 in every held-out case,
so it is the only qualifying family.

## Revalidation

With the external evidence bundle present, rebuild and validate the publication
with:

```sh
bazel run //:phase4_matrix_aggregator -- \
  --expected-commit=7093e8994dc86d0086813bf618bc41b68da05e0b \
  --evidence-root=/home/adam/apgar-phase4-evidence/7093e8994dc86d0086813bf618bc41b68da05e0b \
  --validate=/home/adam/apgar-phase4-evidence/7093e8994dc86d0086813bf618bc41b68da05e0b/matrix/decision-publication.json
```

The expected validation result is a complete publication whose Phase 4
decision remains `failed`.
