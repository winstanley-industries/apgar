"""Corruption-focused tests for the Phase 3 v3 evidence validator."""

from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import tempfile
import unittest

from tools import validate_phase3_followup_evidence as validator


def _context(commit: str) -> dict[str, object]:
    context: dict[str, object] = dict(validator.REQUIRED_CONTEXT)
    context.update(
        {
            "apgar_commit": commit,
            "apgar_parallel_host_workers": "32",
            **{key: "test" for key in validator.REQUIRED_METADATA},
        }
    )
    identities = ",".join(str(value) for value in range(max(validator.CANDIDATE_COUNTS)))
    for index, case in enumerate(validator.CASES):
        context[f"apgar_case_{case}"] = (
            f"family=test;nodes=10;persistent_device_bytes={1000 + index};"
            "prepared_node_lookup_host_bytes=40"
        )
        context[f"apgar_case_{case}_policy_identities"] = identities
    return context


def _base_counters(generator: str, stage: str, count: int, persistent: int) -> dict[str, float]:
    counters = {key: 0.0 for key in validator.COMMON_COUNTERS}
    counters.update(
        {
            "requested_candidate_count": float(count),
            "reached_queries": float(count),
            "base_policy_scalar_cost": 1.0,
            "minimum_reported_policy_scalar_cost": 1.0,
            "reachable_policy_scalar_cost_sum": float(count),
            "deterministic": 1.0,
            "differential_match": 1.0,
            "ordered_results": 1.0,
            "process_lifetime_peak_rss_bytes": 4096.0,
            "process_peak_rss_available": 1.0,
            "semantic_outcome_checksum_hi": 7.0,
            "semantic_outcome_checksum_lo": 11.0,
            "outcome_checksum_hi": 13.0,
            "outcome_checksum_lo": 17.0,
        }
    )
    if generator == "sequential_cpu_astar":
        counters["parallel_host_workers"] = 1.0
    elif generator == "parallel_cpu_astar":
        counters["parallel_host_workers"] = float(min(count, 32))
    else:
        capacity = count * 1000
        counters.update(
            {
                "persistent_owned_vram_bytes": float(persistent),
                "batch_owned_vram_bytes": float(capacity),
                "workspace_capacity_vram_bytes": float(capacity),
                "peak_owned_vram_bytes": float(persistent + capacity),
                "prepared_node_lookup_host_bytes": 40.0,
                "gpu_batch_owned_host_bytes": float(count * 100),
                "device_to_host_readback_bytes": float(
                    count
                    * (
                        validator.RESULT_HEADER_BYTES
                        + validator.COMPACT_PATH_HEADER_BYTES
                        + validator.COMPACT_PATH_STATE_BYTES
                    )
                ),
                "chunk_rounds": 8.0,
                "dispatched_rounds": 8.0,
                "blocking_status_readback_count": 1.0,
                "kernel_launch_count": 36.0,
                "rounds_maximum": 8.0,
                "rounds_total": float(count * 8),
                "parallel_host_workers": float(validator._expected_worker_count(count)),
            }
        )
    if stage != "execution_readback":
        counters.update({key: 0.0 for key in validator.ADMISSION_COUNTERS})
        counters.update(
            {
                "accepted_candidates": float(count),
                "retained_candidate_pool_size": float(count),
                "unique_geometry_signatures": float(count),
                "unique_resource_signatures": float(count),
                "nondominated_candidates": float(count),
                "accepted_logical_bytes": float(count * 10),
                "peak_deterministic_host_bytes": float(count * 20),
                "accepted_candidate_yield": 1.0,
                "generated_candidate_acceptance": 1.0,
                "resource_diversity": 1.0,
                "geometric_diversity": 1.0,
                "candidate_order_checksum_hi": 19.0,
                "candidate_order_checksum_lo": 23.0,
            }
        )
    return counters


def _aggregate_rows(run_name: str, real_time: float, counters: dict[str, float]) -> list[dict]:
    values = {"mean": real_time, "median": real_time, "stddev": 1.0, "cv": 1.0 / real_time}
    rows = []
    for aggregate in validator.AGGREGATES:
        row = {
            "name": f"{run_name}_{aggregate}",
            "run_name": run_name,
            "run_type": "aggregate",
            "repetitions": validator.REPETITIONS,
            "aggregate_name": aggregate,
            "real_time": values[aggregate],
            "time_unit": "us",
        }
        if aggregate == "median":
            row.update(counters)
        rows.append(row)
    return rows


def _result(commit: str) -> dict:
    rows: list[dict] = []
    for case_index, case in enumerate(validator.CASES):
        upload = (
            f"phase3/shared_cuda/prepared_upload/{case}/"
            "min_time:0.020/min_warmup_time:0.010/repeats:20/real_time"
        )
        rows.extend(
            _aggregate_rows(
                upload,
                50.0,
                {
                    "persistent_owned_vram_bytes": float(1000 + case_index),
                    "prepared_node_lookup_host_bytes": 40.0,
                    "prepared_uploads_per_second": 1.0,
                },
            )
        )
    times = {
        "sequential_cpu_astar": 100.0,
        "parallel_cpu_astar": 90.0,
        "batched_cuda_sweep": 80.0,
    }
    for generator in validator.GENERATORS:
        for stage in validator.STAGES:
            for case_index, case in enumerate(validator.CASES):
                for count in validator.CANDIDATE_COUNTS:
                    run_name = (
                        f"phase3/{generator}/{stage}/{case}/k_{count}/"
                        "min_time:0.020/min_warmup_time:0.010/repeats:20/real_time"
                    )
                    rows.extend(
                        _aggregate_rows(
                            run_name,
                            times[generator],
                            _base_counters(generator, stage, count, 1000 + case_index),
                        )
                    )
    return {"context": _context(commit), "benchmarks": rows}


