# Phase 4 Per-Net Report Publication Join v1

Publication Join v1 binds one canonical Per-Net Report Artifact v1 to one
complete, separately generated Isolated Raw Evidence v1 cell. The join does not
modify either wire format and never promotes diagnostic execution to measured
or decision-eligible evidence.

## Diagnostic runner

`phase4_per_net_report_runner` accepts only bounded `--name=value` arguments.
Every name is unique and every canonical configuration field is explicit:
case, pool, four workers, 20 repetitions, setup cap, all four external caps,
and all five corpus limits. Source is one 40-character lowercase commit that
must equal the clean stamped build.

The Raw association is explicit and nonzero:

```text
raw_cell_plan_checksum
raw_cell_artifact_checksum
raw_source_envelope_checksum
pair_attempt_checksum
paired_semantic_checksum
paired_artifact_checksum
baseline_semantic_checksum
baseline_arm_artifact_checksum
candidate_semantic_checksum
candidate_arm_artifact_checksum
```

The runner requires exactly four workers, 20 repetitions, and a case present in
the successful frozen workload-roster manifest, then reconstructs the cell plan
and Raw source envelope before any diagnostic contender runs. It resolves the
imported KiCad fixture through the fixed Bazel runfile and supplies those exact
bytes when case 4000 is rebuilt.
It creates repetition zero in baseline-first order, runs Sequential Baseline,
then runs Reusable Candidate Allocation with a persistent four-worker
preparer. Failure writes a bounded diagnostic to stderr and no report bytes to
stdout. Success writes exactly one canonical report object and one LF. No
shell, measured Raw worker, or caller-selected fixture path is involved.

## Publication validator

`phase4_per_net_report_validator` requires exactly:

```text
--expected-commit=<independent 40-lowercase-hex commit>
--raw=<canonical Raw v1 file>
--report=<canonical Per-Net Report v1 file>
```

Raw input is read once under a 64 MiB cap and passed through the complete Raw
v1 publication validator with canonical 20-repetition/four-worker rules. The
report is read once under a 32 MiB cap. Both readers reject invalid UTF-8,
duplicate keys, non-JSON constants, noncanonical bytes, and trailing data
before semantic admission.

The report parser rejects missing, extra, or reordered keys; wrong JSON types;
unknown enums; out-of-range integers; noncanonical optionals; more than 4,096
per-net rows; and every Per-Net Arm Telemetry v1 closure violation. It
independently reconstructs telemetry hashes, the complete frozen roster hash,
the report artifact hash, and the report source envelope. The independently
validated roster sidecar supplies the exact full EntityRef sequence, including
generation and the imported case-4000 IDs.

The file join requires equality of:

1. expected clean commit, stamp/dirty state, Raw Wire schema, complete config,
   corpus, cell plan, Raw artifact, and Raw source envelope;
2. repetition-zero baseline-first pair-attempt checksum, paired semantic and
   artifact checksums, and both arm semantic and artifact checksums; and
3. every field of both complete diagnostic semantics objects with the matching
   Raw records, including operational execution order and worker count.

The last comparison is structural rather than checksum-only. The report must
retain literal `decision_eligible=false`. No testing relaxation is exposed by
the publication validator.
