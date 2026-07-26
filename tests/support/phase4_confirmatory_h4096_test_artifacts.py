"""Pure synthetic Raw/telemetry artifacts for H4096 validator tests.

These helpers never invoke a runner, construct a representative case, read a
fixture, or execute an allocator. They exercise only frozen manifests and
stable checksum functions.
"""

from __future__ import annotations

import copy
from collections.abc import Mapping
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_representative_manifest_v2 as corpus_v2
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_SOURCE_COMMIT = "0" * 40
_AUTHORITY_RUN_IDENTITY = 1001
_CONTROLLER_IDENTITY = 1002
_BASELINE_PROCESS_IDENTITY = 2001
_CANDIDATE_PROCESS_IDENTITY = 2002


def _budget() -> dict[str, int]:
    return {
        "maximum_prepared_elapsed_nanoseconds": 1_000,
        "maximum_cold_elapsed_nanoseconds": 2_000,
        "maximum_address_space_bytes": 1 << 34,
        "maximum_peak_host_bytes": 1 << 30,
    }


def _corpus_limits() -> dict[str, int]:
    return dict(raw_validator._MAXIMUM_CORPUS_LIMITS)


def _environment() -> dict[str, object]:
    result: dict[str, object] = {
        "schema_version": 1,
        "host_os": "Linux",
        "host_kernel": "synthetic",
        "host_architecture": "x86_64",
        "cpu_model": "synthetic",
        "compiler_identity": "synthetic",
        "monotonic_clock": "clock_monotonic",
        "online_cpu_count": 8,
        "affinity_cpu_count": 8,
        "total_host_memory_bytes": 1 << 34,
        "environment_checksum": 0,
    }
    result["environment_checksum"] = raw_validator.compute_environment_checksum(result)
    return result


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


def _semantics(
    document: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    *,
    repetition: int,
    arm: int,
    order: int,
    budget_checksum: int,
) -> dict[str, object]:
    net_count = manifest_case["workload_net_count"]
    pool = document["config"]["requested_pool_size"]
    candidate = arm == 1
    result: dict[str, object] = {
        "schema_version": 1,
        "arm": arm,
        "execution_order": order,
        "corpus_version": 2,
        "corpus_checksum": document["corpus_checksum"],
        "case_id": document["config"]["case_id"],
        "descriptor_fingerprint": manifest_case["descriptor_fingerprint"],
        "case_checksum": manifest_case["case_checksum"],
        "board_content_hash": manifest_case["board_content_hash"],
        "workload_checksum": manifest_case["workload_checksum"],
        "capacity_model_checksum": manifest_case["capacity_model_checksum"],
        "budget_checksum": budget_checksum,
        "workload_net_count": net_count,
        "requested_pool_size": pool,
        "repetition_index": repetition,
        "root_seed": raw_validator.compute_canonical_root_seed(document, corpus_version=2),
        "preparation_worker_count": 4,
        "baseline_sweeps": pool + 2,
        "candidate_regeneration_epochs": 2,
        "candidate_columns_per_epoch": net_count,
        "candidate_terminal_selection_rounds": pool + 1,
        "external_budget": copy.deepcopy(document["config"]["external_budget"]),
        "opportunity": {
            "route_queries": net_count * (pool + 2),
            "route_work_units": net_count
            * (pool + 2)
            * raw_validator._CANONICAL_ROUTE_WORK_UNITS_PER_QUERY,
        },
        "actual": {
            "route_queries": net_count,
            "route_work_units": net_count,
        },
        "preparation_route_queries": net_count if candidate else 0,
        "preparation_route_work_units": net_count if candidate else 0,
        "regeneration_route_queries": 0,
        "regeneration_route_work_units": 0,
        "requested_columns": net_count,
        "admitted_candidates": net_count,
        "rejected_columns": 0,
        "final_candidate_count": net_count,
        "preparation_checksum": 901 if candidate else 0,
        "algorithm_session_checksum": 902 + arm,
        "final_pool_manifest_checksum": 903 if candidate else 0,
        "final_rejection_manifest_checksum": 904 if candidate else 0,
        "terminal_reason": 0,
        "candidate_outcome_source": 1 if candidate else 0,
        "outcome": {
            "selected_net_count": net_count,
            "no_candidate_net_count": 0,
            "overused_resource_count": 0,
            "total_overuse_units": 0,
            "total_intrinsic_cost": 100,
            "world_checksum": 905 + arm,
        },
        "semantic_checksum": 0,
    }
    result["semantic_checksum"] = raw_validator.compute_semantic_checksum(result)
    return result


