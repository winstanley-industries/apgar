"""Process firewall for the fixtureless Session-v5 ordinary report-producer preflight."""

from __future__ import annotations

import os
import pathlib
import subprocess
import tempfile
import unittest

_ACTIVATION = "P4PAIR-H4096-SESSION-V5-ACTIVATION-001"
_ARGUMENT = "P4PAIR-H4096-SESSION-V5-ARGUMENT-001"
_EMBEDDED_SOURCE = "P4PAIR-H4096-SESSION-V5-SOURCE-001"
_CONTROLLER_SOURCE = "P4PAIR-H4096-SESSION-V5-SOURCE-002"
_EMBEDDED_COMMIT = "0123456789abcdef0123456789abcdef01234567"


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryH4096SessionV5PerNetReportProducerPreflightProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.clean_runner = str(
            runfile(
                "phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner"
            )
        )
        cls.unpublishable_runner = str(
            runfile(
                "phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner"
            )
        )
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.output = cls.root / "must-not-be-created.json"
        cls.fixture_fifo = cls.root / "must-not-be-opened.fifo"
        os.mkfifo(cls.fixture_fifo)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    @staticmethod
    def arguments(runtime_commit: str = _EMBEDDED_COMMIT) -> list[str]:
        return [f"--runtime_commit={runtime_commit}"]

    def run_process(
        self,
        runner: str,
        arguments: list[str],
        *,
        environment: dict[str, str] | None = None,
        pass_fds: tuple[int, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [runner, *arguments],
            check=False,
            text=True,
            capture_output=True,
            timeout=5,
            cwd=self.root,
            env=environment,
            pass_fds=pass_fds,
        )

    def assert_no_artifacts(self) -> None:
        self.assertFalse(self.output.exists())
        self.assertEqual(set(self.root.iterdir()), {self.fixture_fifo})

    def assert_invariant(
        self,
        runner: str,
        arguments: list[str],
        invariant: str,
        *,
        environment: dict[str, str] | None = None,
        pass_fds: tuple[int, ...] = (),
    ) -> None:
        completed = self.run_process(
            runner,
            arguments,
            environment=environment,
            pass_fds=pass_fds,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertEqual(completed.stderr, f"{invariant}\n")
        self.assert_no_artifacts()

    def test_clean_identity_stops_at_shared_activation_barrier(self) -> None:
        self.assert_invariant(
            self.clean_runner,
            self.arguments(),
            _ACTIVATION,
        )

    def test_forced_unpublishable_source_fails_before_activation(self) -> None:
        self.assert_invariant(
            self.unpublishable_runner,
            self.arguments(),
            _EMBEDDED_SOURCE,
        )

    def test_source_commit_perturbations_fail_before_activation(self) -> None:
        cases = (
            ("0123456789abcdef0123456789abcdef01234568", _CONTROLLER_SOURCE),
            ("0123456789ABCDEF0123456789ABCDEF01234567", _ARGUMENT),
            ("0123456789abcdef0123456789abcdef0123456", _ARGUMENT),
            ("g123456789abcdef0123456789abcdef01234567", _ARGUMENT),
            ("0123456789abcdef0123456789abcdef012345678", _ARGUMENT),
            ("", _ARGUMENT),
        )
        for runtime_commit, invariant in cases:
            with self.subTest(runtime_commit=runtime_commit):
                self.assert_invariant(
                    self.clean_runner,
                    self.arguments(runtime_commit),
                    invariant,
                )

    def test_parser_rejects_missing_duplicate_abbreviated_and_forbidden_arguments(self) -> None:
        valid = self.arguments()[0]
        probes = (
            [],
            ["positional"],
            ["--runtime_commit"],
            ["--runtime_commi=" + _EMBEDDED_COMMIT],
            ["--runtime_commit="],
            [valid, valid],
            [valid, "positional"],
            [valid, "--unknown=1"],
            ["--carrier=ordinary"],
            ["--case_id=10200"],
            ["--pool=8"],
            ["--schema_version=1"],
            ["--authority=phase4_confirmatory_per_net_report_publication_join_v3"],
            ["--canonical_algorithm_budget_checksum=7657176792159702821"],
            ["--paired_budget_checksum=13340538727848385478"],
            ["--raw=must-not-exist"],
            ["--report=must-not-exist"],
            ["--telemetry=must-not-exist"],
            ["--fixture_path=must-not-exist"],
            ["--path=must-not-exist"],
            ["--output=must-not-exist"],
            ["--request_fd=3"],
            ["--response_fd=4"],
            ["--child=must-not-exist"],
            ["--activate=1"],
            ["--testing_allow_unstamped=1"],
            ["--mode=controller-ordinary"],
            ["--mutation=identity-schema"],
        )
        for arguments in probes:
            with self.subTest(arguments=arguments):
                self.assert_invariant(self.clean_runner, list(arguments), _ARGUMENT)

    def test_environment_fifos_outputs_and_inherited_descriptors_remain_untouched(self) -> None:
        for runner, invariant in (
            (self.clean_runner, _ACTIVATION),
            (self.unpublishable_runner, _EMBEDDED_SOURCE),
        ):
            with self.subTest(runner=pathlib.Path(runner).name):
                self.assert_environment_and_descriptors_untouched(runner, invariant)

    def assert_environment_and_descriptors_untouched(
        self,
        runner: str,
        invariant: str,
    ) -> None:
        request_read, request_write = os.pipe()
        response_read, response_write = os.pipe()
        fifo_read = os.open(self.fixture_fifo, os.O_RDONLY | os.O_NONBLOCK)
        fifo_write = os.open(self.fixture_fifo, os.O_WRONLY | os.O_NONBLOCK)
        request_sentinel = b"request-must-remain"
        fifo_sentinel = b"fixture-must-remain"
        try:
            os.write(request_write, request_sentinel)
            os.write(fifo_write, fifo_sentinel)
            os.set_blocking(request_read, False)
            os.set_blocking(response_read, False)
            poisoned = os.environ.copy()
            poisoned.update(
                {
                    "APGAR_PHASE4_CARRIER": "same_run",
                    "APGAR_PHASE4_CASE_ID": "10100",
                    "APGAR_PHASE4_FIXTURE_PATH": str(self.fixture_fifo),
                    "APGAR_PHASE4_RAW": str(self.fixture_fifo),
                    "APGAR_PHASE4_REPORT": str(self.fixture_fifo),
                    "APGAR_PHASE4_TELEMETRY": str(self.fixture_fifo),
                    "APGAR_PHASE4_REQUEST_FD": str(request_read),
                    "APGAR_PHASE4_RESPONSE_FD": str(response_write),
                    "APGAR_PHASE4_OUTPUT": str(self.output),
                    "APGAR_PHASE4_CHILD": str(self.output),
                    "APGAR_PHASE4_RUNTIME_COMMIT": _EMBEDDED_COMMIT,
                    "APGAR_PHASE4_TESTING_ALLOW_UNSTAMPED": "1",
                    "APGAR_PHASE4_SESSION_V5_ACTIVATE": "1",
                }
            )
            self.assert_invariant(
                runner,
                self.arguments(),
                invariant,
                environment=poisoned,
                pass_fds=(request_read, response_write, fifo_read),
            )
            self.assertEqual(os.read(request_read, len(request_sentinel)), request_sentinel)
            self.assertEqual(os.read(fifo_read, len(fifo_sentinel)), fifo_sentinel)
            for descriptor in (request_read, response_read, fifo_read):
                with self.assertRaises(BlockingIOError):
                    os.read(descriptor, 1)
            self.assert_no_artifacts()
        finally:
            for descriptor in (
                request_read,
                request_write,
                response_read,
                response_write,
                fifo_read,
                fifo_write,
            ):
                os.close(descriptor)


if __name__ == "__main__":
    unittest.main()
