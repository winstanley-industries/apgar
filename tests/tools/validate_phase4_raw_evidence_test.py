"""Adversarial tests for the Phase 4 raw-cell evidence validator."""

from __future__ import annotations

import copy
import json
import pathlib
import tempfile
import unittest

from tools import validate_phase4_raw_evidence as validator

_EXPECTED_COMMIT = "a" * 40
_CORPUS_CHECKSUM = 7311872938254494931
_CASE_100 = {
    "descriptor_fingerprint": 8019315640326555851,
    "case_checksum": 17177310953492740304,
    "board_content_hash": 13282114147341482240,
    "workload_checksum": 5538372392994751246,
    "capacity_model_checksum": 17239755398713837267,
}


def _validate_document(value: object, **kwargs: object) -> None:
    if not kwargs.get("allow_unstamped", False):
        kwargs.setdefault("expected_commit", _EXPECTED_COMMIT)
    validator.validate_document(value, **kwargs)


def _lifecycle(repetition: int, candidate: bool) -> dict[str, int]:
    if not candidate:
        return {
            "workers_started_before": 0,
            "workers_started_after": 0,
            "invocations_started_before": 0,
            "invocations_started_after": 0,
            "invocations_completed_before": 0,
            "invocations_completed_after": 0,
        }
    return {
        "workers_started_before": 4,
        "workers_started_after": 4,
        "invocations_started_before": repetition + 1,
        "invocations_started_after": repetition + 2,
        "invocations_completed_before": repetition + 1,
        "invocations_completed_after": repetition + 2,
    }


def _budget() -> dict[str, int]:
    return {
        "maximum_prepared_elapsed_nanoseconds": 1_000,
        "maximum_cold_elapsed_nanoseconds": 2_000,
        "maximum_address_space_bytes": 1 << 34,
        "maximum_peak_host_bytes": 1 << 30,
    }


def _semantics(repetition: int, arm: int, order: int) -> dict[str, object]:
    candidate = arm == 1
    result: dict[str, object] = {
        "schema_version": 1,
        "arm": arm,
        "execution_order": order,
        "corpus_version": 1,
        "corpus_checksum": _CORPUS_CHECKSUM,
        "case_id": 100,
        "descriptor_fingerprint": _CASE_100["descriptor_fingerprint"],
        "case_checksum": _CASE_100["case_checksum"],
        "board_content_hash": _CASE_100["board_content_hash"],
        "workload_checksum": _CASE_100["workload_checksum"],
        "capacity_model_checksum": _CASE_100["capacity_model_checksum"],
        "budget_checksum": 777,
        "workload_net_count": 6,
        "requested_pool_size": 4,
        "repetition_index": repetition,
        "root_seed": 0,
        "preparation_worker_count": 4,
        "baseline_sweeps": 6,
        "candidate_regeneration_epochs": 2,
        "candidate_columns_per_epoch": 6,
        "candidate_terminal_selection_rounds": 5,
        "external_budget": _budget(),
        "opportunity": {"route_queries": 36, "route_work_units": 36_000_000_000},
        "actual": {"route_queries": 10, "route_work_units": 100},
        "preparation_route_queries": 8 if candidate else 0,
        "preparation_route_work_units": 80 if candidate else 0,
        "regeneration_route_queries": 2 if candidate else 0,
        "regeneration_route_work_units": 20 if candidate else 0,
        "requested_columns": 10,
        "admitted_candidates": 8 if candidate else 6,
        "rejected_columns": 2 if candidate else 4,
        "final_candidate_count": 8 if candidate else 6,
        "preparation_checksum": 901 if candidate else 0,
        "algorithm_session_checksum": 902,
        "final_pool_manifest_checksum": 903,
        "final_rejection_manifest_checksum": 904,
        "terminal_reason": 0,
        "candidate_outcome_source": 1 if candidate else 0,
        "outcome": {
            "selected_net_count": 6,
            "no_candidate_net_count": 0,
            "overused_resource_count": 0,
            "total_overuse_units": 0,
            "total_intrinsic_cost": 100,
            "world_checksum": 905,
        },
        "semantic_checksum": 0,
    }
    root_document = {
        "corpus_checksum": _CORPUS_CHECKSUM,
        "config": {"case_id": 100, "requested_pool_size": 4},
    }
    result["root_seed"] = validator.compute_canonical_root_seed(root_document)
    result["semantic_checksum"] = validator.compute_semantic_checksum(result)
    return result


