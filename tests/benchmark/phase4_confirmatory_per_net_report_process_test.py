"""Acquisition-free process tests for the closed H2250 ordinary report runner."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import tempfile
import unittest

_COMMIT = "a" * 40
_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def report_command(executable: str, *, testing: bool) -> list[str]:
    command = [
        str(runfile(executable)),
        "--corpus_version=2",
        "--raw_wire_schema_version=1",
        f"--apgar_commit={_COMMIT}",
        "--case_id=10200",
        "--pool_size=4",
        "--workers=4",
        "--repetitions=20",
        "--setup_ns=300000000000",
        "--prepared_ns=300000000000",
        "--cold_ns=300000000000",
        "--address_space_bytes=68719476736",
        "--peak_host_bytes=17179869184",
        "--maximum_nets=4096",
        "--maximum_compiled_nodes=100000000",
        "--maximum_compiled_host_bytes=8589934592",
        "--maximum_active_regions=250000",
        "--maximum_board_entities=100000",
        "--raw_cell_plan_checksum=1",
        "--raw_cell_artifact_checksum=2",
        "--raw_source_envelope_checksum=3",
        "--pair_attempt_checksum=4",
        "--paired_semantic_checksum=5",
        "--paired_artifact_checksum=6",
        "--baseline_semantic_checksum=7",
        "--baseline_arm_artifact_checksum=8",
        "--candidate_semantic_checksum=9",
        "--candidate_arm_artifact_checksum=10",
    ]
    if testing:
        command.insert(1, "--testing_allow_unstamped=1")
    return command


def replace(command: list[str], name: str, value: str) -> list[str]:
    prefix = f"--{name}="
    return [f"{prefix}{value}" if argument.startswith(prefix) else argument for argument in command]


class Phase4ConfirmatoryPerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.invalid_raw_path = cls.root / "invalid-raw.json"
        cls.invalid_raw_path.write_text(
            json.dumps({"corpus_checksum": 0}, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def assert_session_authority_closure(
        self,
        command: list[str],
        *,
        env: dict[str, str] | None = None,
    ) -> None:
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
            env=env,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn(_SESSION_AUTHORITY, completed.stderr)

    def test_test_and_production_runners_close_at_session_authority(self) -> None:
        test_command = report_command(
            "phase4_confirmatory_per_net_report_test_runner",
            testing=True,
        )
        first = subprocess.run(
            test_command,
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
        )
        second = subprocess.run(
            test_command,
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(first.stderr, second.stderr)
        self.assertEqual(first.returncode, 2)
        self.assertEqual(first.stdout, "")
        self.assertIn(_SESSION_AUTHORITY, first.stderr)

        self.assert_session_authority_closure(
            report_command(
                "phase4_confirmatory_per_net_report_runner",
                testing=False,
            )
        )

    def test_session_closure_precedes_missing_or_fifo_fixture_access(self) -> None:
        command = report_command(
            "phase4_confirmatory_per_net_report_test_runner",
            testing=True,
        )
        for fixture_kind in ("missing", "fifo"):
            with self.subTest(fixture_kind=fixture_kind):
                runfiles_root = self.root / fixture_kind
                fixture = (
                    runfiles_root
                    / "isolated"
                    / "tests"
                    / "fixtures"
                    / "phase4_supported_multinet_v1.kicad_pcb"
                )
                fixture.parent.mkdir(parents=True)
                if fixture_kind == "fifo":
                    os.mkfifo(fixture)
                env = os.environ.copy()
                env["TEST_SRCDIR"] = str(runfiles_root)
                env["TEST_WORKSPACE"] = "isolated"
                self.assert_session_authority_closure(command, env=env)

    def test_parser_and_scope_errors_remain_preflight_only(self) -> None:
        valid = report_command(
            "phase4_confirmatory_per_net_report_test_runner",
            testing=True,
        )
        mutations = (
            [argument for argument in valid if not argument.startswith("--corpus_version=")],
            replace(valid, "corpus_version", "1"),
            replace(valid, "raw_wire_schema_version", "2"),
            replace(valid, "pool_size", "8"),
            replace(valid, "case_id", "10201"),
            replace(valid, "case_id", "10100"),
            replace(valid, "raw_cell_artifact_checksum", "0"),
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
                    timeout=5,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")
                self.assertNotIn(_SESSION_AUTHORITY, completed.stderr)

    def test_publication_validator_authenticates_raw_before_report_open(self) -> None:
        report_fifo = self.root / "report.fifo"
        raw_fifo = self.root / "raw.fifo"
        os.mkfifo(report_fifo)
        os.mkfifo(raw_fifo)
        validator = str(runfile("phase4_confirmatory_per_net_report_validator"))

        invalid_raw = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={self.invalid_raw_path}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(invalid_raw.returncode, 1)
        self.assertNotIn("report must be a regular file", invalid_raw.stderr)

        fifo_raw = subprocess.run(
            [
                validator,
                f"--expected-commit={_COMMIT}",
                f"--raw={raw_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
        )
        self.assertEqual(fifo_raw.returncode, 1)
        self.assertIn("raw cell", fifo_raw.stderr)
        self.assertIn("regular file", fifo_raw.stderr)


if __name__ == "__main__":
    unittest.main()