def _record(
    document: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    *,
    repetition: int,
    arm: int,
    order: int,
    budget_checksum: int,
) -> dict[str, object]:
    candidate = arm == 1
    semantics = _semantics(
        document,
        manifest_case,
        repetition=repetition,
        arm=arm,
        order=order,
        budget_checksum=budget_checksum,
    )
    lifecycle = _lifecycle(repetition, candidate)
    observation: dict[str, object] = {
        "schema_version": 1,
        "authority_kind": 0,
        "authority_run_identity": _AUTHORITY_RUN_IDENTITY,
        "controller_identity": _CONTROLLER_IDENTITY,
        "process_instance_identity": (
            _CANDIDATE_PROCESS_IDENTITY if candidate else _BASELINE_PROCESS_IDENTITY
        ),
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
    observation["authority_checksum"] = raw_validator.compute_authority_checksum(observation)
    result: dict[str, object] = {
        "semantics": semantics,
        "case_build_elapsed_nanoseconds": 10,
        "prepared_elapsed_nanoseconds": 20,
        "cold_elapsed_nanoseconds": 40,
        "preparer_lifecycle": lifecycle,
        "external_observation": observation,
        "artifact_checksum": 0,
    }
    result["artifact_checksum"] = raw_validator.compute_record_checksum(result)
    return result


def _arm_attempt(
    document: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    *,
    repetition: int,
    arm: int,
    order: int,
    ordinal: int,
    budget_checksum: int,
) -> dict[str, object]:
    record = _record(
        document,
        manifest_case,
        repetition=repetition,
        arm=arm,
        order=order,
        budget_checksum=budget_checksum,
    )
    candidate = arm == 1
    result: dict[str, object] = {
        "schema_version": 1,
        "arm": arm,
        "repetition_index": repetition,
        "execution_order": order,
        "disposition": 0,
        "dispatch_ordinal": ordinal,
        "process_instance_identity": (
            _CANDIDATE_PROCESS_IDENTITY if candidate else _BASELINE_PROCESS_IDENTITY
        ),
        "outer_elapsed_nanoseconds": 50 + repetition,
        "process_lifetime_peak_host_bytes": 20_000 if candidate else 10_000,
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
    result["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(result)
    return result


def _pair_attempt(
    document: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    *,
    repetition: int,
    budget_checksum: int,
) -> dict[str, object]:
    order = repetition % 2
    first = repetition * 2 + 1
    baseline = _arm_attempt(
        document,
        manifest_case,
        repetition=repetition,
        arm=0,
        order=order,
        ordinal=first if order == 0 else first + 1,
        budget_checksum=budget_checksum,
    )
    candidate = _arm_attempt(
        document,
        manifest_case,
        repetition=repetition,
        arm=1,
        order=order,
        ordinal=first + 1 if order == 0 else first,
        budget_checksum=budget_checksum,
    )
    paired: dict[str, object] = {
        "schema_version": 1,
        "baseline": copy.deepcopy(baseline["record"]),
        "candidate": copy.deepcopy(candidate["record"]),
        "comparison": 0,
        "semantic_checksum": 0,
        "artifact_checksum": 0,
    }
    paired["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(paired)
    paired["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(paired)
    result: dict[str, object] = {
        "schema_version": 1,
        "case_id": document["config"]["case_id"],
        "requested_pool_size": document["config"]["requested_pool_size"],
        "repetition_index": repetition,
        "root_seed": baseline["record"]["semantics"]["root_seed"],
        "execution_order": order,
        "baseline": baseline,
        "candidate": candidate,
        "result": paired,
        "attempt_checksum": 0,
    }
    result["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(result)
    return result


def make_raw(
    case_id: int,
    pool: int,
    *,
    same_run: bool,
    h4096: bool,
    repetitions: int = 1,
) -> dict[str, object]:
    """Build one complete synthetic Corpus-v2 Raw artifact."""
    corpus_checksum, cases, h2250_budgets = (
        raw_validator._frozen_confirmatory_representative_manifest()
    )
    manifest_case = cases[case_id]
    config = {
        "schema_version": 1,
        "case_id": case_id,
        "requested_pool_size": pool,
        "preparation_worker_count": 4,
        "repetitions": repetitions,
        "maximum_setup_elapsed_nanoseconds": 3_000,
        "external_budget": _budget(),
        "corpus_limits": _corpus_limits(),
    }
    result: dict[str, object] = {}
    if same_run:
        result["raw_evidence_schema_version"] = 2
    result.update(
        {
            "wire_schema_version": 2 if same_run else 1,
            "source_commit": _SOURCE_COMMIT,
            "source_stamped": False,
            "source_tree_dirty": True,
            "source_envelope_checksum": 0,
            "schema_version": 1,
            "config": config,
            "environment": _environment(),
            "corpus_checksum": corpus_checksum,
            "cell_plan_checksum": 0,
            "authority_run_identity": _AUTHORITY_RUN_IDENTITY,
            "controller_identity": _CONTROLLER_IDENTITY,
            "attempts": [],
            "artifact_checksum": 0,
        }
    )
    canonical_budgets = (
        budget_v3.budget_map(budget_v3.validate_roster()) if h4096 else h2250_budgets
    )
    algorithm_budget_checksum = canonical_budgets[(case_id, pool)]
    paired_budget_checksum = raw_validator.compute_canonical_budget_checksum(
        result,
        manifest_case,
        algorithm_budget_checksum,
        corpus_version=2,
    )
    result["attempts"] = [
        _pair_attempt(
            result,
            manifest_case,
            repetition=repetition,
            budget_checksum=paired_budget_checksum,
        )
        for repetition in range(repetitions)
    ]
    result["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
        result, corpus_version=2
    )
    result["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(result)
    result["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(result)
    return result


def make_h2250_zero_dispatch_same_run_raw() -> dict[str, object]:
    """Build an H2250 Raw-v2 setup failure with no successful arm witness."""
    result = make_raw(10100, 4, same_run=True, h4096=False, repetitions=1)
    pair = result["attempts"][0]
    for arm_name in ("baseline", "candidate"):
        arm = pair[arm_name]
        failed = arm_name == "baseline"
        arm.update(
            {
                "disposition": 7 if failed else 10,
                "dispatch_ordinal": 0,
                "process_instance_identity": 0,
                "outer_elapsed_nanoseconds": 0,
                "process_lifetime_peak_host_bytes": 0,
                "raw_wait_status": 0,
                "process_exit_code": -1,
                "terminating_signal": 0,
                "watchdog_kill_sent": False,
                "controller_invariant_id": "P4HARNESS-LAUNCH-001" if failed else "",
                "controller_detail": "synthetic pre-dispatch launch failure" if failed else "",
                "record": None,
                "child_failure": None,
            }
        )
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
    pair["result"] = None
    pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    result["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(result)
    result["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(result)
    return result


def _telemetry_leaf(
    semantics: Mapping[str, Any],
    roster: tuple[tuple[int, int], ...],
) -> dict[str, object]:
    per_net = [
        {
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
        }
        for net_id, generation in roster
    ]
    result: dict[str, object] = {
        "schema_version": 1,
        "associated_semantic_checksum": semantics["semantic_checksum"],
        "outcome": copy.deepcopy(semantics["outcome"]),
        "per_net": per_net,
        "telemetry_checksum": 0,
    }
    result["telemetry_checksum"] = telemetry_validator.compute_telemetry_checksum(result)
    return result


def _arm_capture(
    raw_arm: Mapping[str, Any],
    roster: tuple[tuple[int, int], ...],
) -> dict[str, object]:
    record = raw_arm["record"]
    result: dict[str, object] = {
        "schema_version": 1,
        "arm": raw_arm["arm"],
        "repetition_index": raw_arm["repetition_index"],
        "execution_order": raw_arm["execution_order"],
        "dispatch_ordinal": raw_arm["dispatch_ordinal"],
        "process_instance_identity": raw_arm["process_instance_identity"],
        "associated_semantic_checksum": record["semantics"]["semantic_checksum"],
        "associated_arm_artifact_checksum": record["artifact_checksum"],
        "associated_authority_checksum": record["external_observation"]["authority_checksum"],
        "associated_arm_attempt_checksum": raw_arm["attempt_checksum"],
        "telemetry": _telemetry_leaf(record["semantics"], roster),
        "capture_checksum": 0,
    }
    result["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(result)
    return result


def make_sidecar(raw: Mapping[str, Any]) -> dict[str, object]:
    """Build the matching synthetic telemetry companion for one same-run Raw cell."""
    case_id = raw["config"]["case_id"]
    _, roster = corpus_v2.validated_successful_case_roster(case_id)
    attempts: list[dict[str, object]] = []
    for pair in raw["attempts"]:
        capture: dict[str, object] = {
            "schema_version": 1,
            "case_id": pair["case_id"],
            "requested_pool_size": pair["requested_pool_size"],
            "repetition_index": pair["repetition_index"],
            "root_seed": pair["root_seed"],
            "execution_order": pair["execution_order"],
            "associated_raw_pair_attempt_checksum": pair["attempt_checksum"],
            "associated_paired_semantic_checksum": pair["result"]["semantic_checksum"],
            "associated_paired_artifact_checksum": pair["result"]["artifact_checksum"],
            "baseline": _arm_capture(pair["baseline"], roster),
            "candidate": _arm_capture(pair["candidate"], roster),
            "capture_checksum": 0,
        }
        capture["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(capture)
        attempts.append(capture)
    result: dict[str, object] = {
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "raw_evidence_schema_version": 2,
        "raw_wire_schema_version": 2,
        "telemetry_wire_schema_version": 2,
        "config": copy.deepcopy(raw["config"]),
        "corpus_checksum": raw["corpus_checksum"],
        "raw_cell_plan_checksum": raw["cell_plan_checksum"],
        "raw_environment_checksum": raw["environment"]["environment_checksum"],
        "raw_authority_run_identity": raw["authority_run_identity"],
        "raw_controller_identity": raw["controller_identity"],
        "raw_cell_artifact_checksum": raw["artifact_checksum"],
        "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        "attempts": attempts,
        "artifact_checksum": 0,
    }
    result["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(result)
    result["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
        result
    )
    return result
