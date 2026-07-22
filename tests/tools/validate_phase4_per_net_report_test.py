"""Adversarial tests for the Phase 4 Raw/report publication join."""

from __future__ import annotations

import copy
import json
import pathlib
import tempfile
import unittest

from tests.tools import validate_phase4_raw_evidence_test as raw_test
from tools import validate_phase4_per_net_report as validator
from tools import validate_phase4_workload_net_roster_manifest as roster_validator

_COMMIT = "a" * 40


def _metrics(cost: int) -> dict[str, int]:
    return {
        "scalar_policy_cost": cost,
        "intrinsic_base_cost": cost,
        "orthogonal_step_count": 1,
        "diagonal_step_count": 0,
        "bend_count": 0,
        "line_primitive_count": 1,
        "via_count": 0,
        "axis_aligned_length_dbu": cost,
        "diagonal_projection_dbu": 0,
    }


def _telemetry(semantics: dict[str, object]) -> dict[str, object]:
    candidate = semantics["arm"] == 1
    reports: list[dict[str, object]] = []
    costs = [10, 10, 10, 10, 10, 50]
    requests = [2, 2, 2, 2, 1, 1]
    admitted = [2, 2, 1, 1, 1, 1] if candidate else [1] * 6
    rejected = [0, 0, 1, 1, 0, 0] if candidate else [1, 1, 1, 1, 0, 0]
    for index in range(6):
        pool = admitted[index] if candidate else 1
        pairs = 1 if pool == 2 else 0
        reports.append(
            {
                "schema_version": 1,
                "net": {"id": 1000 + index, "generation": 0},
                "columns": {
                    "requested_columns": requests[index],
                    "executed_route_queries": requests[index],
                    "admitted_candidates": admitted[index],
                    "duplicate_candidates": 0,
                    "disconnected_columns": 0,
                    "unsupported_columns": 0,
                    "skipped_columns": 0,
                    "exact_validation_rejections": 0,
                    "other_rejections": rejected[index],
                },
                "final_pool_size": pool,
                "unique_geometry_signature_count": pool,
                "unique_resource_signature_count": pool,
                "candidate_pair_count": pairs,
                "mean_resource_overlap_ppm": 500_000 if pairs else 0,
                "minimum_resource_overlap_ppm": 500_000 if pairs else 0,
                "mean_geometric_overlap_ppm": 250_000 if pairs else 0,
                "minimum_geometric_overlap_ppm": 250_000 if pairs else 0,
                "selected_status": 0,
                "selected_candidate_id": {"high": index + 1, "low": index + 101},
                "selected_candidate_payload_checksum": 1000 + index,
                "selected_candidate_metrics": _metrics(costs[index]),
                "pool_best_intrinsic_cost": costs[index],
            }
        )
    result: dict[str, object] = {
        "schema_version": 1,
        "associated_semantic_checksum": semantics["semantic_checksum"],
        "per_net": reports,
        "telemetry_checksum": 0,
    }
    result["telemetry_checksum"] = validator.compute_telemetry_checksum(result)
    return result


def _report(raw: dict[str, object]) -> dict[str, object]:
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    row, _ = roster_validator.validated_successful_case_roster(100)
    result: dict[str, object] = {
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "raw_wire_schema_version": raw["wire_schema_version"],
        "config": copy.deepcopy(raw["config"]),
        "corpus_checksum": raw["corpus_checksum"],
        "raw_cell_plan_checksum": raw["cell_plan_checksum"],
        "raw_cell_artifact_checksum": raw["artifact_checksum"],
        "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        "raw_reference": {
            "repetition_index": 0,
            "execution_order": 0,
            "pair_attempt_checksum": attempt["attempt_checksum"],
            "paired_semantic_checksum": paired["semantic_checksum"],
            "paired_artifact_checksum": paired["artifact_checksum"],
            "baseline_semantic_checksum": baseline["semantics"]["semantic_checksum"],
            "baseline_arm_artifact_checksum": baseline["artifact_checksum"],
            "candidate_semantic_checksum": candidate["semantics"]["semantic_checksum"],
            "candidate_arm_artifact_checksum": candidate["artifact_checksum"],
        },
        "decision_eligible": False,
        "workload_net_roster_checksum": row["roster_checksum"],
        "arms": [
            {
                "arm": 0,
                "raw_semantic_checksum": baseline["semantics"]["semantic_checksum"],
                "diagnostic": {
                    "semantics": copy.deepcopy(baseline["semantics"]),
                    "telemetry": _telemetry(baseline["semantics"]),
                },
            },
            {
                "arm": 1,
                "raw_semantic_checksum": candidate["semantics"]["semantic_checksum"],
                "diagnostic": {
                    "semantics": copy.deepcopy(candidate["semantics"]),
                    "telemetry": _telemetry(candidate["semantics"]),
                },
            },
        ],
        "artifact_checksum": 0,
    }
    _refresh_report(result)
    return result


