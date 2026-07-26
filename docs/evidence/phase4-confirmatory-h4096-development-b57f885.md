# Phase 4 Confirmatory H=4096 Development Observation

## Result

The two authorized H=4096 development cells produce a narrow, authentic
negative result. The exact cell proves that production selects the unique
optimum inside its captured pools, but that optimum retains one overuse unit.
The ordinary calibration cell then prefers the zero-resource-overuse
sequential baseline in all 20 paired repetitions because the reusable
candidate introduces 16 overuse units.

These are separate source-bound observations, not a cross-commit artifact
join.

| Development role | Exact source commit | Narrow result |
| --- | --- | --- |
| Exact `(10100,4)` | `ec3d22dab978f797c06180c3821b1b97d6e80fa1` | Production equals the unique fixed-pool optimum; one overuse unit remains |
| Calibration `(10200,8)` | `b57f885ed2dc820bfb26b3b0742810286338e9c6` | Baseline preferred in 20 of 20 pairs |

## Exact-cell fixed-pool result

The exact Raw artifact contains 20 complete pairs, balanced as 10
baseline-first and 10 candidate-first executions. The candidate is preferred
in all 20.

| Arm | Selected nets | Overused resources | Overuse units | Intrinsic cost |
| --- | ---: | ---: | ---: | ---: |
| Sequential baseline | 6 | 2 | 2 | 132,600 |
| Reusable candidate | 6 | 1 | 1 | 159,000 |

The final-pool snapshot has a complete Cartesian product of eight worlds.
Independent candidate-admission replay and exhaustive enumeration find one
unique optimum at `(selected=6,overuse_units=1,intrinsic_cost=159000)`, with
one overused resource. Production equals that optimum.

This proves only fixed-pool optimality for exact development cell `(10100,4)`
under the H=4096 authority. It does not prove resource feasibility or route
completeness outside the captured pools.

## Calibration result

The ordinary Raw artifact contains 20 complete pairs, balanced as 10
baseline-first and 10 candidate-first executions. The outcome is deterministic
across all repetitions.

| Arm | Selected nets | Overused resources | Overuse units | Intrinsic cost | Pair result |
| --- | ---: | ---: | ---: | ---: | --- |
| Sequential baseline | 64 | 0 | 0 | 1,874,600 | Preferred 20 of 20 |
| Reusable candidate | 64 | 16 | 16 | 1,520,200 | Preferred 0 of 20 |

The board-level objective is lexicographic. Equal selected-net count makes
overuse decisive, so the candidate's lower intrinsic cost is not a win.

The repetition-zero per-net report closes arithmetically:

| Diagnostic | Baseline | Reusable candidate |
| --- | ---: | ---: |
| Requested and executed columns | 448 | 640 |
| Admitted candidates | 448 | 111 |
| Rejected columns | 0 | 529 |
| Duplicate candidates | 0 | 465 |
| Disconnected columns | 0 | 64 |
| Exact-validation rejections | 0 | 0 |
| Other, unsupported, or skipped | 0 | 0 |
| Final candidate total | 64 | 96 |
| Final pool minimum / maximum | 1 / 1 | 1 / 2 |
| Pools below requested size 8 | 64 | 64 |
| Selected above pool-best intrinsic cost | 0 | 16 |
| Aggregate positive difference from pool-best | 0 | 356,400 |

The diagnostic is `decision_eligible=false`. The counts demonstrate sparse
retained diversity and locate candidate-generation, retention, pricing,
regeneration, and allocation as remediation surfaces. They do not establish
which surface caused the final overuse.

## Operational diagnostics

The ordinary operational capture records four isolated processes in canonical
order: measured baseline, measured candidate, unmeasured baseline replay, and
unmeasured candidate replay. The final publication completely joins Raw
repetition zero, both measured processes, both replay authorities, and the
pinned worker.

| Arm | Fork-to-reap wall | Process CPU | Peak host memory |
| --- | ---: | ---: | ---: |
| Sequential baseline | 1,083,927,715 ns | 1,078,021,000 ns | 37,240,832 B |
| Reusable candidate | 465,817,384 ns | 512,700,000 ns | 35,008,512 B |

These one-capture values are descriptive only. The publication is
non-statistical, non-standalone, and incomplete coverage; no speedup or
performance-decision claim is made.

## Artifact preservation and identities

The preserved external evidence roots are:

```text
/home/adam/apgar-phase4-evidence/ec3d22dab978f797c06180c3821b1b97d6e80fa1/development/exact
/home/adam/apgar-phase4-evidence/b57f885ed2dc820bfb26b3b0742810286338e9c6/development/calibration
```

