"""Process firewalls for the fixtureless Session-v5/H=4096 preflight."""

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

_MODES = (
    "controller-ordinary",
    "controller-same-run",
    "ordinary-worker-baseline",
    "ordinary-worker-candidate",
    "same-run-worker-baseline",
    "same-run-worker-candidate",
)
_CONTROLLER_MODES = frozenset(("controller-ordinary", "controller-same-run"))


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryH4096SessionV5PreflightProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.clean_runner = str(
            runfile("phase4_confirmatory_h4096_session_v5_preflight_test_runner")
        )
        cls.unpublishable_runner = str(
            runfile(
                "phase4_confirmatory_h4096_session_v5_unpublishable_source_preflight_test_runner"
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
    def arguments(mode: str, *extra: str) -> list[str]:
        arguments = [f"--mode={mode}"]
        if mode in _CONTROLLER_MODES:
            arguments.append(f"--runtime_commit={_EMBEDDED_COMMIT}")
        arguments.extend(extra)
        return arguments

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
            env=environment,
            pass_fds=pass_fds,
        )

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
        self.assertFalse(self.output.exists())

    def test_all_fixed_controller_and_worker_modes_stop_at_activation(self) -> None:
        for mode in _MODES:
            with self.subTest(mode=mode):
                self.assert_invariant(
                    self.clean_runner,
                    self.arguments(mode),
                    _ACTIVATION,
                )

    def test_unpublishable_source_fails_after_complete_configuration_preflight(self) -> None:
        for mode in _MODES:
            with self.subTest(mode=mode):
                self.assert_invariant(
                    self.unpublishable_runner,
                    self.arguments(mode),
                    _EMBEDDED_SOURCE,
                )

    def test_configuration_mutation_precedes_unpublishable_source(self) -> None:
        for mode in _MODES:
            for mutation in (
                "identity-carrier",
                "identity-endpoint",
                "cell-schema",
                "cell-case",
                "cell-pool",
                "cell-workers",
                "cell-repetitions",
                "cell-setup",
            ):
                with self.subTest(mode=mode, mutation=mutation):
                    completed = self.run_process(
                        self.unpublishable_runner,
                        self.arguments(mode, f"--mutation={mutation}"),
                    )
                    self.assertEqual(completed.returncode, 2, completed.stderr)
                    self.assertEqual(completed.stdout, "")
                    self.assertTrue(completed.stderr.endswith("\n"))
                    self.assertEqual(completed.stderr.count("\n"), 1)
                    self.assertNotEqual(completed.stderr, f"{_EMBEDDED_SOURCE}\n")
                    self.assertNotEqual(completed.stderr, f"{_CONTROLLER_SOURCE}\n")
                    self.assertNotEqual(completed.stderr, f"{_ACTIVATION}\n")
                    self.assertFalse(self.output.exists())

    def test_parser_rejects_unknown_duplicate_malformed_and_forbidden_options(self) -> None:
        probes = (
            [],
            ["--mode"],
            ["--mode="],
            ["--mode=historical"],
            ["--mode=controller-ordinary", "--mode=controller-same-run"],
            ["--mode=controller-ordinary"],
            [
                "--mode=controller-ordinary",
                f"--runtime_commit={_EMBEDDED_COMMIT}",
                f"--runtime_commit={_EMBEDDED_COMMIT}",
            ],
            ["--mode=controller-ordinary", "--runtime_commit="],
            ["--mode=ordinary-worker-baseline", f"--runtime_commit={_EMBEDDED_COMMIT}"],
            self.arguments("controller-ordinary", "--mutation="),
            self.arguments("controller-ordinary", "--mutation=unknown"),
            self.arguments("controller-ordinary", "--testing_allow_unstamped=1"),
            self.arguments("controller-ordinary", "--phase4_h4096_ordinary_worker=1"),
            self.arguments("controller-ordinary", "--phase4_h4096_same_run_worker=1"),
            self.arguments("controller-ordinary", "--arm=baseline"),
            self.arguments("controller-ordinary", "--fixture_path=must-not-exist"),
            self.arguments("controller-ordinary", "--output=must-not-exist"),
            self.arguments(
                "controller-ordinary",
                "--same_run_telemetry_output=must-not-exist",
            ),
            self.arguments("controller-ordinary", "--request_fd=3"),
            self.arguments("controller-ordinary", "--response_fd=4"),
            self.arguments("controller-ordinary", "--paired_budget_checksum=0"),
            self.arguments(
                "controller-ordinary",
                "--canonical_algorithm_budget_checksum=0",
            ),
            self.arguments("controller-ordinary", "--corpus_version=2"),
            self.arguments("controller-ordinary", "positional"),
        )
        for arguments in probes:
            with self.subTest(arguments=arguments):
                self.assert_invariant(self.clean_runner, list(arguments), _ARGUMENT)

    def test_source_commit_perturbations_fail_before_activation(self) -> None:
        for runtime_commit, invariant in (
            ("", _ARGUMENT),
            ("0", _CONTROLLER_SOURCE),
            ("0123456789abcdef0123456789abcdef01234568", _CONTROLLER_SOURCE),
            ("0123456789ABCDEF0123456789ABCDEF01234567", _CONTROLLER_SOURCE),
            ("g123456789abcdef0123456789abcdef01234567", _CONTROLLER_SOURCE),
        ):
            arguments = [
                "--mode=controller-ordinary",
                f"--runtime_commit={runtime_commit}",
            ]
            with self.subTest(runtime_commit=runtime_commit):
                self.assert_invariant(self.clean_runner, arguments, invariant)

    def test_environment_and_inherited_descriptors_cannot_bypass_or_gain_io(self) -> None:
        for runner, expected_invariant in (
            (self.clean_runner, _ACTIVATION),
            (self.unpublishable_runner, _EMBEDDED_SOURCE),
        ):
            for mode in _MODES:
                with self.subTest(runner=pathlib.Path(runner).name, mode=mode):
                    self.assert_environment_and_descriptors_untouched(
                        runner,
                        mode,
                        expected_invariant,
                    )

    def assert_environment_and_descriptors_untouched(
        self,
        runner: str,
        mode: str,
        expected_invariant: str,
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
                    "APGAR_PHASE4_FIXTURE_PATH": str(self.fixture_fifo),
                    "APGAR_PHASE4_REQUEST_FD": str(request_read),
                    "APGAR_PHASE4_RESPONSE_FD": str(response_write),
                    "APGAR_PHASE4_OUTPUT": str(self.output),
                    "APGAR_PHASE4_RUNTIME_COMMIT": _EMBEDDED_COMMIT,
                    "APGAR_PHASE4_TESTING_ALLOW_UNSTAMPED": "1",
                    "APGAR_PHASE4_SESSION_V5_ACTIVATE": "1",
                }
            )
            self.assert_invariant(
                runner,
                self.arguments(mode),
                expected_invariant,
                environment=poisoned,
                pass_fds=(request_read, response_write, fifo_read),
            )
            self.assertEqual(os.read(request_read, len(request_sentinel)), request_sentinel)
            self.assertEqual(os.read(fifo_read, len(fifo_sentinel)), fifo_sentinel)
            for descriptor in (request_read, response_read, fifo_read):
                with self.assertRaises(BlockingIOError):
                    os.read(descriptor, 1)
            self.assertFalse(self.output.exists())
        finally:
            for descriptor in (
                request_read,
                request_write,
                response_read,
                response_write,
            ):
                os.close(descriptor)
            os.close(fifo_read)
            os.close(fifo_write)


if __name__ == "__main__":
    unittest.main()