def _refresh_report(report: dict[str, object]) -> None:
    report["artifact_checksum"] = validator.compute_report_artifact_checksum(report)
    report["source_envelope_checksum"] = validator.compute_report_source_envelope_checksum(report)


def _refresh_telemetry(report: dict[str, object], arm: int) -> None:
    telemetry = report["arms"][arm]["diagnostic"]["telemetry"]
    telemetry["telemetry_checksum"] = validator.compute_telemetry_checksum(telemetry)
    _refresh_report(report)


class Phase4PerNetReportValidatorTest(unittest.TestCase):
    def setUp(self) -> None:
        self.raw = raw_test._artifact(20)
        self.report = _report(self.raw)

    def test_accepts_complete_join_and_canonical_files(self) -> None:
        validator.validate_join(self.raw, self.report, expected_commit=_COMMIT)
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            raw_path = root / "raw.json"
            report_path = root / "report.json"
            raw_path.write_text(json.dumps(self.raw, separators=(",", ":")) + "\n")
            report_path.write_text(json.dumps(self.report, separators=(",", ":")) + "\n")
            self.assertEqual(
                validator.main(
                    [
                        "--expected-commit",
                        _COMMIT,
                        "--raw",
                        str(raw_path),
                        "--report",
                        str(report_path),
                    ]
                ),
                0,
            )

    def test_rejects_every_raw_reference_and_attempt_one_substitution(self) -> None:
        for field in validator._RAW_REFERENCE_FIELDS[2:]:
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                report["raw_reference"][field] ^= 1
                _refresh_report(report)
                with self.assertRaisesRegex(validator.EvidenceError, "raw reference"):
                    validator.validate_join(self.raw, report, expected_commit=_COMMIT)

        report = copy.deepcopy(self.report)
        later = self.raw["attempts"][1]
        report["raw_reference"].update(
            {
                "pair_attempt_checksum": later["attempt_checksum"],
                "paired_semantic_checksum": later["result"]["semantic_checksum"],
                "paired_artifact_checksum": later["result"]["artifact_checksum"],
                "baseline_semantic_checksum": later["baseline"]["record"]["semantics"][
                    "semantic_checksum"
                ],
                "baseline_arm_artifact_checksum": later["baseline"]["record"]["artifact_checksum"],
                "candidate_semantic_checksum": later["candidate"]["record"]["semantics"][
                    "semantic_checksum"
                ],
                "candidate_arm_artifact_checksum": later["candidate"]["record"][
                    "artifact_checksum"
                ],
            }
        )
        _refresh_report(report)
        with self.assertRaisesRegex(validator.EvidenceError, "raw reference"):
            validator.validate_join(self.raw, report, expected_commit=_COMMIT)

    def test_rejects_root_config_source_and_nondecision_drift(self) -> None:
        mutations = (
            ("corpus_checksum", lambda report: report.__setitem__("corpus_checksum", 1)),
            (
                "raw_cell_plan_checksum",
                lambda report: report.__setitem__("raw_cell_plan_checksum", 1),
            ),
            (
                "raw_cell_artifact_checksum",
                lambda report: report.__setitem__("raw_cell_artifact_checksum", 1),
            ),
            (
                "raw_source_envelope_checksum",
                lambda report: report.__setitem__("raw_source_envelope_checksum", 1),
            ),
            (
                "workers",
                lambda report: report["config"].__setitem__("preparation_worker_count", 3),
            ),
            (
                "setup",
                lambda report: report["config"].__setitem__(
                    "maximum_setup_elapsed_nanoseconds", 4_000
                ),
            ),
            (
                "budget",
                lambda report: report["config"]["external_budget"].__setitem__(
                    "maximum_peak_host_bytes", 2 << 30
                ),
            ),
            (
                "limits",
                lambda report: report["config"]["corpus_limits"].__setitem__(
                    "maximum_board_entities", 2_000
                ),
            ),
            ("decision", lambda report: report.__setitem__("decision_eligible", True)),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                report = copy.deepcopy(self.report)
                mutate(report)
                _refresh_report(report)
                with self.assertRaises(validator.EvidenceError):
                    validator.validate_join(self.raw, report, expected_commit=_COMMIT)

        report = copy.deepcopy(self.report)
        report["source_commit"] = "b" * 40
        _refresh_report(report)
        with self.assertRaisesRegex(validator.EvidenceError, "same clean"):
            validator.validate_join(self.raw, report, expected_commit=_COMMIT)

    def test_rejects_operational_semantic_drift_even_when_semantic_hash_is_unchanged(self) -> None:
        for field, value in (("execution_order", 1), ("preparation_worker_count", 5)):
            with self.subTest(field=field):
                report = copy.deepcopy(self.report)
                semantics = report["arms"][0]["diagnostic"]["semantics"]
                old_checksum = semantics["semantic_checksum"]
                semantics[field] = value
                self.assertEqual(
                    validator.raw_validator.compute_semantic_checksum(semantics), old_checksum
                )
                _refresh_report(report)
                with self.assertRaisesRegex(validator.EvidenceError, "complete semantics"):
                    validator.validate_join(self.raw, report, expected_commit=_COMMIT)

    def test_rejects_roster_bucket_pair_optional_and_enum_drift_after_rehash(self) -> None:
        mutations = (
            lambda per_net: per_net[0]["net"].__setitem__("generation", 1),
            lambda per_net: per_net[0]["columns"].__setitem__("other_rejections", 2),
            lambda per_net: per_net[0].__setitem__("candidate_pair_count", 1),
            lambda per_net: per_net[0]["selected_candidate_id"].update({"high": 0, "low": 0}),
            lambda per_net: per_net[0].__setitem__("selected_status", 9),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                report = copy.deepcopy(self.report)
                mutate(report["arms"][0]["diagnostic"]["telemetry"]["per_net"])
                _refresh_telemetry(report, 0)
                with self.assertRaises(validator.EvidenceError):
                    validator.validate_join(self.raw, report, expected_commit=_COMMIT)

    def test_rejects_duplicate_extra_reordered_nonfinite_and_oversized_report_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            malformed = {
                "duplicate": '{"a":1,"a":1}\n',
                "extra": json.dumps({**self.report, "extra": 1}, separators=(",", ":")) + "\n",
                "reordered": json.dumps(
                    {key: self.report[key] for key in reversed(self.report)}, separators=(",", ":")
                )
                + "\n",
                "nonfinite": '{"value":NaN}\n',
                "nested": "[" * 2_000 + "0" + "]" * 2_000,
            }
            for label, text in malformed.items():
                with self.subTest(label=label):
                    path = root / f"{label}.json"
                    path.write_text(text)
                    if label in {"extra", "reordered"}:
                        document = validator._read_canonical(
                            path, "report", validator._MAX_REPORT_BYTES
                        )
                        with self.assertRaises(validator.EvidenceError):
                            validator._validate_join_documents(
                                self.raw, document, expected_commit=_COMMIT
                            )
                    else:
                        with self.assertRaises(validator.EvidenceError):
                            validator._read_canonical(path, "report", validator._MAX_REPORT_BYTES)
            oversized = root / "oversized.json"
            oversized.write_bytes(b" " * (validator._MAX_REPORT_BYTES + 1))
            with self.assertRaisesRegex(validator.EvidenceError, "input bound"):
                validator._read_canonical(oversized, "report", validator._MAX_REPORT_BYTES)


if __name__ == "__main__":
    unittest.main()