class Phase3FollowupEvidenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temporary.name)
        self.commit = "a" * 40
        self.result_path = self.root / "result.json"
        self.report_path = self.root / "report.md"
        self.decision_path = self.root / "decision.md"
        self.manifest_path = self.root / "manifest.json"
        self.result = _result(self.commit)
        runs, _ = validator._validate_rows(self.result["benchmarks"], self.result["context"])
        correctness = validator._semantic_summary(runs)
        comparisons = validator._comparisons(runs)
        correctness_token = json.dumps(correctness, sort_keys=True, separators=(",", ":"))
        comparison_token = json.dumps(comparisons, sort_keys=True, separators=(",", ":"))
        bound = "result.json\n" + self.commit + "\n" + correctness_token + "\n" + comparison_token
        self.report_path.write_text(bound, encoding="utf-8")
        self.decision_path.write_text(bound, encoding="utf-8")
        self.manifest = {
            "schema": validator.MANIFEST_SCHEMA,
            "result_schema": validator.RESULT_SCHEMA,
            "source_commit": self.commit,
            "result_file": "result.json",
            "report_file": "report.md",
            "decision_file": "decision.md",
            "expected_correctness_summary": correctness,
            "expected_stage_comparisons": comparisons,
        }
        self._write_result_and_manifest()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_result_and_manifest(self) -> None:
        self.result_path.write_text(json.dumps(self.result), encoding="utf-8")
        self.manifest["result_sha256"] = hashlib.sha256(self.result_path.read_bytes()).hexdigest()
        self.manifest["report_sha256"] = hashlib.sha256(self.report_path.read_bytes()).hexdigest()
        self.manifest["decision_sha256"] = hashlib.sha256(
            self.decision_path.read_bytes()
        ).hexdigest()
        result_digest = str(self.manifest["result_sha256"])
        for path in (self.report_path, self.decision_path):
            text = path.read_text(encoding="utf-8")
            if result_digest not in text:
                path.write_text(text + "\n" + result_digest, encoding="utf-8")
        self.manifest["report_sha256"] = hashlib.sha256(self.report_path.read_bytes()).hexdigest()
        self.manifest["decision_sha256"] = hashlib.sha256(
            self.decision_path.read_bytes()
        ).hexdigest()
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def _median(self, generator: str, stage: str, case: str, count: int) -> dict:
        prefix = f"phase3/{generator}/{stage}/{case}/k_{count}/"
        return next(
            row
            for row in self.result["benchmarks"]
            if row["run_name"].startswith(prefix) and row["aggregate_name"] == "median"
        )

    def test_accepts_exact_v3_fixture(self) -> None:
        validator.validate(self.root, self.manifest_path)

    def test_rejects_missing_matrix_row(self) -> None:
        self.result["benchmarks"].pop()
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "row count"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_duplicate_aggregate(self) -> None:
        self.result["benchmarks"][1] = copy.deepcopy(self.result["benchmarks"][0])
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "duplicate aggregate"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_wrong_sweep_fixed_launch_identity(self) -> None:
        row = self._median("batched_cuda_sweep", "execution_readback", validator.CASES[0], 4)
        row["kernel_launch_count"] += 1
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "launch accounting"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_capacity_peak_corruption(self) -> None:
        row = self._median("batched_cuda_sweep", "execution_readback", validator.CASES[0], 4)
        row["peak_owned_vram_bytes"] += 1
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "simultaneous peak"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_semantic_parity_corruption(self) -> None:
        row = self._median("batched_cuda_sweep", "end_to_end", validator.CASES[0], 4)
        row["semantic_outcome_checksum_lo"] += 1
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "differs from CPU oracle"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_prepared_cold_admission_corruption(self) -> None:
        row = self._median("batched_cuda_sweep", "end_to_end", validator.CASES[0], 4)
        row["accepted_logical_bytes"] += 1
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "prepared and cold admission differ"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_changed_geometric_mean_speedup(self) -> None:
        prefix = f"phase3/batched_cuda_sweep/execution_readback/{validator.CASES[0]}/k_4/"
        for aggregate in ("mean", "median"):
            row = next(
                item
                for item in self.result["benchmarks"]
                if item["run_name"].startswith(prefix) and item["aggregate_name"] == aggregate
            )
            row["real_time"] = 79.0
        cv = next(
            item
            for item in self.result["benchmarks"]
            if item["run_name"].startswith(prefix) and item["aggregate_name"] == "cv"
        )
        cv["real_time"] = 1.0 / 79.0
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "stage comparisons"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_changed_uncertainty_summary(self) -> None:
        prefix = f"phase3/batched_cuda_sweep/execution_readback/{validator.CASES[0]}/k_4/"
        row = next(
            row
            for row in self.result["benchmarks"]
            if row["run_name"].startswith(prefix) and row["aggregate_name"] == "stddev"
        )
        row["real_time"] = 1000.0
        cv = next(
            item
            for item in self.result["benchmarks"]
            if item["run_name"] == row["run_name"] and item["aggregate_name"] == "cv"
        )
        cv["real_time"] = 1000.0 / 80.0
        self._write_result_and_manifest()
        with self.assertRaisesRegex(validator.EvidenceError, "stage comparisons"):
            validator.validate(self.root, self.manifest_path)

    def test_rejects_document_hash_corruption(self) -> None:
        self.report_path.write_text(self.report_path.read_text() + "tamper", encoding="utf-8")
        with self.assertRaisesRegex(validator.EvidenceError, "report checksum mismatch"):
            validator.validate(self.root, self.manifest_path)


if __name__ == "__main__":
    unittest.main()
