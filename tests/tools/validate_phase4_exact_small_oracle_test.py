"""Tests for the strict Phase 4 exact-small publication oracle."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import time
import unittest

from tests.support.phase4_current_diagnostic_budget import patch_live_diagnostic_budgets
from tools import validate_phase4_exact_small_oracle as oracle
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_COMMIT = "a" * 40


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


def prepare(case_id: int) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    raw_completed = subprocess.run(
        raw_command(case_id), check=False, text=True, capture_output=True, timeout=30
    )
    if raw_completed.returncode != 0:
        raise AssertionError(raw_completed.stderr)
    raw = json.loads(raw_completed.stdout)
    raw["source_commit"] = _COMMIT
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
    snapshot_completed = subprocess.run(
        [
            str(runfile("phase4_exact_small_snapshot_test_runner")),
            *common,
            f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
            f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=30,
    )
    if snapshot_completed.returncode != 0:
        raise AssertionError(snapshot_completed.stderr)
    return raw, report, json.loads(snapshot_completed.stdout)


def reauthenticate(snapshot: dict[str, object]) -> None:
    snapshot["artifact_checksum"] = oracle.compute_snapshot_artifact_checksum(snapshot)
    snapshot["source_envelope_checksum"] = oracle.compute_snapshot_source_envelope(snapshot)


def candidate(identity: int, cost: int, resources: list[dict[str, int]]) -> dict[str, object]:
    return {
        "id": {"high": 0, "low": identity},
        "metrics": {"intrinsic_base_cost": cost, "scalar_policy_cost": 10_000 - cost},
        "resource_spans": resources,
    }


class Phase4ExactSmallOracleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.budget_patcher = patch_live_diagnostic_budgets(raw_validator, runfile)
        cls.documents = {case_id: prepare(case_id) for case_id in (100, 101, 102)}

    @classmethod
    def tearDownClass(cls) -> None:
        cls.budget_patcher.stop()

    def test_all_exact_cases_join_and_production_is_optimal(self) -> None:
        for case_id, (raw, report, snapshot) in self.documents.items():
            with self.subTest(case_id=case_id):
                artifact = oracle.validate_publication(
                    raw, report, snapshot, expected_commit=_COMMIT
                )
                self.assertEqual(artifact["case_id"], case_id)
                self.assertTrue(artifact["production_is_optimal"])
                self.assertGreaterEqual(artifact["optimum_count"], 1)
                self.assertEqual(artifact["production_objective"], artifact["optimum_objective"])
                self.assertEqual(len(artifact["canonical_witness"]), 6)
                self.assertLessEqual(artifact["cartesian_product"], 4096)
                serialized = oracle.serialize_oracle_artifact(artifact)
                self.assertEqual(serialized, json.dumps(artifact, separators=(",", ":")) + "\n")

    def test_join_layers_and_authenticated_snapshot_corruption_fail(self) -> None:
        raw, report, original = self.documents[100]
        mutations = (
            ("raw", lambda value: value.__setitem__("raw_cell_artifact_checksum", 1)),
            (
                "report",
                lambda value: value.__setitem__("per_net_report_artifact_checksum", 1),
            ),
            (
                "semantics",
                lambda value: value.__setitem__("candidate_semantic_checksum", 1),
            ),
            (
                "telemetry",
                lambda value: value.__setitem__("per_net_candidate_telemetry_checksum", 1),
            ),
        )
        for name, mutate in mutations:
            with self.subTest(name=name):
                changed = copy.deepcopy(original)
                mutate(changed)
                reauthenticate(changed)
                with self.assertRaises(oracle.EvidenceError):
                    oracle.validate_publication(raw, report, changed, expected_commit=_COMMIT)

    def test_complete_per_net_selection_join_rejects_reauthenticated_wrong_id(self) -> None:
        raw, original_report, original_snapshot = self.documents[100]
        report = copy.deepcopy(original_report)
        snapshot = copy.deepcopy(original_snapshot)
        telemetry = report["arms"][1]["diagnostic"]["telemetry"]
        row = next(item for item in telemetry["per_net"] if item["selected_status"] == 0)
        row["selected_candidate_id"] = {"high": _COMMIT.count("a"), "low": 999_999}
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(report)
        report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(report)
        )
        snapshot["per_net_report_artifact_checksum"] = report["artifact_checksum"]
        snapshot["per_net_report_source_envelope_checksum"] = report["source_envelope_checksum"]
        snapshot["per_net_candidate_telemetry_checksum"] = telemetry["telemetry_checksum"]
        reauthenticate(snapshot)
        with self.assertRaisesRegex(oracle.EvidenceError, "selection differs"):
            oracle.validate_publication(raw, report, snapshot, expected_commit=_COMMIT)

    def test_candidate_manifest_capacity_and_artifact_tamper_fail(self) -> None:
        raw, report, original = self.documents[100]
        mutations = (
            lambda value: value["pools"][0]["candidates"][0]["metrics"].__setitem__(
                "scalar_policy_cost", 1
            ),
            lambda value: value["pools"][0]["candidates"][0].__setitem__("payload_checksum", 1),
            lambda value: value.__setitem__("final_pool_manifest_checksum", 1),
            lambda value: value.__setitem__("capacity_model_checksum", 1),
        )
        for mutate in mutations:
            changed = copy.deepcopy(original)
            mutate(changed)
            reauthenticate(changed)
            with self.assertRaises(oracle.EvidenceError):
                oracle.validate_publication(raw, report, changed, expected_commit=_COMMIT)

        changed = copy.deepcopy(original)
        changed["artifact_checksum"] ^= 1
        with self.assertRaises(oracle.EvidenceError):
            oracle.validate_publication(raw, report, changed, expected_commit=_COMMIT)

    def test_oracle_uses_unweighted_cost_and_canonical_equal_optimum_witness(self) -> None:
        expensive_base = candidate(2, 90, [])
        cheap_base = candidate(1, 10, [])
        snapshot = {
            "capacity": {"default_capacity_units": 1, "overrides": []},
            "pools": [
                {"candidates": [expensive_base, cheap_base]},
                *({"candidates": []} for _ in range(5)),
            ],
        }
        optimum, count, witness = oracle.enumerate_exact_oracle(snapshot)
        self.assertEqual(optimum, (1, 0, 10))
        self.assertEqual(count, 1)
        self.assertEqual(witness[0]["id"]["low"], 1)

        tied = copy.deepcopy(snapshot)
        tied["pools"][0]["candidates"][0]["metrics"]["intrinsic_base_cost"] = 10
        optimum, count, witness = oracle.enumerate_exact_oracle(tied)
        self.assertEqual(optimum, (1, 0, 10))
        self.assertEqual(count, 2)
        self.assertEqual(witness[0]["id"]["low"], 1)

        empty = {"capacity": snapshot["capacity"], "pools": [{"candidates": []}] * 6}
        optimum, count, witness = oracle.enumerate_exact_oracle(empty)
        self.assertEqual(optimum, (0, 0, 0))
        self.assertEqual(count, 1)
        self.assertEqual(witness, (None,) * 6)

    def test_overused_resource_count_is_diagnostic_and_never_ranks_worlds(self) -> None:
        first_resource = [
            {
                "layer": 0,
                "lattice_x": 0,
                "lattice_y": 0,
                "direction": 0,
                "edge_count": 1,
                "usage_units": 1,
            }
        ]
        second_resource = [
            {
                "layer": 0,
                "lattice_x": 2,
                "lattice_y": 0,
                "direction": 0,
                "edge_count": 1,
                "usage_units": 1,
            }
        ]
        two_resources = first_resource + second_resource
        distinct, distinct_count = oracle._score(
            (candidate(1, 5, two_resources), candidate(2, 5, [])), 0, {}
        )
        shared, shared_count = oracle._score(
            (candidate(3, 5, first_resource), candidate(4, 5, first_resource)), 0, {}
        )
        self.assertEqual(distinct, shared)
        self.assertEqual(distinct, (2, 2, 10))
        self.assertEqual((distinct_count, shared_count), (2, 1))

    def test_product_boundary_and_overflow_precede_poison_candidate(self) -> None:
        _, _, original = self.documents[100]
        self.assertEqual(oracle.preflight_cartesian_product([4] * 6), 4096)
        self.assertEqual(oracle.preflight_cartesian_product([0] * 6), 1)
        with self.assertRaises(oracle.EvidenceError):
            oracle.preflight_cartesian_product([])
        with self.assertRaises(oracle.EvidenceError):
            oracle.preflight_cartesian_product([4097, 2**64 - 1, 1, 1, 1, 1])

        changed = copy.deepcopy(original)
        poison = next(pool["candidates"][0] for pool in changed["pools"] if pool["candidates"])
        for pool in changed["pools"][:-1]:
            pool["candidates"] = [copy.deepcopy(poison) for _ in range(4)]
        changed["pools"][-1]["candidates"] = [copy.deepcopy(poison) for _ in range(5)]
        with self.assertRaisesRegex(oracle.EvidenceError, "Cartesian product"):
            oracle._parse_snapshot(changed)

    def test_shape_preflight_boundaries_are_fast_and_precede_expansion(self) -> None:
        _, _, original = self.documents[100]
        template = copy.deepcopy(
            next(pool["candidates"][0] for pool in original["pools"] if pool["candidates"])
        )
        template["geometry"] = [
            {
                "kind": "line",
                "layer": 0,
                "start": {"x": 0, "y": 0},
                "end": {"x": 100_000, "y": 0},
            }
        ]
        template["resource_spans"] = [
            {
                "layer": 0,
                "lattice_x": 0,
                "lattice_y": 0,
                "direction": 0,
                "edge_count": 100_000,
                "usage_units": 1,
            }
        ]
        started = time.monotonic()
        totals = oracle._preflight_candidate_shapes([[template], [], [], [], [], []])
        self.assertEqual(totals["expanded_edges"], 100_000)
        self.assertEqual(totals["geometry_steps"], 100_000)
        self.assertLess(time.monotonic() - started, 1.0)

        for edge_count in (100_001, 2**32 - 1):
            with self.subTest(edge_count=edge_count):
                poison = copy.deepcopy(template)
                poison["resource_spans"][0]["edge_count"] = edge_count
                started = time.monotonic()
                with self.assertRaisesRegex(oracle.EvidenceError, "expanded resource edges"):
                    oracle._preflight_candidate_shapes([[poison], [], [], [], [], []])
                self.assertLess(time.monotonic() - started, 1.0)

        poison = copy.deepcopy(template)
        poison["geometry"][0]["start"]["x"] = -(2**63)
        poison["geometry"][0]["end"]["x"] = 2**63 - 1
        started = time.monotonic()
        with self.assertRaisesRegex(oracle.EvidenceError, "geometry steps"):
            oracle._preflight_candidate_shapes([[poison], [], [], [], [], []])
        self.assertLess(time.monotonic() - started, 1.0)

    def test_exact_candidate_admission_replay_rejects_context_and_geometry_mutations(self) -> None:
        _, _, original = self.documents[100]
        mutations = (
            lambda candidate: candidate["intended_terminals"][0].__setitem__(
                "id", candidate["intended_terminals"][0]["id"] + 1
            ),
            lambda candidate: candidate["geometry"][0]["end"].__setitem__(
                "x", candidate["geometry"][0]["end"]["x"] + 1
            ),
            lambda candidate: candidate["geometry"][0].__setitem__("layer", 1),
            lambda candidate: candidate["associations"].__setitem__(
                "geometry_compiler_version",
                candidate["associations"]["geometry_compiler_version"] + 1,
            ),
            lambda candidate: candidate["associations"].__setitem__(
                "rule_bucket_identity", candidate["associations"]["rule_bucket_identity"] + 1
            ),
        )
        for mutate in mutations:
            changed = copy.deepcopy(original)
            candidate = next(
                pool["candidates"][0] for pool in changed["pools"] if pool["candidates"]
            )
            mutate(candidate)
            with self.assertRaisesRegex(oracle.EvidenceError, "admission replay failed"):
                oracle._replay_exact_candidate_admission(changed)

        payload = oracle._candidate_admission_payload(original)
        runner = runfile("phase4_exact_small_candidate_admission_replay")
        for poison in (b"X" + payload[1:], payload + b"trailing"):
            completed = subprocess.run(
                [str(runner)], input=poison, check=False, capture_output=True, timeout=5
            )
            self.assertNotEqual(completed.returncode, 0)
            self.assertEqual(completed.stdout, b"")
            self.assertLessEqual(len(completed.stderr), 1024)

    def test_cli_has_whole_document_output_and_empty_stdout_on_input_failures(self) -> None:
        raw, report, snapshot = self.documents[100]
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            paths = {
                "raw": root / "raw.json",
                "report": root / "report.json",
                "snapshot": root / "snapshot.json",
            }
            for name, document in (("raw", raw), ("report", report), ("snapshot", snapshot)):
                paths[name].write_text(
                    json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8"
                )
            command = [
                str(runfile("phase4_current_v1_diagnostic_cli")),
                "--testing-tool=exact-small-oracle",
                f"--expected-commit={_COMMIT}",
                f"--raw={paths['raw']}",
                f"--report={paths['report']}",
                f"--snapshot={paths['snapshot']}",
            ]
            valid = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            self.assertEqual(valid.stderr, "")
            self.assertTrue(json.loads(valid.stdout)["production_is_optimal"])

            authority_command = [
                str(runfile("phase4_exact_small_oracle_validator")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(paths["raw"]),
                "--report",
                str(paths["report"]),
                "--snapshot",
                str(paths["snapshot"]),
            ]
            authority = subprocess.run(
                authority_command,
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(authority.returncode, 1)
            self.assertEqual(authority.stdout, "")
            self.assertIn("associated with another command or cell", authority.stderr)
            self.assertNotIn("compiled launcher", authority.stderr)

            help_result = subprocess.run(
                [authority_command[0], "--help"],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(help_result.returncode, 0, help_result.stderr)
            self.assertIn("--expected-commit", help_result.stdout)

            direct_inner = list(authority_command)
            direct_inner[0] = str(runfile("phase4_exact_small_oracle_validator_py"))
            rejected_inner = subprocess.run(
                direct_inner,
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(rejected_inner.returncode, 2)
            self.assertEqual(rejected_inner.stdout, "")
            self.assertIn("requires its compiled launcher", rejected_inner.stderr)

            failures = (
                b"{\n",
                b'{"source_commit":"a","source_commit":"b"}\n',
                (b'{"x":' * 66) + b"0" + (b"}" * 66) + b"\n",
                b" " * (64 * 1024 * 1024 + 1),
            )
            for encoded in failures:
                paths["snapshot"].write_bytes(encoded)
                failed = subprocess.run(
                    command, check=False, text=True, capture_output=True, timeout=30
                )
                self.assertEqual(failed.returncode, 1)
                self.assertEqual(failed.stdout, "")


if __name__ == "__main__":
    unittest.main()
