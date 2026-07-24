"""Cross-language process tests for the Phase 4 diagnostic report runner."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import unittest

from tests.support.phase4_current_diagnostic_budget import patch_live_diagnostic_budgets
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def raw_command(case_id: int) -> list[str]:
    return [
        str(runfile("phase4_evidence_runner")),
        "--testing_allow_unstamped=1",
        f"--case_id={case_id}",
        "--pool_size=4",
        "--workers=4",
        "--repetitions=20",
        "--setup_ns=60000000000",
        "--prepared_ns=60000000000",
        "--cold_ns=120000000000",
        "--address_space_bytes=68719476736",
        "--peak_host_bytes=17179869184",
    ]


def normalize_test_raw(document: dict[str, object]) -> None:
    # The report core intentionally accepts only clean-stamped artifacts. The
    # separately compiled test runner marks its synthetic source envelope
    # clean; normalize the raw test artifact to that same explicitly test-only
    # envelope before exercising the publication validator.
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
        str(runfile("phase4_per_net_report_test_runner")),
        "--testing_allow_unstamped=1",
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


def run_raw(case_id: int) -> dict[str, object]:
    completed = subprocess.run(
        raw_command(case_id), check=False, text=True, capture_output=True, timeout=30
    )
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    document = json.loads(completed.stdout)
    normalize_test_raw(document)
    return document


class Phase4PerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.budget_patcher = patch_live_diagnostic_budgets(raw_validator, runfile)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.budget_patcher.stop()

    def run_join(self, case_id: int) -> tuple[dict[str, object], str]:
        raw = run_raw(case_id)
        command = report_command(raw)
        first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stderr, "")
        self.assertTrue(first.stdout.endswith("\n"))
        report = json.loads(first.stdout)
        report_validator.validate_join(raw, report, expected_commit=raw["source_commit"])
        return raw, first.stdout

    def test_real_cpp_report_is_deterministic_and_strictly_joins_raw(self) -> None:
        raw, first = self.run_join(100)
        second = subprocess.run(
            report_command(raw), check=False, text=True, capture_output=True, timeout=30
        )
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout, first)
        for measured_field in (
            '"case_build_elapsed_nanoseconds"',
            '"prepared_elapsed_nanoseconds"',
            '"cold_elapsed_nanoseconds"',
            '"outer_elapsed_nanoseconds"',
            '"process_lifetime_peak_host_bytes"',
        ):
            self.assertNotIn(measured_field, first)
        self.assertIn('"decision_eligible":false', first)

    def test_imported_fixture_report_uses_authentic_frozen_roster(self) -> None:
        _, encoded = self.run_join(4000)
        report = json.loads(encoded)
        nets = [
            (entry["net"]["id"], entry["net"]["generation"])
            for entry in report["arms"][0]["diagnostic"]["telemetry"]["per_net"]
        ]
        self.assertEqual(
            nets,
            [(3033425279953999715, 0), (3033426379465627926, 0)],
        )

    def test_cli_rejects_missing_duplicate_unknown_zero_and_oversized_before_stdout(self) -> None:
        raw = run_raw(100)
        valid = report_command(raw)
        mutations = [
            valid[:-1],
            valid + [valid[-1]],
            valid + ["--unknown=1"],
            [
                "--pair_attempt_checksum=0" if arg.startswith("--pair_attempt_checksum=") else arg
                for arg in valid
            ],
            valid + ["--" + "x" * 130 + "=1"],
        ]
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                completed = subprocess.run(
                    command, check=False, text=True, capture_output=True, timeout=10
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")

        production = [
            str(runfile("phase4_per_net_report_runner")),
            *valid[1:],
        ]
        completed = subprocess.run(
            production, check=False, text=True, capture_output=True, timeout=10
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")

    def test_noncanonical_config_and_excluded_cases_fail_before_diagnostics(self) -> None:
        raw = run_raw(100)
        mutations = (
            ("workers", lambda config: config.__setitem__("preparation_worker_count", 3)),
            ("repetitions", lambda config: config.__setitem__("repetitions", 19)),
            ("excluded_stress", lambda config: config.__setitem__("case_id", 3001)),
        )
        for label, mutate in mutations:
            with self.subTest(label=label):
                changed = json.loads(json.dumps(raw))
                mutate(changed["config"])
                changed["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(changed)
                completed = subprocess.run(
                    report_command(changed),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")
                self.assertIn("requires a complete canonical cell", completed.stderr)


if __name__ == "__main__":
    unittest.main()