def _record(repetition: int, arm: int, order: int) -> dict[str, object]:
    candidate = arm == 1
    lifecycle = _lifecycle(repetition, candidate)
    semantics = _semantics(repetition, arm, order)
    observation: dict[str, object] = {
        "schema_version": 1,
        "authority_kind": 0,
        "authority_run_identity": 1001,
        "controller_identity": 1002,
        "process_instance_identity": 2002 if candidate else 2001,
        "associated_semantic_checksum": semantics["semantic_checksum"],
        "configured_wall_limit_nanoseconds": 2_000,
        "configured_address_space_limit_bytes": 1 << 34,
        "configured_peak_host_limit_bytes": 1 << 30,
        "outer_elapsed_nanoseconds": 50 + repetition,
        "peak_host_bytes": 20_000 if candidate else 10_000,
        "process_exit_code": 0,
        "isolated_process": True,
        "wall_authority_enforced": True,
        "memory_authority_enforced": True,
        "persistent_preparer_reused": candidate,
        "preparer_lifecycle": copy.deepcopy(lifecycle),
        "authority_checksum": 0,
    }
    observation["authority_checksum"] = validator.compute_authority_checksum(observation)
    result: dict[str, object] = {
        "semantics": semantics,
        "case_build_elapsed_nanoseconds": 10,
        "prepared_elapsed_nanoseconds": 20,
        "cold_elapsed_nanoseconds": 40,
        "preparer_lifecycle": lifecycle,
        "external_observation": observation,
        "artifact_checksum": 0,
    }
    result["artifact_checksum"] = validator.compute_record_checksum(result)
    return result


def _arm_attempt(repetition: int, arm: int, order: int, ordinal: int) -> dict[str, object]:
    record = _record(repetition, arm, order)
    result: dict[str, object] = {
        "schema_version": 1,
        "arm": arm,
        "repetition_index": repetition,
        "execution_order": order,
        "disposition": 0,
        "dispatch_ordinal": ordinal,
        "process_instance_identity": 2002 if arm else 2001,
        "outer_elapsed_nanoseconds": 50 + repetition,
        "process_lifetime_peak_host_bytes": 20_000 if arm else 10_000,
        "raw_wait_status": 0,
        "process_exit_code": 0,
        "terminating_signal": 0,
        "watchdog_kill_sent": False,
        "controller_invariant_id": "",
        "controller_detail": "",
        "record": record,
        "child_failure": None,
        "attempt_checksum": 0,
    }
    result["attempt_checksum"] = validator.compute_arm_attempt_checksum(result)
    return result


def _pair_attempt(repetition: int) -> dict[str, object]:
    order = repetition % 2
    first = 2 * repetition + 1
    baseline_ordinal = first if order == 0 else first + 1
    candidate_ordinal = first + 1 if order == 0 else first
    baseline = _arm_attempt(repetition, 0, order, baseline_ordinal)
    candidate = _arm_attempt(repetition, 1, order, candidate_ordinal)
    paired: dict[str, object] = {
        "schema_version": 1,
        "baseline": copy.deepcopy(baseline["record"]),
        "candidate": copy.deepcopy(candidate["record"]),
        "comparison": 0,
        "semantic_checksum": 0,
        "artifact_checksum": 0,
    }
    paired["semantic_checksum"] = validator.compute_paired_semantic_checksum(paired)
    paired["artifact_checksum"] = validator.compute_paired_artifact_checksum(paired)
    result: dict[str, object] = {
        "schema_version": 1,
        "case_id": 100,
        "requested_pool_size": 4,
        "repetition_index": repetition,
        "root_seed": baseline["record"]["semantics"]["root_seed"],
        "execution_order": order,
        "baseline": baseline,
        "candidate": candidate,
        "result": paired,
        "attempt_checksum": 0,
    }
    result["attempt_checksum"] = validator.compute_pair_attempt_checksum(result)
    return result


def _artifact(repetitions: int = 2) -> dict[str, object]:
    environment: dict[str, object] = {
        "schema_version": 1,
        "host_os": "Linux",
        "host_kernel": "test",
        "host_architecture": "x86_64",
        "cpu_model": "test cpu",
        "compiler_identity": "clang-test",
        "monotonic_clock": "clock_monotonic",
        "online_cpu_count": 8,
        "affinity_cpu_count": 8,
        "total_host_memory_bytes": 1 << 34,
        "environment_checksum": 0,
    }
    environment["environment_checksum"] = validator.compute_environment_checksum(environment)
    result: dict[str, object] = {
        "wire_schema_version": 1,
        "source_commit": _EXPECTED_COMMIT,
        "source_stamped": True,
        "source_tree_dirty": False,
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "config": {
            "schema_version": 1,
            "case_id": 100,
            "requested_pool_size": 4,
            "preparation_worker_count": 4,
            "repetitions": repetitions,
            "maximum_setup_elapsed_nanoseconds": 3_000,
            "external_budget": _budget(),
            "corpus_limits": {
                "maximum_nets": 100,
                "maximum_compiled_nodes": 3_000,
                "maximum_compiled_host_bytes": 1 << 30,
                "maximum_active_regions": 100,
                "maximum_board_entities": 1_000,
            },
        },
        "environment": environment,
        "corpus_checksum": _CORPUS_CHECKSUM,
        "cell_plan_checksum": 0,
        "authority_run_identity": 1001,
        "controller_identity": 1002,
        "attempts": [_pair_attempt(index) for index in range(repetitions)],
        "artifact_checksum": 0,
    }
    _, manifest_cases, manifest_budgets = validator._representative_manifest()
    expected_budget_checksum = validator.compute_canonical_budget_checksum(
        result, manifest_cases[100], manifest_budgets[(100, 4)]
    )
    for repetition in range(repetitions):
        for name in ("baseline", "candidate"):
            arm = result["attempts"][repetition][name]
            semantics = arm["record"]["semantics"]
            semantics["budget_checksum"] = expected_budget_checksum
            semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
            arm["record"]["external_observation"]["associated_semantic_checksum"] = semantics[
                "semantic_checksum"
            ]
        _refresh_pair(result, repetition)
    result["cell_plan_checksum"] = validator.compute_cell_plan_checksum(result)
    result["artifact_checksum"] = validator.compute_cell_artifact_checksum(result)
    result["source_envelope_checksum"] = validator.compute_source_envelope_checksum(result)
    return result


