"""Process coverage for the exact-small canonical snapshot producer."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import unittest

from tools import validate_phase4_raw_evidence as raw_validator


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def raw_command() -> list[str]:
    return [
        str(runfile("phase4_evidence_runner")),
        "--testing_allow_unstamped=1",
        "--case_id=100",
        "--pool_size=4",
        "--workers=4",
        "--repetitions=20",
        "--setup_ns=60000000000",
        "--prepared_ns=60000000000",
        "--cold_ns=120000000000",
        "--address_space_bytes=68719476736",
        "--peak_host_bytes=17179869184",
    ]


def common_join_arguments(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    pair = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
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
        f"--paired_semantic_checksum={pair['semantic_checksum']}",
        f"--paired_artifact_checksum={pair['artifact_checksum']}",
        f"--baseline_semantic_checksum={baseline['semantics']['semantic_checksum']}",
        f"--baseline_arm_artifact_checksum={baseline['artifact_checksum']}",
        f"--candidate_semantic_checksum={candidate['semantics']['semantic_checksum']}",
        f"--candidate_arm_artifact_checksum={candidate['artifact_checksum']}",
    ]


def prepare_authorities() -> tuple[dict[str, object], dict[str, object], list[str]]:
    completed = subprocess.run(
        raw_command(), check=False, text=True, capture_output=True, timeout=30
    )
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    raw = json.loads(completed.stdout)
    raw["source_commit"] = "a" * 40
    raw["source_stamped"] = True
    raw["source_tree_dirty"] = False
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)
    common = common_join_arguments(raw)
    report_completed = subprocess.run(
        [str(runfile("phase4_per_net_report_test_runner")), *common],
        check=False,
        text=True,
        capture_output=True,
        timeout=30,
    )
    if report_completed.returncode != 0:
        raise AssertionError(report_completed.stderr)
    report = json.loads(report_completed.stdout)
    return raw, report, common


class Phase4ExactSmallSnapshotProcessTest(unittest.TestCase):
    def test_runner_emits_deterministic_complete_diagnostic_snapshot(self) -> None:
        raw, report, common = prepare_authorities()
        command = [
            str(runfile("phase4_exact_small_snapshot_test_runner")),
            *common,
            f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
            f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
        ]
        first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stderr, "")
        document = json.loads(first.stdout)
        self.assertFalse(document["decision_eligible"])
        self.assertEqual(document["config"]["case_id"], 100)
        self.assertEqual(len(document["workload_roster"]), 6)
        self.assertEqual(len(document["pools"]), 6)
        self.assertLessEqual(document["cartesian_product"], 4096)
        self.assertEqual(document["maximum_serialized_bytes"], 64 * 1024 * 1024)
        self.assertEqual(
            document["candidate_semantics"]["semantic_checksum"],
            document["candidate_semantic_checksum"],
        )
        self.assertEqual(
            document["raw_reference"]["candidate_semantic_checksum"],
            raw["attempts"][0]["candidate"]["record"]["semantics"]["semantic_checksum"],
        )
        for pool in document["pools"]:
            ids = [(row["id"]["high"], row["id"]["low"]) for row in pool["candidates"]]
            self.assertEqual(ids, sorted(ids))
            for candidate in pool["candidates"]:
                self.assertIn("geometry", candidate)
                self.assertIn("resource_spans", candidate)
                self.assertIn("payload_checksum", candidate)

        second = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout, first.stdout)

    def test_runner_rejects_missing_or_unknown_association_before_stdout(self) -> None:
        _, report, common = prepare_authorities()
        valid = [
            str(runfile("phase4_exact_small_snapshot_test_runner")),
            *common,
            f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
            f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
        ]
        for command in (valid[:-1], [*valid, "--unknown=1"]):
            completed = subprocess.run(
                command, check=False, text=True, capture_output=True, timeout=10
            )
            self.assertEqual(completed.returncode, 2)
            self.assertEqual(completed.stdout, "")

        bounded = subprocess.run(
            [*valid, "--testing_maximum_serialized_bytes=1"],
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(bounded.returncode, 1)
        self.assertEqual(bounded.stdout, "")
        self.assertIn("P4EXACT-SNAPSHOT-OUTPUT-BOUND-002", bounded.stderr)


if __name__ == "__main__":
    unittest.main()
