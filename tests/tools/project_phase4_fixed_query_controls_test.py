"""Adversarial tests for Phase 4 Fixed-Query Control v1."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.tools import validate_phase4_raw_evidence_test as raw_test
from tools import project_phase4_fixed_query_controls as projector
from tools import project_phase4_operational_evidence as operational_projector
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_workload_net_roster_manifest as roster_validator

_COMMIT = "a" * 40
_CASES = ((2002, 256, 4), (2003, 128, 8), (2004, 64, 16))


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _raw(
    case_id: int,
    net_count: int,
    pool_size: int,
    *,
    compiler_identity: str = "clang-test",
    setup_limit: int = 3_000,
) -> dict[str, object]:
    result = raw_test._artifact(20)
    _, cases, budgets = raw_validator._representative_manifest()
    manifest = cases[case_id]
    config = result["config"]
    config["case_id"] = case_id
    config["requested_pool_size"] = pool_size
    config["maximum_setup_elapsed_nanoseconds"] = setup_limit
    config["corpus_limits"] = {
        "maximum_nets": 4096,
        "maximum_compiled_nodes": 100_000_000,
        "maximum_compiled_host_bytes": 8 * 1024 * 1024 * 1024,
        "maximum_active_regions": 100_000,
        "maximum_board_entities": 20_000,
    }
    result["authority_run_identity"] = 10_000 + case_id
    result["controller_identity"] = 20_000 + case_id
    result["environment"]["compiler_identity"] = compiler_identity
    result["environment"]["environment_checksum"] = raw_validator.compute_environment_checksum(
        result["environment"]
    )
    root_seed = raw_validator.compute_canonical_root_seed(result)
    budget_checksum = raw_validator.compute_canonical_budget_checksum(
        result, manifest, budgets[(case_id, pool_size)]
    )
    opportunity_queries = net_count * (pool_size + 2)
    for repetition, pair in enumerate(result["attempts"]):
        pair["case_id"] = case_id
        pair["requested_pool_size"] = pool_size
        pair["root_seed"] = root_seed
        for arm_name, arm_value in (("baseline", 0), ("candidate", 1)):
            arm = pair[arm_name]
            process_identity = 30_000 + case_id * 2 + arm_value
            arm["process_instance_identity"] = process_identity
            semantics = arm["record"]["semantics"]
            semantics.update(
                {
                    "case_id": case_id,
                    "descriptor_fingerprint": manifest["descriptor_fingerprint"],
                    "case_checksum": manifest["case_checksum"],
                    "board_content_hash": manifest["board_content_hash"],
                    "workload_checksum": manifest["workload_checksum"],
                    "capacity_model_checksum": manifest["capacity_model_checksum"],
                    "budget_checksum": budget_checksum,
                    "workload_net_count": net_count,
                    "requested_pool_size": pool_size,
                    "root_seed": root_seed,
                    "baseline_sweeps": pool_size + 2,
                    "candidate_regeneration_epochs": 2,
                    "candidate_columns_per_epoch": net_count,
                    "candidate_terminal_selection_rounds": pool_size + 1,
                    "opportunity": {
                        "route_queries": opportunity_queries,
                        "route_work_units": opportunity_queries * 1_000_000_000,
                    },
                    "actual": {
                        "route_queries": net_count,
                        "route_work_units": net_count * 100,
                    },
                    "preparation_route_queries": net_count if arm_value else 0,
                    "preparation_route_work_units": net_count * 100 if arm_value else 0,
                    "regeneration_route_queries": 0,
                    "regeneration_route_work_units": 0,
                    "requested_columns": net_count,
                    "admitted_candidates": net_count,
                    "rejected_columns": 0,
                    "final_candidate_count": net_count,
                    "terminal_reason": 0,
                    "candidate_outcome_source": arm_value,
                    "outcome": {
                        "selected_net_count": net_count,
                        "no_candidate_net_count": 0,
                        "overused_resource_count": 0,
                        "total_overuse_units": 0,
                        "total_intrinsic_cost": net_count * 10,
                        "world_checksum": 40_000 + case_id * 2 + arm_value,
                    },
                }
            )
            if arm_value == 0:
                semantics["preparation_checksum"] = 0
                semantics["final_pool_manifest_checksum"] = 0
                semantics["final_rejection_manifest_checksum"] = 0
            observation = arm["record"]["external_observation"]
            observation["authority_run_identity"] = result["authority_run_identity"]
            observation["controller_identity"] = result["controller_identity"]
            observation["process_instance_identity"] = process_identity
            semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
            observation["associated_semantic_checksum"] = semantics["semantic_checksum"]
        raw_test._refresh_pair(result, repetition)
    result["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(result)
    result["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(result)
    result["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(result)
    raw_validator.validate_document(result, expected_commit=_COMMIT)
    return result


def _metrics(cost: int) -> dict[str, int]:
    return {
        "scalar_policy_cost": cost,
        "intrinsic_base_cost": cost,
        "orthogonal_step_count": 1,
        "diagonal_step_count": 0,
        "bend_count": 0,
        "line_primitive_count": 1,
        "via_count": 0,
        "axis_aligned_length_dbu": cost,
        "diagonal_projection_dbu": 0,
    }


def _telemetry(semantics: dict[str, object], nets: list[tuple[int, int]]) -> dict[str, object]:
    reports = []
    for index, (net_id, generation) in enumerate(nets):
        reports.append(
            {
                "schema_version": 1,
                "net": {"id": net_id, "generation": generation},
                "columns": {
                    "requested_columns": 1,
                    "executed_route_queries": 1,
                    "admitted_candidates": 1,
                    "duplicate_candidates": 0,
                    "disconnected_columns": 0,
                    "unsupported_columns": 0,
                    "skipped_columns": 0,
                    "exact_validation_rejections": 0,
                    "other_rejections": 0,
                },
                "final_pool_size": 1,
                "unique_geometry_signature_count": 1,
                "unique_resource_signature_count": 1,
                "candidate_pair_count": 0,
                "mean_resource_overlap_ppm": 0,
                "minimum_resource_overlap_ppm": 0,
                "mean_geometric_overlap_ppm": 0,
                "minimum_geometric_overlap_ppm": 0,
                "selected_status": 0,
                "selected_candidate_id": {"high": index + 1, "low": index + 1001},
                "selected_candidate_payload_checksum": index + 1,
                "selected_candidate_metrics": _metrics(10),
                "pool_best_intrinsic_cost": 10,
            }
        )
    result: dict[str, object] = {
        "schema_version": 1,
        "associated_semantic_checksum": semantics["semantic_checksum"],
        "per_net": reports,
        "telemetry_checksum": 0,
    }
    result["telemetry_checksum"] = report_validator.compute_telemetry_checksum(result)
    return result


def _report(raw: dict[str, object]) -> dict[str, object]:
    case_id = raw["config"]["case_id"]
    roster_row, nets = roster_validator.validated_successful_case_roster(case_id)
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    result: dict[str, object] = {
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "raw_wire_schema_version": raw["wire_schema_version"],
        "config": copy.deepcopy(raw["config"]),
        "corpus_checksum": raw["corpus_checksum"],
        "raw_cell_plan_checksum": raw["cell_plan_checksum"],
        "raw_cell_artifact_checksum": raw["artifact_checksum"],
        "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        "raw_reference": {
            "repetition_index": 0,
            "execution_order": 0,
            "pair_attempt_checksum": attempt["attempt_checksum"],
            "paired_semantic_checksum": paired["semantic_checksum"],
            "paired_artifact_checksum": paired["artifact_checksum"],
            "baseline_semantic_checksum": baseline["semantics"]["semantic_checksum"],
            "baseline_arm_artifact_checksum": baseline["artifact_checksum"],
            "candidate_semantic_checksum": candidate["semantics"]["semantic_checksum"],
            "candidate_arm_artifact_checksum": candidate["artifact_checksum"],
        },
        "decision_eligible": False,
        "workload_net_roster_checksum": roster_row["roster_checksum"],
        "arms": [
            {
                "arm": arm,
                "raw_semantic_checksum": record["semantics"]["semantic_checksum"],
                "diagnostic": {
                    "semantics": copy.deepcopy(record["semantics"]),
                    "telemetry": _telemetry(record["semantics"], list(nets)),
                },
            }
            for arm, record in ((0, baseline), (1, candidate))
        ],
        "artifact_checksum": 0,
    }
    result["artifact_checksum"] = report_validator.compute_report_artifact_checksum(result)
    result["source_envelope_checksum"] = report_validator.compute_report_source_envelope_checksum(
        result
    )
    report_validator.validate_join(raw, result, expected_commit=_COMMIT)
    return result


def _authorities() -> dict[int, tuple[dict[str, object], ...]]:
    result = {}
    for case_id, nets, pool in _CASES:
        raw = _raw(case_id, nets, pool)
        report = _report(raw)
        operational = operational_projector.project_document(raw)
        result[case_id] = (raw, report, operational)
    return result


def _reauthenticate(document: dict[str, object]) -> None:
    document["artifact_checksum"] = projector.compute_artifact_checksum(document)
    document["source_envelope_checksum"] = projector.compute_source_envelope_checksum(document)


class Phase4FixedQueryControlsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.authorities = _authorities()
        cls.artifact = projector.project_document(cls.authorities)

    def test_accepts_complete_control_and_preserves_scope(self) -> None:
        projector.validate_artifact(self.authorities, self.artifact, expected_commit=_COMMIT)
        self.assertFalse(self.artifact["standalone_decision_eligible"])
        self.assertFalse(self.artifact["phase4_complete"])
        self.assertTrue(self.artifact["fixed_query_coverage_complete"])
        definition = self.artifact["control_definition"]
        self.assertEqual(definition["fixed_route_query_opportunity"], 1024)
        self.assertFalse(definition["whole_trial_equal_across_shapes"])
        self.assertEqual(
            [row["case_id"] for row in self.artifact["controls"]],
            [2000, 2001, 2002, 2003, 2004],
        )
        self.assertEqual(
            [
                row["diversity_accounting"]["requested_pool_pair_opportunity"]
                for row in self.artifact["controls"]
            ],
            [523776, 0, 1536, 3584, 7680],
        )
        self.assertEqual(
            [
                row["whole_trial"]["route_query_opportunity"]
                for row in self.artifact["controls"][2:]
            ],
            [1536, 1280, 1152],
        )
        for row in self.artifact["controls"][:2]:
            self.assertEqual(row["evidence_status"], "descriptor_only_excluded")
            self.assertNotIn("whole_trial", row)
            self.assertNotIn("actual_route_queries", row["initial_candidate_preparation"])
            self.assertEqual(row["execution_measurement"]["status"], "unavailable")

    def test_rejects_rehashed_output_drift_in_every_authority_layer(self) -> None:
        mutations = (
            lambda value: value["controls"][2]["source_binding"].__setitem__(
                "raw_artifact_checksum", 1
            ),
            lambda value: value["controls"][3]["source_binding"].__setitem__(
                "report_artifact_checksum", 1
            ),
            lambda value: value["controls"][4]["source_binding"].__setitem__(
                "operational_artifact_checksum", 1
            ),
            lambda value: value["controls"][2]["initial_candidate_preparation"].__setitem__(
                "actual_route_queries", 1025
            ),
            lambda value: value["controls"][3]["diversity_accounting"].__setitem__(
                "cross_net_candidate_pairs_included", True
            ),
            lambda value: value["controls"][4]["whole_trial"].__setitem__(
                "route_query_opportunity", 1024
            ),
            lambda value: value["controls"][0].__setitem__("evidence_status", "success"),
            lambda value: value["control_definition"].__setitem__(
                "whole_trial_equal_across_shapes", True
            ),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(self.artifact)
                mutate(changed)
                _reauthenticate(changed)
                with self.assertRaises(projector.ControlError):
                    projector.validate_artifact(self.authorities, changed, expected_commit=_COMMIT)

    def test_rejects_cross_case_substitution_and_environment_or_cap_drift(self) -> None:
        substituted = dict(self.authorities)
        substituted[2003] = self.authorities[2002]
        with self.assertRaisesRegex(projector.ControlError, "map key"):
            projector.validate_artifact(substituted, self.artifact, expected_commit=_COMMIT)

        for field, raw in (
            (
                "environment",
                _raw(2003, 128, 8, compiler_identity="different compiler"),
            ),
            ("caps", _raw(2003, 128, 8, setup_limit=2001)),
        ):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.authorities)
                report = _report(raw)
                operational = operational_projector.project_document(raw)
                changed[2003] = (raw, report, operational)
                with self.assertRaisesRegex(projector.ControlError, "share source"):
                    projector.validate_artifact(changed, self.artifact, expected_commit=_COMMIT)

    def test_strict_reader_rejects_noncanonical_or_bounded_input(self) -> None:
        canonical = _canonical(self.artifact)
        malformed = {
            "duplicate": canonical.replace(
                '{"schema_version":1', '{"schema_version":1,"schema_version":1', 1
            ),
            "bool": canonical.replace('"case_id":2000', '"case_id":true', 1),
            "float": canonical.replace('"case_id":2000', '"case_id":2000.0', 1),
            "nan": canonical.replace('"case_id":2000', '"case_id":NaN', 1),
            "bom": "\ufeff" + canonical,
            "missing_lf": canonical[:-1],
            "extra_lf": canonical + "\n",
        }
        reordered = copy.deepcopy(self.artifact)
        reordered["control_definition"] = {
            key: reordered["control_definition"][key]
            for key in reversed(tuple(reordered["control_definition"].keys()))
        }
        malformed["order"] = _canonical(reordered)
        nested: object = 0
        for _ in range(70):
            nested = [nested]
        malformed["nesting"] = _canonical(nested)
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            for name, text in malformed.items():
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    path.write_text(text, encoding="utf-8")
                    with self.assertRaises(projector.ControlError):
                        value = projector.read_artifact(path)
                        projector.validate_artifact(
                            self.authorities, value, expected_commit=_COMMIT
                        )
            oversized = root / "oversized.json"
            oversized.write_bytes(b" " * (projector._MAXIMUM_ARTIFACT_BYTES + 1))
            with self.assertRaisesRegex(projector.ControlError, "input bound"):
                projector.read_artifact(oversized)

    def test_cli_projects_deterministically_and_validates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            command = [
                str(_runfile("phase4_fixed_query_controls")),
                "--expected-commit",
                _COMMIT,
            ]
            for case_id, _, _ in _CASES:
                raw, report, operational = self.authorities[case_id]
                for kind, document in (
                    ("raw", raw),
                    ("report", report),
                    ("operational", operational),
                ):
                    path = root / f"{kind}-{case_id}.json"
                    path.write_text(_canonical(document), encoding="utf-8")
                    command.extend((f"--{kind}-{case_id}", str(path)))
            first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
            second = subprocess.run(
                command, check=False, text=True, capture_output=True, timeout=30
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stderr, "")
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(first.stdout, _canonical(self.artifact))
            artifact_path = root / "fixed-query.json"
            artifact_path.write_text(first.stdout, encoding="utf-8")
            validated = subprocess.run(
                [*command, "--validate", str(artifact_path)],
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(validated.returncode, 0, validated.stderr)
            self.assertIn("validated one", validated.stdout)


if __name__ == "__main__":
    unittest.main()