The exact `SHA256SUMS` file hash is
`0fad8766618991641affe292c2277d823413bd1831bbeed79c0249c65c54c212`.
The calibration `SHA256SUMS` file hash is
`67d65d280ef078bff1b1fbbc14c37a7970c5a6a8a7ea637e90479302efe34c81`.

| Artifact | File SHA-256 | Artifact checksum | Source-envelope checksum |
| --- | --- | ---: | ---: |
| Exact Raw | `c1efe5f49bf9e47ae17cfe4420578f3c710419e2303091adb466b9683a9dfa9d` | 4,619,426,935,301,049,148 | 13,930,757,554,570,051,305 |
| Exact Same-Run telemetry | `857207d6489585aa9a25bcc919bc4f999645a0d45dd44049ff47192e437a2e31` | 2,624,622,634,749,295,557 | 4,119,814,046,081,883,377 |
| Exact per-net report | `3578ecdea127d61a14746c52bc2d7863c0bf7282762e775c8772ef3255c29119` | 4,535,004,164,332,373,856 | 4,967,405,723,010,280,676 |
| Exact operational capture | `c83d0dc629298b056bad2671b1c6b7346848ecd000f5cdbbfdceeea326bcc232` | 11,715,898,689,696,313,249 | 16,883,067,188,142,353,836 |
| Exact operational publication | `6fbabfdeaed6d9f864e628829611c3027056c0b6684324e4f40b28c5b565bc89` | 3,372,186,974,896,244,959 | 13,646,303,591,862,991,298 |
| Exact snapshot | `49b378bb1fa8785e3e11844dc6465a45601597a24b40115599265dad1b4f528d` | 12,588,665,287,159,496,202 | 3,508,896,190,859,788,304 |
| Exact Oracle | `b496bda13d170ca233b868a377025738b448c964ced8c9ca9543a1850ab2e850` | 6,017,247,987,672,348,007 | 15,292,840,419,661,328,525 |
| Calibration Raw | `2bd78d740ee28c41a08efc457796f7898182171965caca6b819cd133ab852572` | 6,924,030,329,952,509,201 | 6,299,699,737,248,172,501 |
| Calibration per-net report | `ebec82b058b4442d79515e0637e0341b0d95d1817fb5afbe916718600145f335` | 10,966,469,809,160,728,882 | 3,192,371,152,911,581,884 |
| Calibration operational capture | `5e9262f2076c8448d3c4a7ad6cf2824ad17252b40562ea9f35e493cd705ab8ee` | 9,909,047,655,253,954,469 | 16,877,607,197,135,106,241 |
| Calibration operational publication | `eadce2d0fafbd0f494d17a527303c5c39986788e78b3686075acc60c306f5bc7` | 9,140,010,409,264,111,424 | 12,035,393,906,815,931,947 |

The full bundles are retained outside Git because they are development
evidence, not standalone decisions. This checked-in report preserves their
source identities, registries, observed outcomes, and interpretation boundary.

## Validation and adversarial review

Every acquisition and validation step used the canonical Bazel targets under
`--config=benchmark`. The exact chain passed the named Same-Run
Raw/telemetry, per-net, operational, snapshot/replay, and Oracle validators.
The calibration chain passed:

- `phase4_confirmatory_h4096_raw_evidence_validator`;
- `phase4_confirmatory_h4096_per_net_report_validator`; and
- `phase4_confirmatory_h4096_operational_measurement_validator`.

Three independent adversarial lanes rechecked the calibration bundle:

- complete checksum and source-envelope closure, public validators, worker and
  toolchain provenance, and standalone-runfiles content;
- all Raw, report, capture, replay, process, and publication arithmetic; and
- fresh-acquisition/no-reuse behavior, negative cross-join rejection, atomic
  installation, and interpretation boundaries.

All three approved without blockers. The bundle remained clean-source-bound to
`b57f885ed2dc820bfb26b3b0742810286338e9c6`.

To check external file integrity:

```sh
(cd /home/adam/apgar-phase4-evidence/ec3d22dab978f797c06180c3821b1b97d6e80fa1/development/exact && sha256sum -c SHA256SUMS)
(cd /home/adam/apgar-phase4-evidence/b57f885ed2dc820bfb26b3b0742810286338e9c6/development/calibration && sha256sum -c SHA256SUMS)
```

## Interpretation boundary

This observation does not establish statistical performance, candidate-pool
completeness, global optimality outside the exact captured pools, calibration
fixed-pool optimality, resource feasibility, combined-board legality, final
legality, campaign coverage, matrix success, Phase 4 completion, or M1
completion. Heldout, imported, fixed-query, stress, aggregation, matrix, and
decision paths remain closed.
