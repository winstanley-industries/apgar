"""Cross-language tests for the frozen confirmatory same-run per-net report slice."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_confirmatory_same_run_per_net_report as publication_validator
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_per_net_report_v2 as legacy_same_run_report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def normalize_clean_source(
    raw: dict[str, object],
    sidecar: dict[str, object],
) -> None:
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


def report_command(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(runfile("phase4_confirmatory_same_run_per_net_report_test_runner")),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
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


class Phase4ConfirmatorySameRunPerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        raw_result = subprocess.run(
            [
                str(runfile("phase4_confirmatory_evidence_test_runner")),
                "--corpus_version=2",
                "--testing_allow_unstamped=1",
                "--case_id=10100",
                "--pool_size=4",
                "--workers=4",
                "--repetitions=20",
                "--setup_ns=300000000000",
                "--prepared_ns=300000000000",
                "--cold_ns=300000000000",
                "--address_space_bytes=68719476736",
                "--peak_host_bytes=17179869184",
                f"--same_run_telemetry_output={cls.sidecar_path}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=180,
        )
        if raw_result.returncode != 0:
            raise RuntimeError(raw_result.stderr)
        cls.raw = json.loads(raw_result.stdout)
        cls.sidecar = same_run_validator.read_document(cls.sidecar_path)
        normalize_clean_source(cls.raw, cls.sidecar)
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")

        first = subprocess.run(
            report_command(cls.raw),
            check=False,
            text=True,
            capture_output=True,
            timeout=180,
        )
        if first.returncode != 0:
            raise RuntimeError(first.stderr)
        second = subprocess.run(
            report_command(cls.raw),
            check=False,
            text=True,
            capture_output=True,
            timeout=180,
        )
        if second.returncode != 0:
            raise RuntimeError(second.stderr)
        cls.first_report_bytes = first.stdout
        cls.second_report_bytes = second.stdout
        cls.report = json.loads(first.stdout)
        cls.report_path = cls.root / "report.json"
        cls.report_path.write_text(first.stdout, encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_real_report_is_deterministic_and_strictly_three_way_joins(self) -> None:
        self.assertEqual(self.first_report_bytes, self.second_report_bytes)
        publication_validator.validate_join(
            self.raw,
            self.sidecar,
            self.report,
            expected_commit=_COMMIT,
        )
        self.assertTrue(same_run_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 2)
        self.assertEqual(self.report["corpus_checksum"], 4182833841936446798)
        self.assertEqual(self.report["workload_net_roster_checksum"], 12521697377381992336)
        for arm in self.report["arms"]:
            self.assertEqual(len(arm["diagnostic"]["telemetry"]["per_net"]), 6)
        for forbidden in (
            "case_build_elapsed_nanoseconds",
            "prepared_elapsed_nanoseconds",
            "cold_elapsed_nanoseconds",
            "outer_elapsed_nanoseconds",
            "process_lifetime_peak_host_bytes",
        ):
            self.assertNotIn(f'"{forbidden}"', self.first_report_bytes)

    def test_cross_authority_and_reauthenticated_substitutions_are_rejected(self) -> None:
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "raw cell fields differ",
        ):
            report_validator.validate_join(
                self.raw,
                self.report,
                expected_commit=_COMMIT,
            )
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            legacy_same_run_report_validator.validate_join(
                self.raw,
                self.sidecar,
                self.report,
                expected_commit=_COMMIT,
            )
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "raw cell fields differ|ordinary Raw authority",
        ):
            report_validator.validate_confirmatory_join(
                self.raw,
                self.report,
                expected_commit=_COMMIT,
            )

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            same_run_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            publication_validator.validate_join(
                self.raw,
                foreign_sidecar,
                self.report,
                expected_commit=_COMMIT,
            )

        foreign_report = copy.deepcopy(self.report)
        foreign_report["raw_cell_artifact_checksum"] ^= 1
        foreign_report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_report
        )
        foreign_report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_report)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "does not join the raw cell"):
            publication_validator.validate_join(
                self.raw,
                self.sidecar,
                foreign_report,
                expected_commit=_COMMIT,
            )

    def test_both_confirmatory_runners_cross_reject_before_stdout(self) -> None:
        valid = report_command(self.raw)

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in valid
            ]

        mutations = (
            [argument for argument in valid if not argument.startswith("--corpus_version=")],
            replace("corpus_version", "1"),
            replace("raw_wire_schema_version", "1"),
            replace("pool_size", "8"),
            replace("case_id", "10101"),
            replace("case_id", "10102"),
            replace("case_id", "10200"),
            replace("case_id", "11000"),
            replace("case_id", "12002"),
            replace("case_id", "13000"),
            replace("case_id", "14000"),
            [
                "--case_id=10200"
                if argument.startswith("--case_id=")
                else "--raw_wire_schema_version=1"
                if argument.startswith("--raw_wire_schema_version=")
                else argument
                for argument in valid
            ],
        )
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                completed = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")

        ordinary = list(valid)
        ordinary[0] = str(runfile("phase4_confirmatory_per_net_report_test_runner"))
        completed = subprocess.run(
            ordinary,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")

    def test_production_runner_has_no_unstamped_escape(self) -> None:
        command = report_command(self.raw)
        command[0] = str(runfile("phase4_confirmatory_same_run_per_net_report_runner"))
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")

    def test_publication_cli_obeys_raw_sidecar_report_open_order(self) -> None:
        validator = str(runfile("phase4_confirmatory_same_run_per_net_report_validator"))
        valid_command = [
            validator,
            f"--expected-commit={_COMMIT}",
            f"--raw={self.raw_path}",
            f"--same-run-telemetry={self.sidecar_path}",
            f"--report={self.report_path}",
        ]
        valid = subprocess.run(
            valid_command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(valid.returncode, 0, valid.stderr)

        sidecar_fifo = self.root / "sidecar.fifo"
        report_fifo = self.root / "report.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)

        invalid_raw = copy.deepcopy(self.raw)
        invalid_raw["corpus_checksum"] = 0
        invalid_raw_path = self.root / "invalid-raw.json"
        invalid_raw_path.write_text(canonical(invalid_raw), encoding="utf-8")
        invalid_raw_result = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={invalid_raw_path}",
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(invalid_raw_result.returncode, 1)
        self.assertIn("corpus_checksum", invalid_raw_result.stderr)
        self.assertNotIn("regular file", invalid_raw_result.stderr)

        sidecar_fifo_result = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={self.raw_path}",
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(sidecar_fifo_result.returncode, 1)
        self.assertIn("regular file", sidecar_fifo_result.stderr)

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            same_run_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        foreign_sidecar_path = self.root / "foreign-sidecar.json"
        foreign_sidecar_path.write_text(canonical(foreign_sidecar), encoding="utf-8")
        foreign_result = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={self.raw_path}",
                f"--same-run-telemetry={foreign_sidecar_path}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(foreign_result.returncode, 1)
        self.assertIn("raw_cell_artifact_checksum differs from Raw", foreign_result.stderr)
        self.assertNotIn("per-net report must be a regular file", foreign_result.stderr)

        report_fifo_result = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={self.raw_path}",
                f"--same-run-telemetry={self.sidecar_path}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(report_fifo_result.returncode, 1)
        self.assertIn("per-net report", report_fifo_result.stderr)
        self.assertIn("regular file", report_fifo_result.stderr)


if __name__ == "__main__":
    unittest.main()
