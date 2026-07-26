"""Acquisition-free process tests for the H4096 ordinary per-net join."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_h4096_per_net_report as h4096_report
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def report_command(raw: dict[str, object], executable: str) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(runfile(executable)),
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


class Phase4ConfirmatoryH4096PerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        cls.raw = artifacts.make_raw(10200, 8, same_run=False, h4096=True, repetitions=20)
        artifacts.mark_raw_clean(cls.raw)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = root / "raw.json"
        cls.report_path = root / "report.json"
        cls.raw_path.write_text(
            json.dumps(cls.raw, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        cls.report_path.write_text(
            json.dumps(cls.report, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

        cls.h2250_raw = artifacts.make_raw(10200, 8, same_run=False, h4096=False, repetitions=20)
        artifacts.mark_raw_clean(cls.h2250_raw)
        cls.h2250_raw_path = root / "h2250-raw.json"
        cls.h2250_raw_path.write_text(
            json.dumps(cls.h2250_raw, ensure_ascii=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_synthetic_report_strictly_joins_and_has_only_diagnostic_content(self) -> None:
        h4096_report.validate_join(
            self.raw,
            self.report,
            expected_commit=self.raw["source_commit"],
        )
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 1)
        self.assertEqual(len(self.report["arms"]), 2)
        for arm in self.report["arms"]:
            self.assertEqual(len(arm["diagnostic"]["telemetry"]["per_net"]), 64)
        encoded = json.dumps(self.report, ensure_ascii=False, separators=(",", ":"))
        for forbidden in (
            "case_build_elapsed_nanoseconds",
            "prepared_elapsed_nanoseconds",
            "cold_elapsed_nanoseconds",
            "outer_elapsed_nanoseconds",
            "process_lifetime_peak_host_bytes",
        ):
            self.assertNotIn(f'"{forbidden}"', encoded)

    def test_report_join_rejects_rehashed_foreign_raw_and_semantics(self) -> None:
        foreign_reference = copy.deepcopy(self.report)
        foreign_reference["raw_reference"]["pair_attempt_checksum"] ^= 1
        foreign_reference["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_reference
        )
        foreign_reference["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_reference)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "raw reference"):
            h4096_report.validate_join(
                self.raw,
                foreign_reference,
                expected_commit=self.raw["source_commit"],
            )

        foreign_semantics = copy.deepcopy(self.report)
        semantics = foreign_semantics["arms"][1]["diagnostic"]["semantics"]
        semantics["budget_checksum"] ^= 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        arm = foreign_semantics["arms"][1]
        arm["raw_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry = arm["diagnostic"]["telemetry"]
        telemetry["associated_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        foreign_semantics["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_semantics
        )
        foreign_semantics["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_semantics)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "complete semantics differ"):
            h4096_report.validate_join(
                self.raw,
                foreign_semantics,
                expected_commit=self.raw["source_commit"],
            )

    def test_named_validator_accepts_join_and_authenticates_raw_before_report(self) -> None:
        base = [
            str(runfile("phase4_confirmatory_h4096_per_net_report_validator")),
            f"--expected-commit={self.raw['source_commit']}",
            f"--raw={self.raw_path}",
            f"--report={self.report_path}",
        ]
        accepted = subprocess.run(
            base,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        fifo = pathlib.Path(self.temporary.name) / "report.fifo"
        os.mkfifo(fifo)
        fifo_result = subprocess.run(
            [*base[:3], f"--report={fifo}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(fifo_result.returncode, 1)
        self.assertIn("regular file", fifo_result.stderr)

        h2250_first = subprocess.run(
            [
                base[0],
                base[1],
                f"--raw={self.h2250_raw_path}",
                f"--report={fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(h2250_first.returncode, 1)
        self.assertIn("another command or cell", h2250_first.stderr)
        self.assertNotIn("regular file", h2250_first.stderr)

        old_authority = subprocess.run(
            [
                str(runfile("phase4_confirmatory_per_net_report_validator")),
                base[1],
                f"--raw={self.raw_path}",
                f"--report={fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(old_authority.returncode, 1)
        self.assertNotIn("regular file", old_authority.stderr)

    def test_test_runner_is_permanently_preflight_only_and_scope_is_fixed(self) -> None:
        valid = report_command(
            self.raw,
            "phase4_confirmatory_h4096_per_net_report_test_runner",
        )
        completed = subprocess.run(
            valid,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("preflight-only", completed.stderr)
        self.assertEqual(completed.stdout, "")

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in valid
            ]

        mutations = (
            replace("corpus_version", "1"),
            replace("raw_wire_schema_version", "2"),
            replace("pool_size", "4"),
            replace("pool_size", "16"),
            replace("case_id", "10100"),
            replace("case_id", "10201"),
            replace("case_id", "11000"),
            [argument for argument in valid if not argument.startswith("--corpus_version=")],
            valid + [valid[-1]],
            valid + ["--fixture_path=/tmp/forbidden"],
        )
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                rejected = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(rejected.returncode, 2, rejected.stderr)
                self.assertNotIn("preflight-only", rejected.stderr)
                self.assertEqual(rejected.stdout, "")

    def test_forced_unpublishable_runner_rejects_before_any_diagnostic_output(self) -> None:
        command = report_command(
            self.raw,
            "phase4_confirmatory_h4096_per_net_report_unpublishable_source_test_runner",
        )
        command = [argument for argument in command if argument != "--testing_allow_unstamped=1"]
        dirty_raw = copy.deepcopy(self.raw)
        dirty_raw["source_stamped"] = False
        dirty_raw["source_tree_dirty"] = True
        dirty_raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            dirty_raw
        )
        prefix = "--raw_source_envelope_checksum="
        command = [
            f"{prefix}{dirty_raw['source_envelope_checksum']}"
            if argument.startswith(prefix)
            else argument
            for argument in command
        ]
        rejected = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertEqual(rejected.stdout, "")


if __name__ == "__main__":
    unittest.main()
