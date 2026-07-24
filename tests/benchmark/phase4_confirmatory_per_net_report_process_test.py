"""Cross-language tests for the frozen confirmatory per-net report slice."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def normalize_clean_source(document: dict[str, object]) -> None:
    document["source_commit"] = "a" * 40
    document["source_stamped"] = True
    document["source_tree_dirty"] = False
    document["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(document)


def report_command(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(runfile("phase4_confirmatory_per_net_report_test_runner")),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
        "--raw_wire_schema_version=1",
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


class Phase4ConfirmatoryPerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        raw_result = subprocess.run(
            [
                str(runfile("phase4_confirmatory_evidence_test_runner")),
                "--corpus_version=2",
                "--testing_allow_unstamped=1",
                "--case_id=10200",
                "--pool_size=4",
                "--workers=4",
                "--repetitions=20",
                "--setup_ns=300000000000",
                "--prepared_ns=300000000000",
                "--cold_ns=300000000000",
                "--address_space_bytes=68719476736",
                "--peak_host_bytes=17179869184",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=360,
        )
        if raw_result.returncode != 0:
            raise RuntimeError(raw_result.stderr)
        cls.raw = json.loads(raw_result.stdout)
        normalize_clean_source(cls.raw)
        cls.raw_path = root / "raw.json"
        cls.raw_path.write_text(
            json.dumps(cls.raw, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

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
        cls.report_path = root / "report.json"
        cls.report_path.write_text(first.stdout, encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_real_report_is_deterministic_and_strictly_joins(self) -> None:
        self.assertEqual(self.first_report_bytes, self.second_report_bytes)
        report_validator.validate_confirmatory_join(
            self.raw,
            self.report,
            expected_commit=self.raw["source_commit"],
        )
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 1)
        self.assertEqual(self.report["corpus_checksum"], 4182833841936446798)
        for arm in self.report["arms"]:
            self.assertEqual(len(arm["diagnostic"]["telemetry"]["per_net"]), 64)
        for forbidden in (
            "case_build_elapsed_nanoseconds",
            "prepared_elapsed_nanoseconds",
            "cold_elapsed_nanoseconds",
            "outer_elapsed_nanoseconds",
            "process_lifetime_peak_host_bytes",
        ):
            self.assertNotIn(f'"{forbidden}"', self.first_report_bytes)

    def test_legacy_and_confirmatory_authorities_reject_each_other(self) -> None:
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            report_validator.validate_join(
                self.raw,
                self.report,
                expected_commit=self.raw["source_commit"],
            )

        legacy = subprocess.run(
            [
                str(runfile("phase4_evidence_runner")),
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
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=120,
        )
        self.assertEqual(legacy.returncode, 0, legacy.stderr)
        legacy_raw = json.loads(legacy.stdout)
        normalize_clean_source(legacy_raw)
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            report_validator.validate_confirmatory_join(
                legacy_raw,
                self.report,
                expected_commit=legacy_raw["source_commit"],
            )

    def test_runner_rejects_non_slice_authority_before_stdout(self) -> None:
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
            replace("raw_wire_schema_version", "2"),
            replace("pool_size", "8"),
            replace("case_id", "10201"),
            replace("case_id", "10100"),
            replace("case_id", "11000"),
            replace("case_id", "12002"),
            replace("case_id", "13000"),
            replace("case_id", "14000"),
            valid + [valid[-1]],
            valid + ["--unknown=1"],
            valid + ["--" + "x" * 130 + "=1"],
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

    def test_production_runner_has_no_unstamped_escape(self) -> None:
        command = report_command(self.raw)
        command[0] = str(runfile("phase4_confirmatory_per_net_report_runner"))
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")

    def test_publication_cli_rejects_fifo_and_validates_raw_first(self) -> None:
        fifo = pathlib.Path(self.temporary.name) / "report.fifo"
        os.mkfifo(fifo)
        base = [
            str(runfile("phase4_confirmatory_per_net_report_validator")),
            f"--expected-commit={self.raw['source_commit']}",
            f"--raw={self.raw_path}",
            f"--report={fifo}",
        ]
        fifo_result = subprocess.run(
            base,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(fifo_result.returncode, 1)
        self.assertIn("regular file", fifo_result.stderr)

        invalid_raw = json.loads(json.dumps(self.raw))
        invalid_raw["corpus_checksum"] = 0
        invalid_raw_path = pathlib.Path(self.temporary.name) / "invalid-raw.json"
        invalid_raw_path.write_text(
            json.dumps(invalid_raw, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        invalid_result = subprocess.run(
            [
                base[0],
                base[1],
                f"--raw={invalid_raw_path}",
                base[3],
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(invalid_result.returncode, 1)
        self.assertIn("corpus_checksum", invalid_result.stderr)
        self.assertNotIn("regular file", invalid_result.stderr)


if __name__ == "__main__":
    unittest.main()
