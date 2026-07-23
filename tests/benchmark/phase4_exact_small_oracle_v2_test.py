"""Cross-language closure tests for the Phase 4 exact-small Raw-v2 oracle."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_exact_small_oracle as oracle_v1
from tools import validate_phase4_exact_small_oracle_v2 as oracle_v2
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _common_arguments(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        "--testing_allow_unstamped=1",
        f"--apgar_commit={raw['source_commit']}",
        f"--case_id={config['case_id']}",
        f"--pool_size={config['requested_pool_size']}",
        f"--workers={config['preparation_worker_count']}",
        f"--repetitions={config['repetitions']}",
        f"--setup_ns={config['maximum_setup_elapsed_nanoseconds']}",
        f"--prepared_ns={config['external_budget']['maximum_prepared_elapsed_nanoseconds']}",
        f"--cold_ns={config['external_budget']['maximum_cold_elapsed_nanoseconds']}",
        f"--address_space_bytes={config['external_budget']['maximum_address_space_bytes']}",
        f"--peak_host_bytes={config['external_budget']['maximum_peak_host_bytes']}",
        f"--maximum_nets={config['corpus_limits']['maximum_nets']}",
        f"--maximum_compiled_nodes={config['corpus_limits']['maximum_compiled_nodes']}",
        f"--maximum_compiled_host_bytes={config['corpus_limits']['maximum_compiled_host_bytes']}",
        f"--maximum_active_regions={config['corpus_limits']['maximum_active_regions']}",
        f"--maximum_board_entities={config['corpus_limits']['maximum_board_entities']}",
        f"--raw_cell_plan_checksum={raw['cell_plan_checksum']}",
        f"--raw_cell_artifact_checksum={raw['artifact_checksum']}",
        f"--raw_source_envelope_checksum={raw['source_envelope_checksum']}",
        f"--pair_attempt_checksum={attempt['attempt_checksum']}",
        f"--paired_semantic_checksum={paired['semantic_checksum']}",
        f"--paired_artifact_checksum={paired['artifact_checksum']}",
        f"--baseline_semantic_checksum={baseline['semantics']['semantic_checksum']}",
        f"--baseline_arm_artifact_checksum={baseline['artifact_checksum']}",
        f"--candidate_semantic_checksum={candidate['semantics']['semantic_checksum']}",
        f"--candidate_arm_artifact_checksum={candidate['artifact_checksum']}",
    ]


def _prepare(
    root: pathlib.Path, case_id: int
) -> tuple[dict[str, object], dict[str, object], dict[str, object], dict[str, object]]:
    raw_path = root / f"raw-{case_id}.json"
    sidecar_path = root / f"same-run-{case_id}.json"
    completed = subprocess.run(
        [
            str(_runfile("phase4_evidence_runner")),
            "--testing_allow_unstamped=1",
            f"--case_id={case_id}",
            "--pool_size=4",
            "--workers=4",
            "--repetitions=20",
            "--setup_ns=300000000000",
            "--prepared_ns=300000000000",
            "--cold_ns=300000000000",
            "--address_space_bytes=68719476736",
            "--peak_host_bytes=17179869184",
            f"--same_run_telemetry_output={sidecar_path}",
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    raw = json.loads(completed.stdout)
    sidecar = same_run_validator.read_document(sidecar_path)
    raw["source_commit"] = _COMMIT
    raw["source_stamped"] = True
    raw["source_tree_dirty"] = False
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)
    sidecar["source_commit"] = _COMMIT
    sidecar["source_stamped"] = True
    sidecar["source_tree_dirty"] = False
    sidecar["raw_source_envelope_checksum"] = raw["source_envelope_checksum"]
    sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
        sidecar
    )
    raw_path.write_text(_canonical(raw), encoding="utf-8")
    sidecar_path.write_text(_canonical(sidecar), encoding="utf-8")

    common = _common_arguments(raw)
    report_completed = subprocess.run(
        [
            str(_runfile("phase4_per_net_report_test_runner")),
            "--raw_wire_schema_version=2",
            *common,
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if report_completed.returncode != 0:
        raise AssertionError(report_completed.stderr)
    report = json.loads(report_completed.stdout)
    snapshot_completed = subprocess.run(
        [
            str(_runfile("phase4_exact_small_snapshot_test_runner")),
            "--raw_evidence_schema_version=2",
            *common,
            f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
            f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if snapshot_completed.returncode != 0:
        raise AssertionError(snapshot_completed.stderr)
    return raw, sidecar, report, json.loads(snapshot_completed.stdout)


class Phase4ExactSmallOracleV2Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.documents = {case_id: _prepare(cls.root, case_id) for case_id in (100, 101, 102)}

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_all_exact_cells_publish_sidecar_bound_oracle_v2(self) -> None:
        for case_id, (raw, sidecar, report, snapshot) in self.documents.items():
            with self.subTest(case_id=case_id):
                artifact = oracle_v2.validate_publication(
                    raw,
                    sidecar,
                    report,
                    snapshot,
                    expected_commit=_COMMIT,
                )
                self.assertEqual(artifact["schema_version"], 2)
                self.assertEqual(artifact["case_id"], case_id)
                self.assertEqual(artifact["raw_evidence_schema_version"], 2)
                self.assertEqual(artifact["raw_wire_schema_version"], 2)
                self.assertEqual(
                    artifact["same_run_telemetry_artifact_checksum"],
                    sidecar["artifact_checksum"],
                )
                self.assertTrue(artifact["production_is_optimal"])
                self.assertFalse(artifact["decision_eligible"])
                self.assertEqual(
                    oracle_v2.serialize_oracle_artifact(artifact),
                    _canonical(artifact),
                )
                with self.assertRaises(raw_validator.EvidenceError):
                    oracle_v1.validate_publication(
                        raw,
                        report,
                        snapshot,
                        expected_commit=_COMMIT,
                    )
        self.assertFalse(
            same_run_validator.exact_rejection_guardrail_passes(self.documents[100][1])
        )

    def test_foreign_sidecar_and_raw_v1_snapshot_envelope_cannot_masquerade(self) -> None:
        raw, sidecar, report, original = self.documents[100]
        foreign = copy.deepcopy(sidecar)
        foreign["raw_cell_artifact_checksum"] ^= 1
        foreign["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            foreign
        )
        with self.assertRaises(raw_validator.EvidenceError):
            oracle_v2.validate_publication(
                raw,
                foreign,
                report,
                original,
                expected_commit=_COMMIT,
            )

        snapshot = copy.deepcopy(original)
        raw_v1_envelope = {
            key: value for key, value in raw.items() if key not in {"raw_evidence_schema_version"}
        }
        raw_v1_envelope["wire_schema_version"] = 1
        snapshot["raw_source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            raw_v1_envelope
        )
        snapshot["artifact_checksum"] = oracle_v1.compute_snapshot_artifact_checksum(snapshot)
        snapshot["source_envelope_checksum"] = oracle_v1.compute_snapshot_source_envelope(snapshot)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "does not join the Raw cell"):
            oracle_v2.validate_publication(
                raw,
                sidecar,
                report,
                snapshot,
                expected_commit=_COMMIT,
            )

    def test_fully_valid_cross_case_authorities_cannot_be_swapped(self) -> None:
        raw, sidecar, report, snapshot = self.documents[100]
        _, foreign_sidecar, foreign_report, foreign_snapshot = self.documents[101]
        for label, inputs in (
            ("sidecar", (raw, foreign_sidecar, report, snapshot)),
            ("report", (raw, sidecar, foreign_report, snapshot)),
            ("snapshot", (raw, sidecar, report, foreign_snapshot)),
        ):
            with self.subTest(label=label):
                with self.assertRaises(raw_validator.EvidenceError):
                    oracle_v2.validate_publication(
                        *inputs,
                        expected_commit=_COMMIT,
                    )

    def test_cli_requires_the_same_run_companion(self) -> None:
        raw, sidecar, report, snapshot = self.documents[100]
        paths = {}
        for name, document in (
            ("raw", raw),
            ("sidecar", sidecar),
            ("report", report),
            ("snapshot", snapshot),
        ):
            path = self.root / f"cli-{name}.json"
            path.write_text(_canonical(document), encoding="utf-8")
            paths[name] = path
        completed = subprocess.run(
            [
                str(_runfile("phase4_exact_small_oracle_v2_validator")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(paths["raw"]),
                "--same-run-telemetry",
                str(paths["sidecar"]),
                "--report",
                str(paths["report"]),
                "--snapshot",
                str(paths["snapshot"]),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(json.loads(completed.stdout)["case_id"], 100)

    def test_cli_rejects_foreign_sidecar_before_reading_later_inputs(self) -> None:
        raw, sidecar, _, _ = self.documents[100]
        _, _, foreign_report, _ = self.documents[101]
        foreign = copy.deepcopy(sidecar)
        foreign["raw_cell_artifact_checksum"] ^= 1
        foreign["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            foreign
        )
        raw_path = self.root / "ordering-raw.json"
        sidecar_path = self.root / "ordering-sidecar.json"
        valid_sidecar_path = self.root / "ordering-valid-sidecar.json"
        foreign_report_path = self.root / "ordering-foreign-report.json"
        raw_path.write_text(_canonical(raw), encoding="utf-8")
        sidecar_path.write_text(_canonical(foreign), encoding="utf-8")
        valid_sidecar_path.write_text(_canonical(sidecar), encoding="utf-8")
        foreign_report_path.write_text(_canonical(foreign_report), encoding="utf-8")
        completed = subprocess.run(
            [
                str(_runfile("phase4_exact_small_oracle_v2_validator")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(raw_path),
                "--same-run-telemetry",
                str(sidecar_path),
                "--report",
                str(self.root / "must-not-be-read-report.json"),
                "--snapshot",
                str(self.root / "must-not-be-read-snapshot.json"),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn("differs from Raw", completed.stderr)
        self.assertNotIn("cannot read", completed.stderr)

        oversized = self.root / "ordering-oversized-sidecar.json"
        with oversized.open("wb") as stream:
            stream.seek(same_run_validator._MAX_BYTES)
            stream.write(b"\n")
        completed = subprocess.run(
            [
                str(_runfile("phase4_exact_small_oracle_v2_validator")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(raw_path),
                "--same-run-telemetry",
                str(oversized),
                "--report",
                str(self.root / "must-still-not-be-read-report.json"),
                "--snapshot",
                str(self.root / "must-still-not-be-read-snapshot.json"),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn(f"exceeds {same_run_validator._MAX_BYTES} bytes", completed.stderr)
        self.assertNotIn("must-still-not-be-read", completed.stderr)

        completed = subprocess.run(
            [
                str(_runfile("phase4_exact_small_oracle_v2_validator")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(raw_path),
                "--same-run-telemetry",
                str(valid_sidecar_path),
                "--report",
                str(foreign_report_path),
                "--snapshot",
                str(self.root / "must-not-be-read-after-report.json"),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn("report config does not exactly match", completed.stderr)
        self.assertNotIn("must-not-be-read-after-report", completed.stderr)

    def test_output_validator_rejects_rechecksummed_structural_aliases(self) -> None:
        raw, sidecar, report, snapshot = self.documents[100]
        artifact = oracle_v2.validate_publication(
            raw,
            sidecar,
            report,
            snapshot,
            expected_commit=_COMMIT,
        )
        for field, replacement, pattern in (
            ("raw_wire_schema_version", True, "raw_wire_schema_version"),
            ("production_is_optimal", False, "exact optimum"),
            ("cartesian_product", 4097, "out of bounds"),
        ):
            with self.subTest(field=field):
                mutated = copy.deepcopy(artifact)
                mutated[field] = replacement
                mutated["artifact_checksum"] = oracle_v2.compute_artifact_checksum(mutated)
                mutated["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
                    mutated
                )
                with self.assertRaisesRegex(raw_validator.EvidenceError, pattern):
                    oracle_v2.serialize_oracle_artifact(mutated)

        for field in (
            "raw_source_envelope_checksum",
            "same_run_telemetry_source_envelope_checksum",
            "per_net_report_source_envelope_checksum",
            "snapshot_source_envelope_checksum",
        ):
            with self.subTest(field=field):
                mutated = copy.deepcopy(artifact)
                mutated[field] = 1
                mutated["artifact_checksum"] = oracle_v2.compute_artifact_checksum(mutated)
                mutated["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
                    mutated
                )
                with self.assertRaisesRegex(raw_validator.EvidenceError, "not derivable"):
                    oracle_v2.serialize_oracle_artifact(mutated)

        impossible = copy.deepcopy(artifact)
        impossible["optimum_count"] = impossible["cartesian_product"] + 1
        impossible["artifact_checksum"] = oracle_v2.compute_artifact_checksum(impossible)
        impossible["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
            impossible
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "within the product"):
            oracle_v2.serialize_oracle_artifact(impossible)

        impossible = copy.deepcopy(artifact)
        impossible["production_objective"]["total_overuse_units"] = 1
        impossible["optimum_objective"]["total_overuse_units"] = 1
        impossible["production_overused_resource_count"] = 0
        impossible["canonical_witness_overused_resource_count"] = 0
        impossible["artifact_checksum"] = oracle_v2.compute_artifact_checksum(impossible)
        impossible["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
            impossible
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "overuse units"):
            oracle_v2.serialize_oracle_artifact(impossible)

        impossible = copy.deepcopy(artifact)
        impossible["production_objective"]["total_overuse_units"] = 1
        impossible["optimum_objective"]["total_overuse_units"] = 1
        impossible["production_overused_resource_count"] = 2
        impossible["canonical_witness_overused_resource_count"] = 2
        impossible["artifact_checksum"] = oracle_v2.compute_artifact_checksum(impossible)
        impossible["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
            impossible
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "overuse units"):
            oracle_v2.serialize_oracle_artifact(impossible)

        impossible = copy.deepcopy(artifact)
        for index, row in enumerate(impossible["canonical_witness"], start=1):
            row["net"] = {"id": index, "generation": 1}
        impossible["artifact_checksum"] = oracle_v2.compute_artifact_checksum(impossible)
        impossible["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
            impossible
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "frozen case roster"):
            oracle_v2.serialize_oracle_artifact(impossible)

        impossible = copy.deepcopy(artifact)
        impossible["production_objective"] = {
            "selected_net_count": 0,
            "total_overuse_units": 1,
            "total_intrinsic_base_cost": 1,
        }
        impossible["optimum_objective"] = copy.deepcopy(impossible["production_objective"])
        impossible["production_overused_resource_count"] = 1
        impossible["canonical_witness_overused_resource_count"] = 1
        for row in impossible["canonical_witness"]:
            row["candidate_id"] = None
        impossible["artifact_checksum"] = oracle_v2.compute_artifact_checksum(impossible)
        impossible["source_envelope_checksum"] = oracle_v2.compute_source_envelope_checksum(
            impossible
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "without selections"):
            oracle_v2.serialize_oracle_artifact(impossible)


if __name__ == "__main__":
    unittest.main()
