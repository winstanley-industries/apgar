"""Acquisition-free process firewall for H4096 same-run operations."""

from __future__ import annotations

import copy
import os
import pathlib
import resource
import subprocess
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tests.support import phase4_confirmatory_operational_test_artifacts as operational_artifacts
from tools import phase4_confirmatory_h4096_same_run_operational_authority as authority
from tools import (
    validate_phase4_confirmatory_h4096_same_run_operational_measurement as publisher,
)
from tools import validate_phase4_raw_evidence as raw_validator

_COMMIT = "a" * 40
_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def capture_command(*, production: bool) -> list[str]:
    executable = (
        "phase4_confirmatory_h4096_same_run_operational_capture"
        if production
        else "phase4_confirmatory_h4096_same_run_operational_capture_test"
    )
    command = [
        str(runfile(executable)),
        "--corpus-version=2",
        "--raw-wire-schema-version=2",
        "--case-id=10100",
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
        "phase4_confirmatory_h4096_same_run_operational_replay_worker"
        if production
        else "phase4_confirmatory_h4096_same_run_operational_replay_test_worker"
    )
    command = [
        str(runfile(executable)),
        "--corpus_version=2",
        "--raw_wire_schema_version=2",
        f"--mode={mode}",
        f"--arm={arm}",
        "--case_id=10100",
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


class Phase4ConfirmatoryH4096SameRunOperationalMeasurementProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw, cls.sidecar, cls.capture = operational_artifacts.make_operational_inputs(
            case_id=10_100,
            pool=4,
            same_run=True,
            h4096=True,
            authority_module=authority,
            commit=_COMMIT,
        )
        if cls.sidecar is None:
            raise AssertionError("same-run operational input is missing telemetry")
        cls.publication = publisher._project_validated_document(
            cls.raw,
            cls.sidecar,
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

    def test_frozen_raw_telemetry_validation_and_corruption_remain_acquisition_free(
        self,
    ) -> None:
        raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=1,
        )
        sidecar = artifacts.make_sidecar(raw)
        publisher.h4096_telemetry.validate_confirmatory_h4096_join(
            raw,
            sidecar,
            allow_unstamped=True,
            expected_repetitions=1,
        )
        corrupt = copy.deepcopy(sidecar)
        corrupt["artifact_checksum"] ^= 1
        with self.assertRaises(raw_validator.EvidenceError):
            publisher.h4096_telemetry.validate_confirmatory_h4096_join(
                raw,
                corrupt,
                allow_unstamped=True,
                expected_repetitions=1,
            )

    def test_synthetic_frozen_publication_join_and_domain_corruption(self) -> None:
        publisher.validate_join(
            self.raw,
            self.sidecar,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertEqual(self.publication["cell_role"], "exact")
        self.assertEqual(len(self.publication["arms"]), 2)

        foreign_publication = copy.deepcopy(self.publication)
        foreign_publication["raw_authority_binding"]["authority"] = (
            "phase4_confirmatory_same_run_raw_evidence_v1"
        )
        foreign_publication["artifact_checksum"] = publisher._publication_checksum(
            foreign_publication
        )
        foreign_publication["source_envelope_checksum"] = publisher._publication_source_checksum(
            foreign_publication
        )
        with self.assertRaisesRegex(
            publisher.projection_validator.ProjectionError,
            "differs from the fully validated",
        ):
            publisher.validate_join(
                self.raw,
                self.sidecar,
                self.capture,
                foreign_publication,
                expected_commit=_COMMIT,
                testing=True,
            )


if __name__ == "__main__":
    unittest.main()
