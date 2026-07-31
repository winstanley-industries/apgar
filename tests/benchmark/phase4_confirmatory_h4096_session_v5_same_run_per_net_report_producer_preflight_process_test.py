"""Process firewall for the fixtureless Session-v5 same-run report-producer preflight."""

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
_POSITIVE_STAMP_BASENAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.ok"
)
_NEGATIVE_STAMP_BASENAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok"
)
_POSITIVE_STAMP_INVARIANT = "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-001"
_NEGATIVE_STAMP_INVARIANT = (
    "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-NEGATIVE-001"
)
_CONFIGURATIONS = frozenset({"normal", "asan", "ubsan"})


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerPreflightProcessTest(
    unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        cls.clean_runner = str(
            runfile(
                "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_"
                "producer_preflight_test_runner"
            )
        )
        cls.unpublishable_runner = str(
            runfile(
                "phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_"
                "per_net_report_producer_preflight_test_runner"
            )
        )
        cls.positive_stamp = runfile(_POSITIVE_STAMP_BASENAME)
        cls.negative_stamp = runfile(_NEGATIVE_STAMP_BASENAME)
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.output = cls.root / "must-not-be-created.json"
        cls.raw_fifo = cls.root / "raw-must-not-be-opened.fifo"
        cls.telemetry_fifo = cls.root / "telemetry-must-not-be-opened.fifo"
        cls.report_fifo = cls.root / "report-must-not-be-opened.fifo"
        cls.fixture_fifo = cls.root / "fixture-must-not-be-opened.fifo"
        cls.fifos = (
            cls.raw_fifo,
            cls.telemetry_fifo,
            cls.report_fifo,
            cls.fixture_fifo,
        )
        for fifo in cls.fifos:
            os.mkfifo(fifo)

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
        self.assertEqual(set(self.root.iterdir()), set(self.fifos))

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

    def read_audit_stamp(
        self,
        path: pathlib.Path,
        basename: str,
        invariant: str,
    ) -> str:
        self.assertEqual(path.name, basename)
        content = path.read_text(encoding="utf-8")
        lines = content.splitlines(keepends=True)
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[0], f"{invariant}\n")
        self.assertTrue(lines[1].startswith("configuration="))
        configuration = lines[1].removeprefix("configuration=").removesuffix("\n")
        self.assertIn(configuration, _CONFIGURATIONS)
        self.assertEqual(content, f"{invariant}\nconfiguration={configuration}\n")
        return configuration

    def test_semantic_audit_stamps_authenticate_the_same_configuration(self) -> None:
        positive_configuration = self.read_audit_stamp(
            self.positive_stamp,
            _POSITIVE_STAMP_BASENAME,
            _POSITIVE_STAMP_INVARIANT,
        )
        negative_configuration = self.read_audit_stamp(
            self.negative_stamp,
            _NEGATIVE_STAMP_BASENAME,
            _NEGATIVE_STAMP_INVARIANT,
        )
        self.assertEqual(positive_configuration, negative_configuration)

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
            ["--carrier=same_run"],
            ["--case_id=10100"],
            ["--pool=4"],
            ["--schema_version=1"],
            ["--raw_schema_version=2"],
            ["--telemetry_schema_version=1"],
            ["--report_schema_version=1"],
            ["--authority=phase4_confirmatory_same_run_per_net_report_publication_join_v3"],
            ["--raw_authority=phase4_confirmatory_same_run_raw_evidence_v3"],
            ["--telemetry_authority=phase4_confirmatory_same_run_decision_telemetry_v3"],
            ["--report_authority=phase4_confirmatory_same_run_per_net_report_publication_join_v3"],
            ["--canonical_algorithm_budget_checksum=13645569624513409309"],
            ["--paired_budget_checksum=12493092620111240227"],
            ["--raw=must-not-exist"],
            ["--telemetry=must-not-exist"],
            ["--report=must-not-exist"],
            ["--fixture_path=must-not-exist"],
            ["--path=must-not-exist"],
            ["--output=must-not-exist"],
            ["--request_fd=3"],
            ["--response_fd=4"],
            ["--child=must-not-exist"],
            ["--activate=1"],
            ["--testing_allow_unstamped=1"],
            ["--mode=controller-same-run"],
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
        fifo_reads = [os.open(fifo, os.O_RDONLY | os.O_NONBLOCK) for fifo in self.fifos]
        fifo_writes = [os.open(fifo, os.O_WRONLY | os.O_NONBLOCK) for fifo in self.fifos]
        request_sentinel = b"request-must-remain"
        fifo_sentinels = (
            b"raw-must-remain",
            b"telemetry-must-remain",
            b"report-must-remain",
            b"fixture-must-remain",
        )
        try:
            os.write(request_write, request_sentinel)
            for descriptor, sentinel in zip(fifo_writes, fifo_sentinels, strict=True):
                os.write(descriptor, sentinel)
            os.set_blocking(request_read, False)
            os.set_blocking(response_read, False)
            poisoned = os.environ.copy()
            poisoned.update(
                {
                    "APGAR_PHASE4_CARRIER": "same_run",
                    "APGAR_PHASE4_CASE_ID": "10100",
                    "APGAR_PHASE4_POOL": "4",
                    "APGAR_PHASE4_FIXTURE_PATH": str(self.fixture_fifo),
                    "APGAR_PHASE4_RAW": str(self.raw_fifo),
                    "APGAR_PHASE4_RAW_PATH": str(self.raw_fifo),
                    "APGAR_PHASE4_TELEMETRY": str(self.telemetry_fifo),
                    "APGAR_PHASE4_TELEMETRY_PATH": str(self.telemetry_fifo),
                    "APGAR_PHASE4_REPORT": str(self.report_fifo),
                    "APGAR_PHASE4_REPORT_PATH": str(self.report_fifo),
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
                pass_fds=(request_read, response_write, *fifo_reads),
            )
            self.assertEqual(os.read(request_read, len(request_sentinel)), request_sentinel)
            for descriptor, sentinel in zip(fifo_reads, fifo_sentinels, strict=True):
                self.assertEqual(os.read(descriptor, len(sentinel)), sentinel)
            for descriptor in (request_read, response_read, *fifo_reads):
                with self.assertRaises(BlockingIOError):
                    os.read(descriptor, 1)
            self.assert_no_artifacts()
        finally:
            for descriptor in (
                request_read,
                request_write,
                response_read,
                response_write,
                *fifo_reads,
                *fifo_writes,
            ):
                os.close(descriptor)


if __name__ == "__main__":
    unittest.main(argv=[__file__])
