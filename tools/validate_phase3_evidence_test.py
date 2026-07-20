"""Hostile tests for the Phase 3 published-evidence validator."""

import hashlib
import json
import os
import pathlib
import shutil
import tempfile
import unittest

from tools import validate_phase3_evidence


class Phase3EvidenceValidatorTest(unittest.TestCase):
    def setUp(self) -> None:
        runfiles_root = pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
        self._temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self._temporary.cleanup)
        self.root = pathlib.Path(self._temporary.name)
        self.paths = (
            "benchmarks/results/phase3_candidate_bakeoff_3ff9f61.json",
            "benchmarks/results/phase3_candidate_bakeoff_manifest_v1.json",
            "benchmarks/phase3_candidate_dispatch_diversity_report.md",
            "docs/adr/ADR-013-phase3-candidate-dispatch-conclusions.md",
        )
        for relative in self.paths:
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(runfiles_root / relative, destination)
        self.manifest_path = self.root / self.paths[1]

    def _rewrite_result_and_checksum(self, result: dict[str, object]) -> None:
        result_path = self.root / self.paths[0]
        result_path.write_text(json.dumps(result), encoding="utf-8")
        self._refresh_manifest_checksum("result_sha256", result_path)

    def _refresh_manifest_checksum(self, field: str, path: pathlib.Path) -> None:
        manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        manifest[field] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    def _rewrite_bound_document(
        self, path_index: int, checksum_field: str, old: str, new: str
    ) -> None:
        path = self.root / self.paths[path_index]
        original = path.read_text(encoding="utf-8")
        self.assertIn(old, original)
        path.write_text(original.replace(old, new), encoding="utf-8")
        self._refresh_manifest_checksum(checksum_field, path)

    def _load_result(self) -> dict[str, object]:
        return json.loads((self.root / self.paths[0]).read_text(encoding="utf-8"))

    def _set_manifest_result_schema(self, result_schema: str) -> None:
        manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        manifest["result_schema"] = result_schema
        self.manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    def _upgrade_result_to_v2(
        self, result: dict[str, object], *, include_retained_rejections: bool = True
    ) -> None:
        context = result["context"]
        context["apgar_result_schema"] = "phase3_candidate_bakeoff_v2"
        context.update(validate_phase3_evidence._V2_REQUIRED_CONTEXT)
        case_ordinals = {
            case: index + 1 for index, case in enumerate(validate_phase3_evidence._CASES)
        }
        for row in result["benchmarks"]:
            match = validate_phase3_evidence._ROW_PATTERN.match(row["name"])
            if match is None:
                continue
            _, stage, case, count_text, _ = match.groups()
            invariant_aggregate = row["aggregate_name"] in ("mean", "median")
            row["semantic_outcome_checksum_hi"] = case_ordinals[case] if invariant_aggregate else 0
            row["semantic_outcome_checksum_lo"] = int(count_text) if invariant_aggregate else 0
            if include_retained_rejections and stage != "execution_readback":
                row["retained_rejection_records"] = row["rejected_candidates"]

    def test_rejects_raw_result_checksum_drift(self) -> None:
        result_path = self.root / self.paths[0]
        result_path.write_bytes(result_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "checksum mismatch"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_raw_report_checksum_drift(self) -> None:
        report_path = self.root / self.paths[2]
        report_path.write_bytes(report_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "report checksum mismatch"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_raw_decision_checksum_drift(self) -> None:
        decision_path = self.root / self.paths[3]
        decision_path.write_bytes(decision_path.read_bytes() + b"\n")
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "ADR checksum mismatch"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_missing_matrix_row_even_with_updated_checksum(self) -> None:
        result = json.loads((self.root / self.paths[0]).read_text(encoding="utf-8"))
        result["benchmarks"].pop()
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "four aggregates"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_report_summary_drift(self) -> None:
        self._rewrite_bound_document(
            2,
            "report_sha256",
            "| **Total** | **52** | **14** | **0** |",
            "| **Total** | **51** | **15** | **0** |",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "total row"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_determinism_summary_drift(self) -> None:
        self._rewrite_bound_document(
            2,
            "report_sha256",
            "All 792 generator/stage medians",
            "All 791 generator/stage medians",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "median-row summary"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_reachability_summary_drift(self) -> None:
        self._rewrite_bound_document(
            2,
            "report_sha256",
            "2,317 reachable",
            "2,316 reachable",
        )
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "reachable-query summary"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_decision_summary_drift_with_updated_checksum(self) -> None:
        self._rewrite_bound_document(
            3,
            "decision_sha256",
            "won 52 and parallel CPU A* won 14. CUDA won none.",
            "won 51 and parallel CPU A* won 15. CUDA won none.",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "dispatch conclusion"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_report_frontier_execution_summary_drift(self) -> None:
        self._rewrite_bound_document(
            2,
            "report_sha256",
            "CUDA frontier won one and CUDA sweep won",
            "CUDA frontier won ninety-nine and CUDA sweep won",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "execution conclusion"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_report_sweep_execution_summary_drift(self) -> None:
        self._rewrite_bound_document(
            2,
            "report_sha256",
            "CUDA sweep won\nfour execution/readback comparisons",
            "CUDA sweep won\nninety-nine execution/readback comparisons",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "execution conclusion"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_decision_frontier_execution_summary_drift(self) -> None:
        self._rewrite_bound_document(
            3,
            "decision_sha256",
            "frontier nominally won one",
            "frontier nominally won ninety-nine",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "execution conclusion"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_decision_sweep_execution_summary_drift(self) -> None:
        self._rewrite_bound_document(
            3,
            "decision_sha256",
            "CUDA sweep won only four",
            "CUDA sweep won only ninety-nine",
        )
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "execution conclusion"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_manifest_correctness_summary_drift(self) -> None:
        manifest = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        manifest["expected_correctness_summary"]["reached_queries"] = 2316
        self.manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "correctness summary"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_nonfinite_numeric_value(self) -> None:
        result = json.loads((self.root / self.paths[0]).read_text(encoding="utf-8"))
        result["benchmarks"][0]["real_time"] = float("nan")
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "finite and nonnegative"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_negative_numeric_value(self) -> None:
        result = json.loads((self.root / self.paths[0]).read_text(encoding="utf-8"))
        result["benchmarks"][0]["real_time"] = -1.0
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "finite and nonnegative"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_boolean_json_schema_version(self) -> None:
        result = self._load_result()
        result["context"]["json_schema_version"] = True
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "json_schema_version mismatch"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_actual_google_benchmark_library_version_drift(self) -> None:
        result = self._load_result()
        result["context"]["library_version"] = "1.9.4"
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "library_version mismatch"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_missing_counter_from_nonmedian_aggregate(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "mean"
            and validate_phase3_evidence._ROW_PATTERN.match(row["name"]) is not None
        )
        del row["kernel_launch_count"]
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "kernel_launch_count"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_scalar_cost_drift_even_when_differential_flag_is_set(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "median"
            and row["name"].startswith(
                "phase3/batched_cuda_frontier/execution_readback/dense_corridors/k_4/"
            )
        )
        self.assertEqual(row["differential_match"], 1.0)
        row["base_policy_scalar_cost"] += 1
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "scalar costs differ"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_failure_accounting_drift(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "median"
            and row["name"].startswith(
                "phase3/sequential_cpu_astar/execution_readback/dense_corridors/k_4/"
            )
        )
        row["failed_queries"] += 1
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "do not cover"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_cuda_launch_accounting_drift(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "median"
            and row["name"].startswith(
                "phase3/batched_cuda_frontier/execution_readback/dense_corridors/k_4/"
            )
        )
        row["kernel_launch_count"] += 1
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "launch accounting"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_rejects_admission_partition_drift(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "median"
            and row["name"].startswith(
                "phase3/sequential_cpu_astar/exact_admission_store/dense_corridors/k_4/"
            )
        )
        row["rejected_candidates"] += 1
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, "do not partition"):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_accepts_synthetic_v2_counter_contract(self) -> None:
        result = self._load_result()
        self._upgrade_result_to_v2(result)
        context = result["context"]
        validate_phase3_evidence._validate_context(
            context,
            context["apgar_commit"],
            "phase3_candidate_bakeoff_v2",
        )
        medians = validate_phase3_evidence._validate_rows(
            result["benchmarks"], "phase3_candidate_bakeoff_v2"
        )
        correctness = validate_phase3_evidence._correctness_summary(
            medians, "phase3_candidate_bakeoff_v2"
        )
        self.assertEqual(correctness["generator_stage_medians"], 792)

    def test_v2_rejects_ordered_policy_outcome_drift(self) -> None:
        result = self._load_result()
        self._upgrade_result_to_v2(result)
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "median"
            and row["name"].startswith(
                "phase3/batched_cuda_sweep/execution_readback/dense_corridors/k_4/"
            )
        )
        row["semantic_outcome_checksum_lo"] += 1
        self._rewrite_result_and_checksum(result)
        self._set_manifest_result_schema("phase3_candidate_bakeoff_v2")
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "ordered policy outcomes differ"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_v2_rejects_missing_semantic_outcome_counter(self) -> None:
        result = self._load_result()
        self._upgrade_result_to_v2(result)
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "mean"
            and validate_phase3_evidence._ROW_PATTERN.match(row["name"]) is not None
        )
        del row["semantic_outcome_checksum_hi"]
        self._rewrite_result_and_checksum(result)
        self._set_manifest_result_schema("phase3_candidate_bakeoff_v2")
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "semantic_outcome_checksum_hi"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_v2_rejects_missing_or_mutated_required_context(self) -> None:
        original = self._load_result()
        for key in validate_phase3_evidence._V2_REQUIRED_CONTEXT:
            with self.subTest(key=key, corruption="missing"):
                result = json.loads(json.dumps(original))
                del result["context"][key]
                self._rewrite_result_and_checksum(result)
                with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, key):
                    validate_phase3_evidence.validate(self.root, self.manifest_path)
            with self.subTest(key=key, corruption="mutated"):
                result = json.loads(json.dumps(original))
                result["context"][key] += ".corrupted"
                self._rewrite_result_and_checksum(result)
                with self.assertRaisesRegex(validate_phase3_evidence.EvidenceError, key):
                    validate_phase3_evidence.validate(self.root, self.manifest_path)

    def test_v2_rejects_missing_retained_rejection_accounting(self) -> None:
        result = self._load_result()
        row = next(
            row
            for row in result["benchmarks"]
            if row["aggregate_name"] == "mean"
            and validate_phase3_evidence._ROW_PATTERN.match(row["name"]) is not None
            and "retained_rejection_records" in row
        )
        del row["retained_rejection_records"]
        self._rewrite_result_and_checksum(result)
        with self.assertRaisesRegex(
            validate_phase3_evidence.EvidenceError, "retained_rejection_records"
        ):
            validate_phase3_evidence.validate(self.root, self.manifest_path)


if __name__ == "__main__":
    unittest.main()
