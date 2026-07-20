# Phase 3 Follow-up Evidence Manifest v1

This manifest binds one canonical persistent-workspace/compact-readback Phase 3
benchmark to its exact implementation commit and conclusions. Its `schema` is
`phase3_followup_evidence_manifest_v1`; `result_schema` is exactly
`phase3_candidate_bakeoff_v3`. It is additive and does not reinterpret the
immutable Phase 3 v1/v2 manifest, artifacts, or validator.

The manifest has these required fields:

- `source_commit`: the exact 40-character lowercase implementation commit;
- `result_file`, `report_file`, and `decision_file`: safe repository-relative
  ordinary-file paths;
- `result_sha256`, `report_sha256`, and `decision_sha256`: lowercase SHA-256
  digests of the exact files;
- `expected_correctness_summary`: the validator-derived object containing
  `generator_stage_medians`, `unique_requested_queries`, `reached_queries`,
  `unreachable_queries`, and `unexpected_failure_queries`; and
- `expected_stage_comparisons`: one object for each `execution_readback`,
  `prepared_end_to_end`, and `end_to_end` stage.

Each stage-comparison object contains exactly these derived values:
`comparisons`, `nominal_cuda_median_wins`, `nominal_cpu_median_wins`,
`nominal_median_ties`, `clear_cuda_mean_wins`, `clear_cpu_mean_wins`, and
`unclear_mean_differences`, plus `equal_row_geomean_cuda_speedup` and
`pooled_cuda_throughput_speedup`. The nominal comparison uses CUDA sweep's
median real time against the lower of the two CPU medians for the same case
and candidate count.

The equal-row speedup is the geometric mean of the 88 same-row ratios
`fastest_cpu_median / cuda_median`, so every case/count row has equal weight.
The pooled throughput speedup is `sum(fastest_cpu_median) /
sum(cuda_median)`, so higher-latency work has greater weight. Both are reported
because they answer different workload-weighting questions.

The uncertainty-qualified comparison uses the aggregate mean and standard
deviation with the fixed twenty repetitions. For each CPU mode, the unpaired
mean-difference margin is

```text
1.96 * sqrt((cuda_stddev^2 + cpu_stddev^2) / 20)
```

CUDA is a clear win only when its mean plus that margin is below each CPU
mean. CPU is a clear win when at least one CPU mean plus its corresponding
margin is below the CUDA mean. All other comparisons are unclear. This is a
declared aggregate-only normal approximation, not a paired-trial analysis,
median confidence interval, or substitute for retained raw repetition data.

The validator independently requires the exact filtered matrix: eleven shared
prepared-upload runs and three generators by three timed stages by eleven
cases by eight candidate counts, with exactly four aggregate rows per run and
no extras. It checks the clean exact-commit context, correctness partitions,
ordered semantic parity, admission repeatability, compact-readback bounds,
fixed sweep launches, reusable-workspace capacity peak, and required memory
ownership identities before deriving either manifest summary.

Both the report and ADR are byte-hash-bound. Each must name the result path,
result digest, source commit, and contain the canonical compact JSON rendering
of both validator-derived summary objects. This makes conclusion checks
machine-readable while leaving the surrounding prose unconstrained.

The canonical validator invocation is:

```sh
bazel run //:phase3_followup_evidence_validator -- REPOSITORY_ROOT MANIFEST
```

The validator target deliberately requires an explicit manifest until a real,
clean-stamped v3 artifact exists; this schema does not authorize fabricated or
placeholder evidence.
