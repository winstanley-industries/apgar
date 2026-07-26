"""Acquisition-free process firewall tests for the fixed H4096 Raw runner."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import unittest

_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryH4096PreflightProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.missing_fixture = cls.root / "intentionally-absent.kicad_pcb"
        cls.fifo_fixture = cls.root / "must-not-be-opened.fifo"
        os.mkfifo(cls.fifo_fixture)
        cls.test_runner = str(runfile("phase4_confirmatory_h4096_evidence_test_runner"))
        cls.production_runner = str(runfile("phase4_confirmatory_h4096_evidence_runner"))
        cls.unpublishable_source_runner = str(
            runfile("phase4_confirmatory_h4096_unpublishable_source_test_runner")
        )
        cls.v1_parser_runner = str(runfile("phase4_v1_fixtureless_parser_test_runner"))
        cls.h2250_parser_runner = str(runfile("phase4_confirmatory_fixtureless_parser_test_runner"))

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def run_process(
        self,
        runner: str,
        arguments: list[str],
        *,
        timeout: int = 5,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [runner, *arguments],
            check=False,
            text=True,
            capture_output=True,
            timeout=timeout,
        )

    def controller(
        self,
        *,
        case_id: str,
        pool: str,
        fixture: pathlib.Path,
        same_run: bool,
        include_testing_escape: bool = True,
    ) -> list[str]:
        arguments = [
            "--corpus_version=2",
            f"--case_id={case_id}",
            f"--pool_size={pool}",
            f"--fixture_path={fixture}",
        ]
        if include_testing_escape:
            arguments.append("--testing_allow_unstamped=1")
        if same_run:
            arguments.append(
                f"--same_run_telemetry_output={self.root / 'must-not-be-created.json'}"
            )
        return arguments

    def worker(
        self,
        *,
        case_id: str,
        pool: str,
        fixture: pathlib.Path,
        same_run: bool,
    ) -> list[str]:
        return [
            "--corpus_version=2",
            "--phase4_h4096_same_run_worker=1" if same_run else "--phase4_h4096_ordinary_worker=1",
            "--arm=baseline",
            f"--case_id={case_id}",
            f"--pool_size={pool}",
            "--workers=4",
            "--repetitions=20",
            "--setup_ns=300000000000",
            "--prepared_ns=300000000000",
            "--cold_ns=300000000000",
            "--address_space_bytes=68719476736",
            "--peak_host_bytes=17179869184",
            f"--fixture_path={fixture}",
            "--request_fd=0",
            "--response_fd=1",
        ]

    def assert_scope_rejected(self, arguments: list[str]) -> None:
        completed = self.run_process(self.test_runner, arguments)
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn("H4096 development acquisition is restricted", completed.stderr)
        self.assertNotIn("failed to read", completed.stderr)

    def assert_session_closed(self, runner: str, arguments: list[str]) -> None:
        completed = self.run_process(runner, arguments)
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn(_SESSION_AUTHORITY, completed.stderr)
        self.assertNotIn("failed to read", completed.stderr)
        self.assertFalse((self.root / "must-not-be-created.json").exists())

    @staticmethod
    def replace_or_append(arguments: list[str], key: str, value: str) -> list[str]:
        prefix = f"--{key}="
        replaced = False
        result = []
        for argument in arguments:
            if argument.startswith(prefix):
                result.append(f"{prefix}{value}")
                replaced = True
            else:
                result.append(argument)
        if not replaced:
            result.append(f"{prefix}{value}")
        return result

    def test_session_authority_closes_all_controller_and_hidden_worker_modes(self) -> None:
        for runner, h4096 in (
            (self.test_runner, True),
            (self.h2250_parser_runner, False),
        ):
            for case_id, pool, same_run in (("10100", "4", True), ("10200", "8", False)):
                for fixture in (self.missing_fixture, self.fifo_fixture):
                    worker = self.worker(
                        case_id=case_id,
                        pool=pool,
                        fixture=fixture,
                        same_run=same_run,
                    )
                    if not h4096:
                        worker[1] = (
                            "--phase4_same_run_worker=1" if same_run else "--phase4_worker=1"
                        )
                    for mode, arguments in (
                        (
                            "controller",
                            self.controller(
                                case_id=case_id,
                                pool=pool,
                                fixture=fixture,
                                same_run=same_run,
                            ),
                        ),
                        ("worker", worker),
                    ):
                        with self.subTest(
                            runner=pathlib.Path(runner).name,
                            mode=mode,
                            case_id=case_id,
                            fixture=fixture.name,
                        ):
                            self.assert_session_closed(runner, arguments)

    def test_h2250_session_authority_precedes_default_fixture_resolution(self) -> None:
        controller = self.controller(
            case_id="10200",
            pool="8",
            fixture=self.missing_fixture,
            same_run=False,
        )
        worker = self.worker(
            case_id="10100",
            pool="4",
            fixture=self.missing_fixture,
            same_run=True,
        )
        worker[1] = "--phase4_same_run_worker=1"
        for mode, arguments in (("controller", controller), ("worker", worker)):
            without_fixture = [
                argument for argument in arguments if not argument.startswith("--fixture_path=")
            ]
            with self.subTest(mode=mode):
                self.assert_session_closed(self.h2250_parser_runner, without_fixture)

    def test_controller_rejects_every_other_scope_before_fifo_access(self) -> None:
        probes = (
            ("10100", "4", False),
            ("10100", "8", True),
            ("10101", "4", True),
            ("10200", "4", False),
            ("10200", "16", False),
            ("10200", "8", True),
            ("10201", "8", False),
            ("11000", "4", True),
            ("12002", "4", False),
            ("13000", "4", False),
            ("14000", "4", True),
        )
        for case_id, pool, same_run in probes:
            with self.subTest(case_id=case_id, pool=pool, same_run=same_run):
                self.assert_scope_rejected(
                    self.controller(
                        case_id=case_id,
                        pool=pool,
                        fixture=self.fifo_fixture,
                        same_run=same_run,
                    )
                )

    def test_hidden_workers_repeat_scope_before_fixture_access(self) -> None:
        probes = (
            ("10100", "4", False),
            ("10100", "8", True),
            ("10200", "8", True),
            ("10200", "4", False),
            ("11000", "4", True),
            ("14000", "4", False),
        )
        for case_id, pool, same_run in probes:
            with self.subTest(valid=False, case_id=case_id, pool=pool, same_run=same_run):
                self.assert_scope_rejected(
                    self.worker(
                        case_id=case_id,
                        pool=pool,
                        fixture=self.fifo_fixture,
                        same_run=same_run,
                    )
                )

    def test_hidden_worker_flag_names_are_cross_target_closed(self) -> None:
        for old_flag in ("--phase4_worker=1", "--phase4_same_run_worker=1"):
            arguments = self.worker(
                case_id="10200",
                pool="8",
                fixture=self.fifo_fixture,
                same_run=False,
            )
            arguments[1] = old_flag
            completed = self.run_process(self.test_runner, arguments)
            self.assertEqual(completed.returncode, 2)
            self.assertIn("phase4_confirmatory_h4096_evidence_runner requires", completed.stderr)
            self.assertNotIn("failed to read", completed.stderr)

        for runner, corpus_version in (
            (self.v1_parser_runner, "1"),
            (self.h2250_parser_runner, "2"),
        ):
            for same_run in (False, True):
                case_id = "10100" if same_run else "10200"
                pool = "4" if same_run else "8"
                arguments = self.worker(
                    case_id=case_id if corpus_version == "2" else "100",
                    pool=pool if corpus_version == "2" else "4",
                    fixture=self.fifo_fixture,
                    same_run=same_run,
                )
                arguments[0] = f"--corpus_version={corpus_version}"
                with self.subTest(
                    runner=pathlib.Path(runner).name,
                    same_run=same_run,
                ):
                    completed = self.run_process(runner, arguments)
                    self.assertEqual(completed.returncode, 2, completed.stderr)
                    self.assertIn("requires", completed.stderr)
                    self.assertNotIn("failed to read", completed.stderr)

    def test_test_runner_is_irrevocably_nonpublishable(self) -> None:
        without_escape = self.run_process(
            self.test_runner,
            self.controller(
                case_id="10200",
                pool="8",
                fixture=self.fifo_fixture,
                same_run=False,
                include_testing_escape=False,
            ),
        )
        self.assertEqual(without_escape.returncode, 2)
        self.assertNotIn("failed to read", without_escape.stderr)

        with_commit = self.run_process(
            self.test_runner,
            [
                *self.controller(
                    case_id="10200",
                    pool="8",
                    fixture=self.fifo_fixture,
                    same_run=False,
                ),
                f"--apgar_commit={'a' * 40}",
            ],
        )
        self.assertEqual(with_commit.returncode, 2)
        self.assertNotIn("failed to read", with_commit.stderr)

    def test_production_rejects_test_escape_before_fixture_access(self) -> None:
        completed = self.run_process(
            self.production_runner,
            self.controller(
                case_id="10200",
                pool="8",
                fixture=self.fifo_fixture,
                same_run=False,
            ),
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("phase4_confirmatory_h4096_evidence_runner requires", completed.stderr)
        self.assertNotIn("failed to read", completed.stderr)

    def test_complete_preflight_precedes_source_and_fixture_access(self) -> None:
        mutations = (
            (
                "workers",
                "3",
                "P4HARNESS-H4096-CANONICAL-CARDINALITY-001",
            ),
            (
                "repetitions",
                "19",
                "P4HARNESS-H4096-CANONICAL-CARDINALITY-001",
            ),
            (
                "prepared_ns",
                "0",
                "P4HARNESS-SPEC-001",
            ),
        )
        for case_id, pool, same_run in (("10100", "4", True), ("10200", "8", False)):
            controller = self.controller(
                case_id=case_id,
                pool=pool,
                fixture=self.fifo_fixture,
                same_run=same_run,
                include_testing_escape=False,
            )
            worker = self.worker(
                case_id=case_id,
                pool=pool,
                fixture=self.fifo_fixture,
                same_run=same_run,
            )
            for mode, base_arguments in (("controller", controller), ("worker", worker)):
                for key, value, invariant in mutations:
                    with self.subTest(
                        mode=mode,
                        same_run=same_run,
                        key=key,
                    ):
                        completed = self.run_process(
                            self.unpublishable_source_runner,
                            self.replace_or_append(base_arguments, key, value),
                        )
                        self.assertEqual(completed.returncode, 2, completed.stderr)
                        self.assertIn(invariant, completed.stderr)
                        self.assertNotIn("failed to read", completed.stderr)

    def test_session_authority_precedes_production_source_and_fixture_access(self) -> None:
        controller_arguments = self.controller(
            case_id="10200",
            pool="8",
            fixture=self.fifo_fixture,
            same_run=False,
            include_testing_escape=False,
        )
        for apgar_commit in (None, "a" * 40):
            arguments = list(controller_arguments)
            if apgar_commit is not None:
                arguments.append(f"--apgar_commit={apgar_commit}")
            with self.subTest(mode="controller", apgar_commit=apgar_commit):
                completed = self.run_process(self.production_runner, arguments)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)
                self.assertNotIn("failed to read", completed.stderr)

        for case_id, pool, same_run in (("10100", "4", True), ("10200", "8", False)):
            with self.subTest(mode="worker", same_run=same_run):
                completed = self.run_process(
                    self.unpublishable_source_runner,
                    self.worker(
                        case_id=case_id,
                        pool=pool,
                        fixture=self.fifo_fixture,
                        same_run=same_run,
                    ),
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)
                self.assertNotIn("failed to read", completed.stderr)

    def test_v1_diagnostic_runner_remains_outside_the_session_closure(self) -> None:
        completed = self.run_process(
            self.v1_parser_runner,
            [
                "--corpus_version=1",
                "--case_id=100",
                "--pool_size=4",
                "--testing_allow_unstamped=1",
                f"--fixture_path={self.missing_fixture}",
            ],
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertNotIn(_SESSION_AUTHORITY, completed.stderr)
        self.assertIn("failed to read the imported Phase 4 fixture", completed.stderr)

    def test_strict_parser_rejects_malformed_and_duplicate_authority_inputs(self) -> None:
        probes = (
            ["--corpus_version=1", "--case_id=10200", "--pool_size=8"],
            ["--corpus_version=2", "--case_id=010200", "--pool_size=8"],
            ["--corpus_version=2", "--case_id=-10200", "--pool_size=8"],
            ["--corpus_version=2", "--case_id=10200x", "--pool_size=8"],
            ["--corpus_version=2", "--case_id=4294967296", "--pool_size=8"],
            [
                "--corpus_version=2",
                "--case_id=10200",
                "--case_id=10200",
                "--pool_size=8",
            ],
        )
        for probe in probes:
            with self.subTest(probe=probe):
                completed = self.run_process(
                    self.test_runner,
                    [
                        *probe,
                        "--testing_allow_unstamped=1",
                        f"--fixture_path={self.fifo_fixture}",
                    ],
                )
                self.assertEqual(completed.returncode, 2)
                self.assertNotIn("failed to read", completed.stderr)


if __name__ == "__main__":
    unittest.main()
