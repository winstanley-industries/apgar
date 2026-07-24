"""Cross-language tests for the Phase 4 Raw-v2 downstream joins."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.support.phase4_current_diagnostic_budget import patch_live_diagnostic_budgets
from tools import project_phase4_operational_evidence_v2 as operational_v2
from tools import validate_phase4_per_net_report as report_v1
from tools import validate_phase4_per_net_report_v2 as report_v2
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _normalize_source(raw: dict[str, object], sidecar: dict[str, object]) -> None:
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


def _report_command(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(_runfile("phase4_per_net_report_test_runner")),
        "--testing_allow_unstamped=1",
        "--raw_wire_schema_version=2",
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


class Phase4RawV2DownstreamJoinTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.budget_patcher = patch_live_diagnostic_budgets(raw_validator, _runfile)
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        cls.root = root
        cls.raw_path = root / "raw.json"
        cls.sidecar_path = root / "same_run.json"
        command = [
            str(_runfile("phase4_evidence_runner")),
            "--testing_allow_unstamped=1",
            "--case_id=100",
            "--pool_size=4",
            "--workers=4",
            "--repetitions=20",
            "--setup_ns=300000000000",
            "--prepared_ns=300000000000",
            "--cold_ns=300000000000",
            "--address_space_bytes=68719476736",
            "--peak_host_bytes=17179869184",
            f"--same_run_telemetry_output={cls.sidecar_path}",
        ]
        completed = subprocess.run(command, check=False, text=True, capture_output=True, timeout=60)
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr)
        cls.raw = json.loads(completed.stdout)
        cls.sidecar = same_run_validator.read_document(cls.sidecar_path)
        _normalize_source(cls.raw, cls.sidecar)
        cls.raw_path.write_text(_canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(_canonical(cls.sidecar), encoding="utf-8")
        report = subprocess.run(
            _report_command(cls.raw),
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if report.returncode != 0:
            raise RuntimeError(report.stderr)
        cls.report_path = root / "report.json"
        cls.report_path.write_text(report.stdout, encoding="utf-8")
        cls.report = json.loads(report.stdout)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()
        cls.budget_patcher.stop()

    def test_complete_three_way_join_and_projection(self) -> None:
        report_v2.validate_join(
            self.raw,
            self.sidecar,
            self.report,
            expected_commit=_COMMIT,
        )
        projection = operational_v2.project_document(self.raw, self.sidecar)
        operational_v2.validate_projection(self.raw, self.sidecar, projection)
        self.assertEqual(self.report["schema_version"], 1)
        self.assertEqual(self.report["raw_wire_schema_version"], 2)
        self.assertEqual(projection["schema_version"], 2)
        self.assertEqual(projection["raw_binding"]["raw_evidence_schema_version"], 2)
        self.assertFalse(projection["standalone_decision_eligible"])
        self.assertFalse(projection["phase4_telemetry_complete"])
        self.assertFalse(
            projection["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"]
        )
        self.assertEqual(len(projection["repetitions"]), 20)

    def test_versioned_consumers_reject_masquerades_and_foreign_sidecars(self) -> None:
        with self.assertRaises(raw_validator.EvidenceError):
            report_v1.validate_join(self.raw, self.report, expected_commit=_COMMIT)

        wire_v1_report = copy.deepcopy(self.report)
        wire_v1_report["raw_wire_schema_version"] = 1
        wire_v1_report["artifact_checksum"] = report_v1.compute_report_artifact_checksum(
            wire_v1_report
        )
        wire_v1_report["source_envelope_checksum"] = (
            report_v1.compute_report_source_envelope_checksum(wire_v1_report)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "Wire-v2"):
            report_v2.validate_join(
                self.raw,
                self.sidecar,
                wire_v1_report,
                expected_commit=_COMMIT,
            )

        foreign = copy.deepcopy(self.sidecar)
        foreign["raw_cell_artifact_checksum"] ^= 1
        foreign["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            foreign
        )
        with self.assertRaises(raw_validator.EvidenceError):
            report_v2.validate_join(
                self.raw,
                foreign,
                self.report,
                expected_commit=_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            same_run_validator.validate_join(
                self.raw,
                foreign,
                expected_commit=_COMMIT,
            )

    def test_projection_binds_every_same_run_capture_and_cli_is_deterministic(self) -> None:
        projection = operational_v2.project_document(self.raw, self.sidecar)
        changed = copy.deepcopy(projection)
        changed["repetitions"][19]["candidate"]["same_run_arm_capture_checksum"] ^= 1
        changed["artifact_checksum"] = operational_v2.compute_artifact_checksum(changed)
        changed["source_envelope_checksum"] = operational_v2.compute_source_envelope_checksum(
            changed
        )
        with self.assertRaises(operational_v2.ProjectionError):
            operational_v2.validate_projection(self.raw, self.sidecar, changed)

        command = [
            str(_runfile("phase4_current_v1_diagnostic_cli")),
            "--testing-tool=operational-projection-v2",
            "--raw",
            str(self.raw_path),
            "--same-run-telemetry",
            str(self.sidecar_path),
            "--expected-commit",
            _COMMIT,
        ]
        first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=20)
        second = subprocess.run(command, check=False, text=True, capture_output=True, timeout=20)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, second.stdout)
        self.assertEqual(first.stdout, operational_v2.encode_projection(projection))

        joined = subprocess.run(
            [
                str(_runfile("phase4_current_v1_diagnostic_cli")),
                "--testing-tool=per-net-report-v2",
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(self.raw_path),
                "--same-run-telemetry",
                str(self.sidecar_path),
                "--report",
                str(self.report_path),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=20,
        )
        self.assertEqual(joined.returncode, 0, joined.stderr)

    def test_publication_cli_authenticates_raw_authority_before_reading_report(self) -> None:
        foreign = copy.deepcopy(self.sidecar)
        foreign["raw_cell_artifact_checksum"] ^= 1
        foreign["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            foreign
        )
        foreign_path = self.root / "foreign.json"
        foreign_path.write_text(_canonical(foreign), encoding="utf-8")
        malformed_report = self.root / "malformed-report.json"
        malformed_report.write_bytes(b"\xff")
        completed = subprocess.run(
            [
                str(_runfile("phase4_current_v1_diagnostic_cli")),
                "--testing-tool=per-net-report-v2",
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(self.raw_path),
                "--same-run-telemetry",
                str(foreign_path),
                "--report",
                str(malformed_report),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=20,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("raw_cell_artifact_checksum differs from Raw", completed.stderr)
        self.assertNotIn("cannot read per-net report", completed.stderr)

        non_object_report = self.root / "non-object-report.json"
        non_object_report.write_text("[]\n", encoding="utf-8")
        completed = subprocess.run(
            [
                str(_runfile("phase4_current_v1_diagnostic_cli")),
                "--testing-tool=per-net-report-v2",
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(self.raw_path),
                "--same-run-telemetry",
                str(self.sidecar_path),
                "--report",
                str(non_object_report),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=20,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("per-net report input must be a JSON object", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)

        with mock.patch.object(
            same_run_validator.protocol_validator,
            "read_protocol",
            side_effect=same_run_validator.protocol_validator.ProtocolV2Error("broken"),
        ):
            with self.assertRaisesRegex(
                raw_validator.EvidenceError, "cannot authenticate the frozen decision protocol"
            ):
                same_run_validator.validate_join(
                    self.raw,
                    self.sidecar,
                    expected_commit=_COMMIT,
                )


if __name__ == "__main__":
    unittest.main()