def _failure() -> dict[str, object]:
    result: dict[str, object] = {
        "schema_version": 1,
        "summary_code": 7,
        "arm": 1,
        "summary_required": 10,
        "summary_configured": 9,
        "summary_invariant_id": "candidate.failed",
        "summary_detail": "failed",
        "payload_kind": 3,
        "child_error_code": 2,
        "child_invariant_id": "child.failed",
        "child_detail": "child failed",
        "has_child_net": True,
        "child_net_id": 5,
        "child_net_generation": 1,
        "child_required": 10,
        "child_configured": 9,
        "child_bound_kind": 0,
        "child_secondary_required": 0,
        "child_secondary_configured": 0,
        "has_case_identity": True,
        "case_id": 100,
        "descriptor_fingerprint": _CASE_100["descriptor_fingerprint"],
        "case_checksum": _CASE_100["case_checksum"],
        "board_content_hash": _CASE_100["board_content_hash"],
        "workload_checksum": _CASE_100["workload_checksum"],
        "capacity_model_checksum": _CASE_100["capacity_model_checksum"],
        "has_epoch_index": False,
        "epoch_index": 0,
        "has_failed_observation": True,
        "failed_observation_checksum": 44,
        "attempted_column_count": 4,
        "attempted_route_queries": 4,
        "attempted_route_work_units": 40,
        "candidate_store_publication_committed": False,
        "authoritative_candidate_store_present": False,
        "reconciled_candidate_count": 0,
        "reconciled_rejection_count": 0,
        "reconciled_candidate_store_checksum": 0,
        "payload_checksum": 0,
    }
    result["payload_checksum"] = validator.compute_failure_checksum(result)
    return result


def _refresh_arm(arm: dict[str, object]) -> None:
    record = arm["record"]
    observation = record["external_observation"]
    observation["authority_checksum"] = validator.compute_authority_checksum(observation)
    record["artifact_checksum"] = validator.compute_record_checksum(record)
    arm["attempt_checksum"] = validator.compute_arm_attempt_checksum(arm)


def _refresh_pair(document: dict[str, object], repetition: int) -> None:
    pair = document["attempts"][repetition]
    for field in ("baseline", "candidate"):
        _refresh_arm(pair[field])
        pair["result"][field] = copy.deepcopy(pair[field]["record"])
    pair["result"]["semantic_checksum"] = validator.compute_paired_semantic_checksum(pair["result"])
    pair["result"]["artifact_checksum"] = validator.compute_paired_artifact_checksum(pair["result"])
    pair["attempt_checksum"] = validator.compute_pair_attempt_checksum(pair)
    document["artifact_checksum"] = validator.compute_cell_artifact_checksum(document)
    document["source_envelope_checksum"] = validator.compute_source_envelope_checksum(document)


