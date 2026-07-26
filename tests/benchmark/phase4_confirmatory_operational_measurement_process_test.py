"""Acquisition-free process firewall for ordinary Corpus-v2 operations."""

from __future__ import annotations

import contextlib
import copy
import io
import os
import pathlib
import resource
import subprocess
import unittest
from unittest import mock

from tests.support import phase4_confirmatory_operational_test_artifacts as artifacts
from tools import capture_phase4_confirmatory_operational_measurement as capture_controller
from tools import phase4_confirmatory_operational_authority as authority
from tools import validate_phase4_confirmatory_operational_measurement as publisher
from tools import validate_phase4_operational_measurement as operational_validator

_COMMIT = "a" * 40
_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def capture_command(*, production: bool) -> list[str]:
    executable = (
        "phase4_confirmatory_operational_capture"
        if production
        else "phase4_confirmatory_operational_capture_test"
    )
    command = [
        str(runfile(executable)),
        "--corpus-version=2",
        "--raw-wire-schema-version=1",
        "--case-id=10200",
        "--pool-size=4",
        "--workers=4",
        "--setup-ns=300000000000",
        "--prepared-ns=300000000000",
        "--cold-ns=300000000000",
        "--address-space-bytes=68719476736",
        "--peak-host-bytes=17179869184",
        "--maximum-nets=4096",
        "--maximum-compiled-nodes=100000000",
        "--maximum-compiled-host-bytes=8589934592",
        "--maximum-active-regions=250000",
        "--maximum-board-entities=100000",
        f"--apgar-commit={_COMMIT}",
    ]
    if not production:
        command.append("--testing-allow-unstamped")
    return command


def worker_command(*, production: bool, mode: str, arm: str) -> list[str]:
    executable = (
        "phase4_confirmatory_operational_replay_worker"
        if production
        else "phase4_confirmatory_operational_replay_test_worker"
    )
    command = [
        str(runfile(executable)),
        "--corpus_version=2",
        "--raw_wire_schema_version=1",
        f"--mode={mode}",
        f"--arm={arm}",
        "--case_id=10200",
        "--pool_size=4",
        "--workers=4",
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
        f"--apgar_commit={_COMMIT}",
    ]
    if not production:
        command.append("--testing_allow_unstamped=1")
    return command


def install_wrong_address_space_limit() -> None:
    resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))


class Phase4ConfirmatoryOperationalMeasurementProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw, sidecar, cls.capture = artifacts.make_operational_inputs(
            case_id=10_200,
            pool=4,
            same_run=False,
            h4096=False,
            authority_module=authority,
            commit=_COMMIT,
        )
        if sidecar is not None:
            raise AssertionError("ordinary operational input unexpectedly has telemetry")
        cls.publication = operational_validator.project_confirmatory_ordinary_document(
            cls.raw,
            cls.capture,
        )

    def assert_session_closed(
        self,
        command: list[str],
        *,
        wrong_address_space_limit: bool = False,
    ) -> None:
        completed = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            preexec_fn=install_wrong_address_space_limit if wrong_address_space_limit else None,
        )
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn(_SESSION_AUTHORITY, completed.stderr)
        self.assertNotIn("RLIMIT", completed.stderr)
        self.assertNotIn("failed to read", completed.stderr)
        self.assertNotIn("preparer", completed.stderr)

    def test_production_and_test_controllers_stop_at_session_authority(self) -> None:
        for production in (False, True):
            with self.subTest(production=production):
                self.assert_session_closed(capture_command(production=production))

    def test_every_child_role_stops_before_source_rlimit_and_allocator_access(self) -> None:
        for production in (False, True):
            for mode in ("measured", "authority"):
                for arm in ("baseline", "candidate"):
                    with self.subTest(production=production, mode=mode, arm=arm):
                        self.assert_session_closed(
                            worker_command(production=production, mode=mode, arm=arm),
                            wrong_address_space_limit=True,
                        )

    def test_common_controller_closes_before_worker_resolution(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
            mock.patch.object(authority, "resolve_bundled_worker") as resolve_worker,
            mock.patch.object(capture_controller.capture_tool, "capture") as capture,
        ):
            returncode = capture_controller.main(
                capture_command(production=False)[1:],
                testing=True,
                authority_module=authority,
            )
        self.assertEqual(returncode, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn(
            f"failed: {_SESSION_AUTHORITY}:",
            stderr.getvalue(),
        )
        self.assertEqual(stderr.getvalue().count(_SESSION_AUTHORITY), 1)
        resolve_worker.assert_not_called()
        capture.assert_not_called()

    def test_synthetic_frozen_publication_join_and_source_corruption(self) -> None:
        publisher.validate_join(
            self.raw,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertEqual(self.publication["cell_role"], "calibration")
        self.assertEqual(len(self.publication["arms"]), 2)

        foreign_capture = copy.deepcopy(self.capture)
        foreign_capture["source_commit"] = "b" * 40
        for arm in foreign_capture["arms"]:
            for worker_name in ("measured_worker", "authority_worker"):
                worker = arm[worker_name]
                worker["source_commit"] = foreign_capture["source_commit"]
                artifacts.rehash_worker(worker)
        artifacts.rehash_capture(foreign_capture)
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "source envelopes differ",
        ):
            publisher.validate_join(
                self.raw,
                foreign_capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )


if __name__ == "__main__":
    unittest.main()
