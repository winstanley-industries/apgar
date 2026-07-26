import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryRawEvidenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        cls.sidecar_path = root / "exact-sidecar.json"
        cls.exact_raw = artifacts.make_raw(
            10_100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        cls.exact_sidecar = artifacts.make_sidecar(cls.exact_raw)
        cls.exact_raw_path = root / "exact-raw.json"
        cls.exact_raw_path.write_text(
            json.dumps(cls.exact_raw, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        cls.sidecar_path.write_text(
            json.dumps(cls.exact_sidecar, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

        cls.calibration_raw = artifacts.make_raw(
            10_200,
            4,
            same_run=False,
            h4096=False,
            repetitions=1,
            canonical_confirmatory_caps=True,
        )

        legacy = subprocess.run(
            [
                str(runfile("phase4_evidence_runner")),
                "--testing_allow_unstamped=1",
                "--case_id=100",
                "--pool_size=4",
                "--workers=4",
                "--repetitions=1",
                "--setup_ns=300000000000",
                "--prepared_ns=300000000000",
                "--cold_ns=300000000000",
                "--address_space_bytes=68719476736",
                "--peak_host_bytes=17179869184",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if legacy.returncode != 0:
            raise RuntimeError(legacy.stderr)
        cls.legacy_raw = json.loads(legacy.stdout)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_synthetic_exact_same_run_and_calibration_ordinary_cells_validate(self) -> None:
        raw_validator.validate_confirmatory_same_run_document_v2(
            self.exact_raw,
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )
        telemetry_validator.validate_confirmatory_join(
            self.exact_raw,
            self.exact_sidecar,
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )
        self.assertTrue(telemetry_validator.exact_rejection_guardrail_passes(self.exact_sidecar))
        raw_validator.validate_confirmatory_document(
            self.calibration_raw,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "ordinary Raw authority"):
            raw_validator._validate_confirmatory_protocol_scope(self.exact_raw, same_run=False)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "same-run Raw authority"):
            raw_validator._validate_confirmatory_protocol_scope(self.calibration_raw, same_run=True)

    def test_v1_and_v2_authorities_reject_each_other(self) -> None:
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            raw_validator.validate_same_run_document_v2(
                self.exact_raw,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            raw_validator.validate_confirmatory_document(
                self.legacy_raw,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

    def test_confirmatory_runner_closes_session_before_fixture_access(self) -> None:
        missing_fixture = pathlib.Path(self.temporary.name) / "controller-must-not-open.kicad_pcb"
        fifo_fixture = pathlib.Path(self.temporary.name) / "controller-must-not-read.fifo"
        os.mkfifo(fifo_fixture)
        for fixture in (missing_fixture, fifo_fixture):
            with self.subTest(fixture=fixture.name):
                completed = subprocess.run(
                    [
                        str(runfile("phase4_confirmatory_evidence_test_runner")),
                        "--corpus_version=2",
                        "--testing_allow_unstamped=1",
                        "--case_id=10200",
                        "--pool_size=4",
                        f"--fixture_path={fixture}",
                    ],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)
                self.assertNotIn("failed to read", completed.stderr)

    def test_direct_confirmatory_workers_enforce_scope_before_fixture_access(self) -> None:
        runner = str(runfile("phase4_confirmatory_evidence_test_runner"))
        missing_fixture = pathlib.Path(self.temporary.name) / "must-not-be-opened.kicad_pcb"
        common = [
            runner,
            "--corpus_version=2",
            "--arm=baseline",
            "--pool_size=4",
            "--workers=4",
            "--repetitions=20",
            "--setup_ns=300000000000",
            "--prepared_ns=300000000000",
            "--cold_ns=300000000000",
            "--address_space_bytes=68719476736",
            "--peak_host_bytes=17179869184",
            f"--fixture_path={missing_fixture}",
            "--request_fd=0",
            "--response_fd=1",
        ]
        for worker_mode, case_id in (
            ("--phase4_worker=1", 10200),
            ("--phase4_same_run_worker=1", 10100),
        ):
            with self.subTest(worker_mode=worker_mode, case_id=case_id):
                completed = subprocess.run(
                    common + [worker_mode, f"--case_id={case_id}"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)
                self.assertNotIn("failed to read", completed.stderr)

    def test_confirmatory_runner_enforces_protocol_assigned_raw_authority(self) -> None:
        runner = str(runfile("phase4_confirmatory_evidence_test_runner"))
        for arguments in (
            ["--case_id=10100", "--pool_size=4"],
            [
                "--case_id=10200",
                "--pool_size=4",
                f"--same_run_telemetry_output={pathlib.Path(self.temporary.name) / 'forbidden.json'}",
            ],
        ):
            completed = subprocess.run(
                [
                    runner,
                    "--corpus_version=2",
                    "--testing_allow_unstamped=1",
                    *arguments,
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 2)
            self.assertIn("protocol-assigned raw authority", completed.stderr)

    def test_production_runner_has_no_unstamped_escape(self) -> None:
        completed = subprocess.run(
            [
                str(runfile("phase4_confirmatory_evidence_runner")),
                "--corpus_version=2",
                "--testing_allow_unstamped=1",
                "--case_id=10100",
                "--pool_size=4",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("requires --corpus_version=2", completed.stderr)

    def test_fault_runner_cannot_bypass_session_closure_or_publish_a_sidecar(self) -> None:
        sidecar = pathlib.Path(self.temporary.name) / "forbidden-sidecar.json"
        fifo_fixture = pathlib.Path(self.temporary.name) / "fault-runner-must-not-read.fifo"
        os.mkfifo(fifo_fixture)
        environment = os.environ.copy()
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "second_run_bad_association"
        completed = subprocess.run(
            [
                str(runfile("phase4_confirmatory_evidence_fault_test_runner")),
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
                f"--fixture_path={fifo_fixture}",
                f"--same_run_telemetry_output={sidecar}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn(_SESSION_AUTHORITY, completed.stderr)
        self.assertNotIn("failed to read", completed.stderr)
        self.assertFalse(sidecar.exists())

    def test_validator_clis_reject_fifo_inputs_without_blocking(self) -> None:
        fifo = pathlib.Path(self.temporary.name) / "validator-input.fifo"
        os.mkfifo(fifo)
        raw_validation = subprocess.run(
            [
                str(runfile("phase4_confirmatory_raw_evidence_validator")),
                "--testing-allow-unstamped",
                "--testing-repetitions=1",
                "--testing-workers=4",
                str(fifo),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(raw_validation.returncode, 1)
        self.assertIn("regular file", raw_validation.stderr)

        telemetry_validation = subprocess.run(
            [
                str(runfile("phase4_confirmatory_same_run_decision_telemetry_validator")),
                "--testing-allow-unstamped",
                "--testing-repetitions=20",
                "--testing-workers=4",
                str(self.exact_raw_path),
                str(fifo),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(telemetry_validation.returncode, 1)
        self.assertIn("regular file", telemetry_validation.stderr)


if __name__ == "__main__":
    unittest.main()