class Phase4RawEvidenceValidatorTest(unittest.TestCase):
    def test_accepts_complete_canonical_cell(self) -> None:
        _validate_document(_artifact(20))

    def test_rejects_fabricated_or_nonbuildable_frozen_corpus_identity(self) -> None:
        artifact = _artifact(2)
        artifact["corpus_checksum"] ^= 1
        artifact["cell_plan_checksum"] = validator.compute_cell_plan_checksum(artifact)
        artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
        artifact["source_envelope_checksum"] = validator.compute_source_envelope_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "frozen representative manifest"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        artifact["config"]["requested_pool_size"] = 8
        artifact["cell_plan_checksum"] = validator.compute_cell_plan_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "frozen case roster"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        artifact["config"]["case_id"] = 3001
        artifact["cell_plan_checksum"] = validator.compute_cell_plan_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "bounded stress witness"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        for name in ("baseline", "candidate"):
            semantics = artifact["attempts"][0][name]["record"]["semantics"]
            semantics["descriptor_fingerprint"] ^= 1
            semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
            artifact["attempts"][0][name]["record"]["external_observation"][
                "associated_semantic_checksum"
            ] = semantics["semantic_checksum"]
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "another command or cell"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_fabricated_budget_and_component_caps(self) -> None:
        artifact = _artifact(2)
        for name in ("baseline", "candidate"):
            semantics = artifact["attempts"][0][name]["record"]["semantics"]
            semantics["budget_checksum"] ^= 1
            semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
            artifact["attempts"][0][name]["record"]["external_observation"][
                "associated_semantic_checksum"
            ] = semantics["semantic_checksum"]
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "another command or cell"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        semantics = artifact["attempts"][0]["candidate"]["record"]["semantics"]
        semantics["preparation_route_queries"] = 36
        semantics["regeneration_route_queries"] = 0
        semantics["preparation_route_work_units"] = 100
        semantics["regeneration_route_work_units"] = 0
        semantics["actual"]["route_queries"] = 36
        semantics["actual"]["route_work_units"] = 100
        semantics["requested_columns"] = 36
        semantics["rejected_columns"] = 28
        semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
        artifact["attempts"][0]["candidate"]["record"]["external_observation"][
            "associated_semantic_checksum"
        ] = semantics["semantic_checksum"]
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "component work"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_impossible_authenticated_semantic_counters(self) -> None:
        def reauthenticate(artifact: dict[str, object], semantics: dict[str, object]) -> None:
            semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
            artifact["attempts"][0]["candidate"]["record"]["external_observation"][
                "associated_semantic_checksum"
            ] = semantics["semantic_checksum"]
            _refresh_pair(artifact, 0)

        artifact = _artifact(2)
        semantics = artifact["attempts"][0]["candidate"]["record"]["semantics"]
        semantics["final_candidate_count"] = 5
        reauthenticate(artifact, semantics)
        with self.assertRaisesRegex(validator.EvidenceError, "counters are inconsistent"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        semantics = artifact["attempts"][0]["candidate"]["record"]["semantics"]
        semantics["rejected_columns"] = 1
        reauthenticate(artifact, semantics)
        with self.assertRaisesRegex(validator.EvidenceError, "counters are inconsistent"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        semantics = artifact["attempts"][0]["candidate"]["record"]["semantics"]
        semantics["preparation_route_queries"] = 1
        semantics["preparation_route_work_units"] = 1_000_000_001
        semantics["regeneration_route_queries"] = 0
        semantics["regeneration_route_work_units"] = 0
        semantics["actual"]["route_queries"] = 1
        semantics["actual"]["route_work_units"] = 1_000_000_001
        reauthenticate(artifact, semantics)
        with self.assertRaisesRegex(validator.EvidenceError, "counters are inconsistent"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_success_case_underprovisioned_by_frozen_resource_requirements(self) -> None:
        for field, value in (
            ("maximum_nets", 5),
            ("maximum_compiled_nodes", 2_441),
            ("maximum_compiled_host_bytes", 62_783),
            ("maximum_active_regions", 41),
            ("maximum_board_entities", 19),
        ):
            with self.subTest(field=field):
                artifact = _artifact(2)
                artifact["config"]["corpus_limits"][field] = value
                artifact["cell_plan_checksum"] = validator.compute_cell_plan_checksum(artifact)
                with self.assertRaisesRegex(validator.EvidenceError, "cannot build"):
                    _validate_document(artifact, expected_repetitions=2)

    def test_testing_repetition_override_is_explicit(self) -> None:
        artifact = _artifact(2)
        with self.assertRaisesRegex(validator.EvidenceError, "exactly 20"):
            _validate_document(artifact)
        _validate_document(artifact, expected_repetitions=2)

    def test_testing_worker_override_is_explicit(self) -> None:
        artifact = _artifact(2)
        artifact["config"]["preparation_worker_count"] = 1
        for repetition in range(2):
            for name in ("baseline", "candidate"):
                arm = artifact["attempts"][repetition][name]
                arm["record"]["semantics"]["preparation_worker_count"] = 1
                arm["record"]["semantics"]["semantic_checksum"] = (
                    validator.compute_semantic_checksum(arm["record"]["semantics"])
                )
                arm["record"]["external_observation"]["associated_semantic_checksum"] = arm[
                    "record"
                ]["semantics"]["semantic_checksum"]
                if name == "candidate":
                    lifecycle = arm["record"]["preparer_lifecycle"]
                    lifecycle["workers_started_before"] = 1
                    lifecycle["workers_started_after"] = 1
                    arm["record"]["external_observation"]["preparer_lifecycle"] = copy.deepcopy(
                        lifecycle
                    )
            _refresh_pair(artifact, repetition)
        artifact["cell_plan_checksum"] = validator.compute_cell_plan_checksum(artifact)
        artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
        artifact["source_envelope_checksum"] = validator.compute_source_envelope_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "must be exactly 4"):
            _validate_document(artifact, expected_repetitions=2)
        _validate_document(artifact, expected_repetitions=2, expected_workers=1)

    def test_rejects_noncanonical_equal_budget_shape_with_valid_checksums(self) -> None:
        artifact = _artifact(2)
        for name in ("baseline", "candidate"):
            arm = artifact["attempts"][0][name]
            arm["record"]["semantics"]["baseline_sweeps"] = 7
            arm["record"]["semantics"]["semantic_checksum"] = validator.compute_semantic_checksum(
                arm["record"]["semantics"]
            )
            arm["record"]["external_observation"]["associated_semantic_checksum"] = arm["record"][
                "semantics"
            ]["semantic_checksum"]
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "canonical equal-budget shape"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_cross_repetition_semantic_or_comparison_drift(self) -> None:
        artifact = _artifact(2)
        candidate = artifact["attempts"][1]["candidate"]
        semantics = candidate["record"]["semantics"]
        semantics["outcome"]["world_checksum"] ^= 1
        semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
        candidate["record"]["external_observation"]["associated_semantic_checksum"] = semantics[
            "semantic_checksum"
        ]
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "not deterministic across"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        pair = artifact["attempts"][1]
        candidate = pair["candidate"]
        semantics = candidate["record"]["semantics"]
        semantics["outcome"]["total_intrinsic_cost"] += 1
        semantics["semantic_checksum"] = validator.compute_semantic_checksum(semantics)
        candidate["record"]["external_observation"]["associated_semantic_checksum"] = semantics[
            "semantic_checksum"
        ]
        pair["result"]["comparison"] = 1
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "comparison is not deterministic"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_unstamped_or_dirty_publication(self) -> None:
        artifact = _artifact()
        artifact["source_stamped"] = False
        artifact["source_tree_dirty"] = True
        artifact["source_commit"] = "unstamped"
        with self.assertRaisesRegex(validator.EvidenceError, "publication requires"):
            _validate_document(artifact, expected_repetitions=2)
        artifact["source_envelope_checksum"] = validator.compute_source_envelope_checksum(artifact)
        _validate_document(artifact, allow_unstamped=True, expected_repetitions=2)

    def test_publication_requires_and_matches_independent_commit(self) -> None:
        artifact = _artifact(2)
        with self.assertRaisesRegex(validator.EvidenceError, "independently supplied"):
            validator.validate_document(artifact, expected_repetitions=2)
        with self.assertRaisesRegex(validator.EvidenceError, "does not match"):
            validator.validate_document(artifact, expected_commit="b" * 40, expected_repetitions=2)

        artifact["source_commit"] = "b" * 40
        artifact["source_envelope_checksum"] = validator.compute_source_envelope_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "does not match"):
            _validate_document(artifact, expected_repetitions=2)

    def test_source_envelope_authenticates_provenance(self) -> None:
        artifact = _artifact(2)
        artifact["source_tree_dirty"] = True
        with self.assertRaisesRegex(validator.EvidenceError, "source provenance"):
            _validate_document(
                artifact,
                allow_unstamped=True,
                expected_repetitions=2,
            )

    def test_rejects_noncanonical_json_bytes_and_key_order(self) -> None:
        artifact = _artifact(2)
        canonical = (
            json.dumps(artifact, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
        )
        reordered = {"source_commit": artifact["source_commit"]}
        reordered.update({key: value for key, value in artifact.items() if key != "source_commit"})
        hostile = {
            "pretty": json.dumps(artifact, indent=2) + "\n",
            "no_lf": canonical[:-1],
            "extra_lf": canonical + "\n",
            "reordered": json.dumps(reordered, separators=(",", ":")) + "\n",
            "redundant_escape": canonical.replace("Linux", "Lin\\/ux", 1),
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "cell.json"
            path.write_text(canonical, encoding="utf-8")
            self.assertEqual(validator.read_document(path), artifact)
            for name, contents in hostile.items():
                with self.subTest(name=name):
                    path.write_text(contents, encoding="utf-8")
                    with self.assertRaisesRegex(
                        validator.EvidenceError, "canonical key order|canonical one-line JSON"
                    ):
                        validator.read_document(path)

    def test_rejects_out_of_contract_budget_and_corpus_limits(self) -> None:
        mutations = (
            (
                "prepared",
                lambda value: value["config"]["external_budget"].update(
                    maximum_prepared_elapsed_nanoseconds=2_001
                ),
                "prepared elapsed cap",
            ),
            (
                "cold watchdog",
                lambda value: value["config"]["external_budget"].update(
                    maximum_cold_elapsed_nanoseconds=validator._MAXIMUM_WATCHDOG_NANOSECONDS + 1
                ),
                "24 hours",
            ),
            (
                "setup watchdog",
                lambda value: value["config"].update(
                    maximum_setup_elapsed_nanoseconds=validator._MAXIMUM_WATCHDOG_NANOSECONDS + 1
                ),
                "24 hours",
            ),
            (
                "rlim infinity",
                lambda value: value["config"]["external_budget"].update(
                    maximum_address_space_bytes=validator._U64_MAX
                ),
                "RLIM_INFINITY",
            ),
        )
        for name, mutate, diagnostic in mutations:
            with self.subTest(name=name):
                artifact = _artifact(2)
                mutate(artifact)
                with self.assertRaisesRegex(validator.EvidenceError, diagnostic):
                    _validate_document(artifact, expected_repetitions=2)

        for field, maximum in validator._MAXIMUM_CORPUS_LIMITS.items():
            with self.subTest(field=field):
                artifact = _artifact(2)
                artifact["config"]["corpus_limits"][field] = maximum + 1
                with self.assertRaisesRegex(validator.EvidenceError, "must not exceed"):
                    _validate_document(artifact, expected_repetitions=2)

    def test_rejects_duplicate_json_keys(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "duplicate.json"
            path.write_text('{"wire_schema_version":1,"wire_schema_version":1}', encoding="utf-8")
            with self.assertRaisesRegex(validator.EvidenceError, "duplicate JSON object key"):
                validator.read_document(path)

    def test_rejects_extra_field_and_bool_as_integer(self) -> None:
        artifact = _artifact()
        artifact["extra"] = 1
        with self.assertRaisesRegex(validator.EvidenceError, "extra=\\['extra'\\]"):
            _validate_document(artifact, expected_repetitions=2)
        artifact = _artifact()
        artifact["source_stamped"] = 1
        with self.assertRaisesRegex(validator.EvidenceError, "must be a boolean"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_unknown_enum_even_with_recomputed_envelopes(self) -> None:
        artifact = _artifact()
        arm = artifact["attempts"][0]["candidate"]
        arm["record"]["semantics"]["terminal_reason"] = 5
        arm["record"]["semantics"]["semantic_checksum"] = validator.compute_semantic_checksum(
            arm["record"]["semantics"]
        )
        arm["record"]["external_observation"]["associated_semantic_checksum"] = arm["record"][
            "semantics"
        ]["semantic_checksum"]
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "unknown enum value"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_checksum_corruption(self) -> None:
        artifact = _artifact()
        artifact["attempts"][0]["baseline"]["attempt_checksum"] ^= 1
        artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "attempt_checksum"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_missing_reordered_or_wrong_ab_ba_attempt(self) -> None:
        artifact = _artifact()
        artifact["attempts"].reverse()
        artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "missing, extra, reordered"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact()
        first = artifact["attempts"][0]
        first["baseline"]["dispatch_ordinal"] = 2
        first["candidate"]["dispatch_ordinal"] = 1
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "serial order"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_process_replacement_and_shared_contender_process(self) -> None:
        artifact = _artifact()
        candidate = artifact["attempts"][1]["candidate"]
        candidate["process_instance_identity"] = 2999
        candidate["record"]["external_observation"]["process_instance_identity"] = 2999
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "long-lived process"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact()
        for pair in artifact["attempts"]:
            candidate = pair["candidate"]
            candidate["process_instance_identity"] = 2001
            candidate["record"]["external_observation"]["process_instance_identity"] = 2001
        _refresh_pair(artifact, 0)
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "distinct isolated processes"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_peak_drift_and_observation_misassociation(self) -> None:
        artifact = _artifact()
        candidate = artifact["attempts"][1]["candidate"]
        candidate["process_lifetime_peak_host_bytes"] += 1
        candidate["record"]["external_observation"]["peak_host_bytes"] += 1
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "wait4 peak"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact()
        baseline = artifact["attempts"][0]["baseline"]
        baseline["record"]["external_observation"]["authority_run_identity"] = 9999
        _refresh_pair(artifact, 0)
        with self.assertRaisesRegex(validator.EvidenceError, "another process"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_lifecycle_gap_with_valid_checksums(self) -> None:
        artifact = _artifact()
        candidate = artifact["attempts"][1]["candidate"]
        lifecycle = candidate["record"]["preparer_lifecycle"]
        lifecycle["invocations_started_before"] += 4
        lifecycle["invocations_started_after"] += 4
        lifecycle["invocations_completed_before"] += 4
        lifecycle["invocations_completed_after"] += 4
        candidate["record"]["external_observation"]["preparer_lifecycle"] = copy.deepcopy(lifecycle)
        _refresh_pair(artifact, 1)
        with self.assertRaisesRegex(validator.EvidenceError, "not continuous"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_unpaired_attempt_and_result_copy_drift(self) -> None:
        artifact = _artifact()
        artifact["attempts"][0]["result"] = None
        artifact["attempts"][0]["attempt_checksum"] = validator.compute_pair_attempt_checksum(
            artifact["attempts"][0]
        )
        artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
        with self.assertRaisesRegex(validator.EvidenceError, "completed pair"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact()
        artifact["attempts"][0]["result"]["baseline"]["cold_elapsed_nanoseconds"] += 1
        with self.assertRaisesRegex(validator.EvidenceError, "artifact_checksum"):
            _validate_document(artifact, expected_repetitions=2)

    def test_authenticates_durable_failure_before_rejecting_incomplete_cell(self) -> None:
        failure = _failure()
        validator._failure(failure, "failure")
        failure["summary_required"] += 1
        with self.assertRaisesRegex(validator.EvidenceError, "payload_checksum"):
            validator._failure(failure, "failure")

        artifact = _artifact()
        candidate = artifact["attempts"][0]["candidate"]
        candidate["disposition"] = 1
        candidate["record"] = None
        candidate["child_failure"] = _failure()
        candidate["attempt_checksum"] = validator.compute_arm_attempt_checksum(candidate)
        with self.assertRaisesRegex(validator.EvidenceError, "not a successful attempt"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_typed_or_corpus_failure_from_another_active_case(self) -> None:
        def install_failure(artifact: dict[str, object], failure: dict[str, object]) -> None:
            pair = artifact["attempts"][0]
            candidate = pair["candidate"]
            candidate["disposition"] = 1
            candidate["record"] = None
            candidate["child_failure"] = failure
            candidate["attempt_checksum"] = validator.compute_arm_attempt_checksum(candidate)
            pair["result"] = None
            pair["attempt_checksum"] = validator.compute_pair_attempt_checksum(pair)
            artifact["artifact_checksum"] = validator.compute_cell_artifact_checksum(artifact)
            artifact["source_envelope_checksum"] = validator.compute_source_envelope_checksum(
                artifact
            )

        artifact = _artifact(2)
        failure = _failure()
        failure["case_checksum"] ^= 1
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        install_failure(artifact, failure)
        with self.assertRaisesRegex(validator.EvidenceError, "associated with another case"):
            _validate_document(artifact, expected_repetitions=2)

        artifact = _artifact(2)
        failure = _failure()
        failure.update(
            summary_code=4,
            payload_kind=1,
            child_error_code=1,
            child_invariant_id="corpus.unknown",
            child_detail="unknown case",
            has_child_net=False,
            child_net_id=0,
            child_net_generation=0,
            child_required=0,
            child_configured=0,
            has_case_identity=False,
            case_id=101,
            descriptor_fingerprint=0,
            case_checksum=0,
            board_content_hash=0,
            workload_checksum=0,
            capacity_model_checksum=0,
            has_failed_observation=False,
            failed_observation_checksum=0,
            attempted_column_count=0,
            attempted_route_queries=0,
            attempted_route_work_units=0,
        )
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        install_failure(artifact, failure)
        with self.assertRaisesRegex(validator.EvidenceError, "associated with another case"):
            _validate_document(artifact, expected_repetitions=2)

    def test_rejects_failure_enum_presence_and_canonical_zero_contradictions(self) -> None:
        mutations = (
            (
                "child enum",
                lambda value: value.update(child_error_code=11),
                "no greater than 10",
            ),
            (
                "absent child net",
                lambda value: value.update(has_child_net=False),
                "canonical zero identity",
            ),
            (
                "absent case",
                lambda value: value.update(has_case_identity=False),
                "canonical zero fingerprints",
            ),
            (
                "absent epoch",
                lambda value: value.update(epoch_index=1),
                "absent epoch index",
            ),
            (
                "absent observation",
                lambda value: value.update(has_failed_observation=False),
                "canonical zero counters",
            ),
            (
                "absent store",
                lambda value: value.update(reconciled_candidate_count=1),
                "canonical zero roster",
            ),
            (
                "wrong arm",
                lambda value: value.update(arm=0),
                "summary code|candidate-preparation payload",
            ),
            (
                "wrong summary",
                lambda value: value.update(summary_code=4),
                "candidate-preparation payload",
            ),
            (
                "committed absent store",
                lambda value: value.update(candidate_store_publication_committed=True),
                "must remain authoritative",
            ),
        )
        for name, mutate, diagnostic in mutations:
            with self.subTest(name=name):
                failure = _failure()
                mutate(failure)
                failure["payload_checksum"] = validator.compute_failure_checksum(failure)
                with self.assertRaisesRegex(validator.EvidenceError, diagnostic):
                    validator._failure(failure, "failure")

    def test_rejects_attempted_columns_without_noncorpus_observation(self) -> None:
        failure = _failure()
        failure.update(
            has_failed_observation=False,
            failed_observation_checksum=0,
            attempted_route_queries=0,
            attempted_route_work_units=0,
        )
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "canonical zero counters"):
            validator._failure(failure, "failure")

    def test_rejects_impossible_observed_failure_work_and_active_case_caps(self) -> None:
        failure = _failure()
        failure["attempted_column_count"] = 3
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "canonical query work"):
            validator._failure(failure, "failure")

        failure = _failure()
        failure["attempted_route_work_units"] = 4_000_000_001
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "canonical query work"):
            validator._failure(failure, "failure")

        _, manifest_cases, _ = validator._representative_manifest()
        failure = _failure()
        failure["attempted_column_count"] = 25
        failure["attempted_route_queries"] = 25
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "active case bound"):
            validator._failure(
                failure,
                "failure",
                expected_manifest_case=manifest_cases[100],
                requested_pool_size=4,
            )

        failure = _failure()
        failure.update(
            summary_code=8,
            payload_kind=4,
            child_error_code=2,
            has_child_net=False,
            child_net_id=0,
            child_net_generation=0,
            child_required=0,
            child_configured=0,
            attempted_column_count=7,
            attempted_route_queries=7,
            attempted_route_work_units=70,
            authoritative_candidate_store_present=True,
            reconciled_candidate_count=1,
            reconciled_candidate_store_checksum=123,
        )
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "active case bound"):
            validator._failure(
                failure,
                "failure",
                expected_manifest_case=manifest_cases[100],
                requested_pool_size=4,
            )

    def test_rejects_summary_only_payload_for_typed_failure_summaries(self) -> None:
        for summary_code in (4, 6, 7, 8, 9, 11, 12):
            with self.subTest(summary_code=summary_code):
                failure = _failure()
                failure.update(
                    summary_code=summary_code,
                    arm=0 if summary_code in {4, 6} else 1,
                    payload_kind=0,
                    child_error_code=0,
                    child_invariant_id="",
                    child_detail="",
                    has_child_net=False,
                    child_net_id=0,
                    child_net_generation=0,
                    child_required=0,
                    child_configured=0,
                    has_case_identity=False,
                    case_id=0,
                    descriptor_fingerprint=0,
                    case_checksum=0,
                    board_content_hash=0,
                    workload_checksum=0,
                    capacity_model_checksum=0,
                    has_failed_observation=False,
                    failed_observation_checksum=0,
                    attempted_column_count=0,
                    attempted_route_queries=0,
                    attempted_route_work_units=0,
                )
                failure["payload_checksum"] = validator.compute_failure_checksum(failure)
                with self.assertRaisesRegex(validator.EvidenceError, "summary-only"):
                    validator._failure(failure, "failure")

        failure = _failure()
        failure.update(
            summary_code=10,
            payload_kind=0,
            child_error_code=0,
            child_invariant_id="",
            child_detail="",
            has_child_net=False,
            child_net_id=0,
            child_net_generation=0,
            child_required=0,
            child_configured=0,
            has_case_identity=False,
            case_id=0,
            descriptor_fingerprint=0,
            case_checksum=0,
            board_content_hash=0,
            workload_checksum=0,
            capacity_model_checksum=0,
            has_failed_observation=False,
            failed_observation_checksum=0,
            attempted_column_count=0,
            attempted_route_queries=0,
            attempted_route_work_units=0,
        )
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        validator._failure(failure, "failure")

    def test_rejects_contradictory_corpus_work_bound_failure(self) -> None:
        failure = _failure()
        failure.update(
            summary_code=4,
            arm=0,
            payload_kind=1,
            child_error_code=10,
            has_case_identity=False,
            descriptor_fingerprint=0,
            case_checksum=0,
            board_content_hash=0,
            workload_checksum=0,
            capacity_model_checksum=0,
            has_failed_observation=False,
            failed_observation_checksum=0,
            attempted_route_queries=0,
            attempted_route_work_units=0,
            child_bound_kind=1,
        )
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        validator._failure(failure, "failure")

        failure["child_bound_kind"] = 0
        failure["payload_checksum"] = validator.compute_failure_checksum(failure)
        with self.assertRaisesRegex(validator.EvidenceError, "bound-witness state"):
            validator._failure(failure, "failure")

        for mutation in (
            {"child_net_id": 0},
            {"child_required": 9, "child_configured": 9},
            {
                "child_bound_kind": 3,
                "child_required": 10,
                "child_configured": 9,
                "child_secondary_required": 8,
                "child_secondary_configured": 8,
            },
        ):
            failure = _failure()
            failure.update(
                summary_code=4,
                arm=0,
                payload_kind=1,
                child_error_code=10,
                has_case_identity=False,
                descriptor_fingerprint=0,
                case_checksum=0,
                board_content_hash=0,
                workload_checksum=0,
                capacity_model_checksum=0,
                has_failed_observation=False,
                failed_observation_checksum=0,
                attempted_route_queries=0,
                attempted_route_work_units=0,
                child_bound_kind=1,
            )
            failure.update(mutation)
            failure["payload_checksum"] = validator.compute_failure_checksum(failure)
            with self.assertRaisesRegex(validator.EvidenceError, "bound-witness state"):
                validator._failure(failure, "failure")


if __name__ == "__main__":
    unittest.main()
