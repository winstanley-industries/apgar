"""Validate and publish joined Phase 4 operational measurement evidence."""

from __future__ import annotations

import argparse
import copy
import json
import os
import pathlib
import secrets
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import capture_phase4_operational_measurement as capture_tool
from tools import project_phase4_operational_evidence as projection_v1
from tools import project_phase4_operational_evidence_v2 as projection_v2
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator
from tools import validate_phase4_statistical_protocol_v4 as protocol_v4

_MAXIMUM_CAPTURE_BYTES = 32 * 1024 * 1024
_MAXIMUM_PUBLICATION_BYTES = 32 * 1024 * 1024
_MAXIMUM_DEPTH = 96
_U64_MAX = (1 << 64) - 1
_CAPTURE_FIELDS = (
    "schema_version",
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "standalone_publication_eligible",
    "cell_operational_capture_complete",
    "controller_identity",
    "capture_run_identity",
    "cell_config",
    "reproducibility_provenance",
    "arms",
    "artifact_checksum",
    "source_envelope_checksum",
)
_PROCESS_FIELDS = (
    "schema_version",
    "role",
    "controller_identity",
    "dispatch_ordinal",
    "process_instance_identity",
    "configured_address_space_limit_bytes",
    "outer_wall_nanoseconds",
    "user_cpu_nanoseconds",
    "system_cpu_nanoseconds",
    "total_cpu_nanoseconds",
    "peak_host_bytes",
    "raw_wait_status",
    "process_exit_code",
    "terminating_signal",
    "watchdog_kill_sent",
    "isolated_exec",
    "measurement_scope",
)
_AUTHORITY_PROCESS_FIELDS = (
    "schema_version",
    "role",
    "controller_identity",
    "dispatch_ordinal",
    "process_instance_identity",
    "configured_address_space_limit_bytes",
    "raw_wait_status",
    "process_exit_code",
    "terminating_signal",
    "watchdog_kill_sent",
    "isolated_exec",
    "resource_measurements",
)
_ARM_FIELDS = (
    "arm",
    "measured_process",
    "measured_worker",
    "authority_process",
    "authority_worker",
)
_EXECUTION_FIELDS = (
    "semantics",
    "case_build_elapsed_nanoseconds",
    "prepared_elapsed_nanoseconds",
    "cold_elapsed_nanoseconds",
    "preparer_lifecycle",
)
_LIFECYCLE_FIELDS = (
    "workers_started_before",
    "workers_started_after",
    "invocations_started_before",
    "invocations_started_after",
    "invocations_completed_before",
    "invocations_completed_after",
)
_PROFILE_FIELDS = (
    "schema_version",
    "execution",
    "case_build",
    "process_cpu",
    "peak_host_memory",
    "compatible_batch_formation_and_fill",
    "compact_readback",
    "prepared_view_cache_and_cache_misses",
    "initial_device_upload",
    "gpu_utilization",
    "peak_device_memory",
    "contender_transient_release_tail_wall_nanoseconds",
    "unclassified_prepared_scope_wall_nanoseconds",
    "unclassified_cold_scope_exit_wall_nanoseconds",
    "baseline",
    "preparation",
    "candidate_session",
    "profile_checksum",
)
_WITNESS_FIELDS = (
    "session_checksum",
    "board_content_hash",
    "workload_checksum",
    "capacity_model_checksum",
    "preparation_checksum",
    "maximum_regeneration_epochs",
    "terminal_reason",
    "counters",
    "epoch_record_count",
    "epoch_association_checksum",
    "final_pool_manifest_checksum",
    "final_rejection_manifest_checksum",
)
_SESSION_COUNTER_FIELDS = (
    "completed_regeneration_epochs",
    "planning_expanded_resource_visits",
    "requested_columns",
    "route_queries",
    "route_work_units",
    "policy_projection_visits",
    "generated_candidate_bytes",
    "rejection_record_bytes",
    "transient_result_bytes",
    "admitted_candidates",
    "duplicate_candidates",
    "rejected_columns",
    "novel_retained_candidates",
    "changed_selections",
)
_EXECUTION_COUNTER_FIELDS = (
    "requested_columns",
    "route_queries",
    "route_work_units",
    "policy_projection_visits",
    "peak_route_record_count",
    "peak_route_queue_size",
    "generated_candidate_bytes",
    "rejection_record_bytes",
    "transient_result_bytes",
    "successful_routes",
    "built_candidates",
    "admitted_candidates",
    "duplicate_candidates",
    "rejected_columns",
    "novel_retained_candidates",
    "changed_selections",
    "successor_pinned_candidates",
)
_AUTHORITY_FIELDS = (
    "schema_version",
    "semantics",
    "preparer_lifecycle",
    "recomputed_full_preimage_session_checksum",
    "candidate_session_witness",
    "authority_checksum",
)


class EvidenceError(ValueError):
    """Stable operational-publication diagnostic."""


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise EvidenceError(f"non-finite JSON number: {value}")


def _check_depth(root: Any) -> None:
    stack = [(root, 1)]
    while stack:
        value, depth = stack.pop()
        if depth > _MAXIMUM_DEPTH:
            raise EvidenceError(f"JSON nesting exceeds {_MAXIMUM_DEPTH} levels")
        if isinstance(value, dict):
            stack.extend((child, depth + 1) for child in value.values())
        elif isinstance(value, list):
            stack.extend((child, depth + 1) for child in value)


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be an object")
    return value


def _array(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be an array")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value) != tuple(expected):
        raise EvidenceError(f"{label} has missing, extra, or reordered fields")


def _u64(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= _U64_MAX:
        raise EvidenceError(f"{label} must be a u64")
    return value


def _u32(value: Any, label: str) -> int:
    result = _u64(value, label)
    if result > 0xFFFF_FFFF:
        raise EvidenceError(f"{label} must be a u32")
    return result


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise EvidenceError(f"{label} must be boolean")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise EvidenceError(f"{label} must be a string")
    return value


def _enum(value: Any, allowed: set[int], label: str) -> int:
    result = _u32(value, label)
    if result not in allowed:
        raise EvidenceError(f"{label} is outside its enum")
    return result


def _lifecycle(value: Any, label: str) -> Mapping[str, int]:
    result = _object(value, label)
    _fields(result, _LIFECYCLE_FIELDS, label)
    for field in _LIFECYCLE_FIELDS:
        _u64(result[field], f"{label}.{field}")
    return result  # type: ignore[return-value]


def _applicability(value: Any, label: str, expected_status: int, expected_reason: int) -> None:
    result = _object(value, label)
    _fields(result, ("status", "reason"), label)
    if (
        _enum(result["status"], {0, 1, 2}, f"{label}.status") != expected_status
        or _enum(result["reason"], set(range(12)), f"{label}.reason") != expected_reason
    ):
        raise EvidenceError(f"{label} does not carry the frozen status/reason pair")


def _session_counters(value: Any, label: str) -> Mapping[str, int]:
    result = _object(value, label)
    _fields(result, _SESSION_COUNTER_FIELDS, label)
    for field in _SESSION_COUNTER_FIELDS:
        _u64(result[field], f"{label}.{field}")
    return result  # type: ignore[return-value]


def _execution_counters(value: Any, label: str) -> Mapping[str, int]:
    result = _object(value, label)
    _fields(result, _EXECUTION_COUNTER_FIELDS, label)
    for field in _EXECUTION_COUNTER_FIELDS:
        _u64(result[field], f"{label}.{field}")
    return result  # type: ignore[return-value]


def _witness(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _WITNESS_FIELDS, label)
    for field in (
        "session_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
        "preparation_checksum",
        "epoch_record_count",
        "epoch_association_checksum",
        "final_pool_manifest_checksum",
        "final_rejection_manifest_checksum",
    ):
        _u64(result[field], f"{label}.{field}")
    _u32(result["maximum_regeneration_epochs"], f"{label}.maximum_regeneration_epochs")
    _enum(result["terminal_reason"], set(range(4)), f"{label}.terminal_reason")
    _session_counters(result["counters"], f"{label}.counters")
    return result


def _normalized_candidate_terminal_reason(reason: int) -> int:
    return (0, 2, 3, 4)[reason]


def _hash_witness(hashed: raw_validator.StableHashBuilder, witness: Mapping[str, Any]) -> None:
    for field in (
        "session_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
        "preparation_checksum",
    ):
        hashed.u64(witness[field])
    hashed.u32(witness["maximum_regeneration_epochs"])
    hashed.byte(witness["terminal_reason"])
    for field in _SESSION_COUNTER_FIELDS:
        hashed.u64(witness["counters"][field])
    for field in (
        "epoch_record_count",
        "epoch_association_checksum",
        "final_pool_manifest_checksum",
        "final_rejection_manifest_checksum",
    ):
        hashed.u64(witness[field])


def _epoch_association(session: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-CPU-CANDIDATE-ALLOCATION-EPOCH-ASSOCIATION-V1")
    epochs = session["regeneration_epochs"]
    hashed.u64(len(epochs))
    for epoch in epochs:
        hashed.u32(epoch["epoch_index"])
        hashed.u64(epoch["plan_checksum"])
        hashed.u64(epoch["execution_checksum"])
        for field in _EXECUTION_COUNTER_FIELDS:
            hashed.u64(epoch["counters"][field])
    hashed.u64(session["planning_expanded_resource_visits"])
    return hashed.finish()


def _profile_checksum(profile: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-TRIAL-ARM-OPERATIONAL-PROFILE-V1")
    hashed.u32(profile["schema_version"])
    semantics = profile["execution"]["semantics"]
    hashed.u64(raw_validator.compute_semantic_checksum(semantics))
    hashed.u64(semantics["semantic_checksum"])
    hashed.byte(semantics["execution_order"])
    hashed.u32(semantics["preparation_worker_count"])
    for field in (
        "case_build_elapsed_nanoseconds",
        "prepared_elapsed_nanoseconds",
        "cold_elapsed_nanoseconds",
    ):
        hashed.u64(profile["execution"][field])
    lifecycle = profile["execution"]["preparer_lifecycle"]
    for field in _LIFECYCLE_FIELDS:
        hashed.u64(lifecycle[field])
    built = profile["case_build"]
    hashed.byte(built["case_source"])
    for field in (
        "fixture_import_applicability",
        "synthetic_materialization_applicability",
        "compile_probe_applicability",
    ):
        hashed.byte(built[field]["status"])
        hashed.byte(built[field]["reason"])
    for field in (
        "descriptor_validation_and_bound_preflight_wall_nanoseconds",
        "fixture_identity_and_import_wall_nanoseconds",
        "synthetic_geometry_and_board_materialization_wall_nanoseconds",
        "geometry_compilation_probe_wall_nanoseconds",
        "workload_geometry_compilation_wall_nanoseconds",
        "capacity_and_case_assembly_wall_nanoseconds",
        "component_wall_nanoseconds",
        "unclassified_and_release_wall_nanoseconds",
    ):
        hashed.u64(built[field])
    for field in (
        "process_cpu",
        "peak_host_memory",
        "compatible_batch_formation_and_fill",
        "compact_readback",
        "prepared_view_cache_and_cache_misses",
        "initial_device_upload",
        "gpu_utilization",
        "peak_device_memory",
    ):
        hashed.byte(profile[field]["status"])
        hashed.byte(profile[field]["reason"])
    for field in (
        "contender_transient_release_tail_wall_nanoseconds",
        "unclassified_prepared_scope_wall_nanoseconds",
        "unclassified_cold_scope_exit_wall_nanoseconds",
    ):
        hashed.u64(profile[field])
    baseline = profile["baseline"]
    hashed.boolean(baseline is not None)
    if baseline is not None:
        for field in (
            "component_wall_nanoseconds",
            "validation_and_initialization_wall_nanoseconds",
            "scheduling_and_policy_projection_wall_nanoseconds",
            "candidate_generation_wall_nanoseconds",
            "exact_admission_and_store_publication_wall_nanoseconds",
            "incremental_resource_accumulation_wall_nanoseconds",
            "sweep_selection_and_resource_replay_wall_nanoseconds",
            "price_update_wall_nanoseconds",
            "final_assembly_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
        ):
            hashed.u64(baseline[field])
    preparation = profile["preparation"]
    hashed.boolean(preparation is not None)
    if preparation is not None:
        for field in (
            "component_wall_nanoseconds",
            "validation_and_scheduling_wall_nanoseconds",
            "base_worker_wave_wall_nanoseconds",
            "alternative_policy_wall_nanoseconds",
            "alternative_worker_wave_wall_nanoseconds",
            "route_and_candidate_build_worker_sum_nanoseconds",
            "exact_admission_and_store_publication_wall_nanoseconds",
            "publication_correlation_and_pool_materialization_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
            "base_jobs_dispatched",
            "alternative_jobs_dispatched",
        ):
            hashed.u64(preparation[field])
    session = profile["candidate_session"]
    hashed.boolean(session is not None)
    if session is not None:
        for field in (
            "component_wall_nanoseconds",
            "validation_and_source_inspection_wall_nanoseconds",
            "initial_price_state_wall_nanoseconds",
            "initial_selection_and_resource_accumulation_wall_nanoseconds",
            "targeted_regeneration_planning_wall_nanoseconds",
            "targeted_regeneration_price_update_wall_nanoseconds",
            "targeted_regeneration_selection_and_target_planning_wall_nanoseconds",
            "targeted_regeneration_execution_wall_nanoseconds",
            "successor_correlation_wall_nanoseconds",
            "terminal_multi_world_component_wall_nanoseconds",
            "terminal_multi_world_price_update_wall_nanoseconds",
            "final_manifest_and_assembly_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
        ):
            hashed.u64(session[field])
        hashed.u64(len(session["regeneration_plans"]))
        for plan in session["regeneration_plans"]:
            hashed.u32(plan["epoch_index"])
            for field in (
                "plan_checksum",
                "component_wall_nanoseconds",
                "source_selection_and_resource_accumulation_wall_nanoseconds",
                "price_update_wall_nanoseconds",
                "next_price_selection_and_resource_accumulation_wall_nanoseconds",
                "target_ranking_retention_and_assembly_wall_nanoseconds",
                "unclassified_serial_wall_nanoseconds",
                "price_update_operations",
            ):
                hashed.u64(plan[field])
        hashed.u64(len(session["regeneration_epochs"]))
        for epoch in session["regeneration_epochs"]:
            hashed.u32(epoch["epoch_index"])
            hashed.u64(epoch["plan_checksum"])
            hashed.u64(epoch["execution_checksum"])
            for field in _EXECUTION_COUNTER_FIELDS:
                hashed.u64(epoch["counters"][field])
            for field in (
                "component_wall_nanoseconds",
                "validation_and_preflight_wall_nanoseconds",
                "baseline_selection_and_source_store_preflight_wall_nanoseconds",
                "policy_projection_and_candidate_generation_wall_nanoseconds",
                "exact_admission_and_store_publication_wall_nanoseconds",
                "publication_correlation_wall_nanoseconds",
                "refreshed_selection_and_resource_accumulation_wall_nanoseconds",
                "successor_retention_wall_nanoseconds",
                "final_assembly_wall_nanoseconds",
                "unclassified_serial_wall_nanoseconds",
            ):
                hashed.u64(epoch[field])
        worlds = session["terminal_multi_world"]
        for field in (
            "component_wall_nanoseconds",
            "validation_and_source_preflight_wall_nanoseconds",
            "selection_and_resource_accumulation_wall_nanoseconds",
            "price_update_and_snapshot_wall_nanoseconds",
            "terminal_retention_and_assembly_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
        ):
            hashed.u64(worlds[field])
        hashed.u64(session["planning_expanded_resource_visits"])
        _hash_witness(hashed, session["replay_witness"])
    return hashed.finish()


def _profile(value: Any, label: str) -> Mapping[str, Any]:
    profile = _object(value, label)
    _fields(profile, _PROFILE_FIELDS, label)
    if _u32(profile["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    execution = _object(profile["execution"], f"{label}.execution")
    _fields(execution, _EXECUTION_FIELDS, f"{label}.execution")
    semantics = raw_validator._semantics(execution["semantics"], f"{label}.execution.semantics")
    lifecycle = _lifecycle(execution["preparer_lifecycle"], f"{label}.execution.preparer_lifecycle")
    for field in (
        "case_build_elapsed_nanoseconds",
        "prepared_elapsed_nanoseconds",
        "cold_elapsed_nanoseconds",
    ):
        _u64(execution[field], f"{label}.execution.{field}")
    case = _object(profile["case_build"], f"{label}.case_build")
    case_fields = (
        "case_source",
        "fixture_import_applicability",
        "synthetic_materialization_applicability",
        "compile_probe_applicability",
        "descriptor_validation_and_bound_preflight_wall_nanoseconds",
        "fixture_identity_and_import_wall_nanoseconds",
        "synthetic_geometry_and_board_materialization_wall_nanoseconds",
        "geometry_compilation_probe_wall_nanoseconds",
        "workload_geometry_compilation_wall_nanoseconds",
        "capacity_and_case_assembly_wall_nanoseconds",
        "component_wall_nanoseconds",
        "unclassified_and_release_wall_nanoseconds",
    )
    _fields(case, case_fields, f"{label}.case_build")
    imported = _enum(case["case_source"], {0, 1}, f"{label}.case_build.case_source") == 1
    if imported != (semantics["case_id"] == 4_000):
        raise EvidenceError(f"{label} case source differs from the frozen descriptor")
    if imported:
        _applicability(case["fixture_import_applicability"], f"{label}.case_build.fixture", 0, 0)
        _applicability(
            case["synthetic_materialization_applicability"],
            f"{label}.case_build.synthetic",
            1,
            8,
        )
        _applicability(case["compile_probe_applicability"], f"{label}.case_build.probe", 1, 9)
    else:
        _applicability(case["fixture_import_applicability"], f"{label}.case_build.fixture", 1, 7)
        _applicability(
            case["synthetic_materialization_applicability"],
            f"{label}.case_build.synthetic",
            0,
            0,
        )
        _applicability(case["compile_probe_applicability"], f"{label}.case_build.probe", 0, 0)
    for field in case_fields[4:]:
        _u64(case[field], f"{label}.case_build.{field}")
    _applicability(profile["process_cpu"], f"{label}.process_cpu", 2, 10)
    _applicability(profile["peak_host_memory"], f"{label}.peak_host_memory", 2, 11)
    _applicability(profile["compatible_batch_formation_and_fill"], f"{label}.batch", 1, 1)
    _applicability(profile["compact_readback"], f"{label}.readback", 1, 2)
    _applicability(profile["prepared_view_cache_and_cache_misses"], f"{label}.cache", 1, 3)
    _applicability(profile["initial_device_upload"], f"{label}.upload", 1, 4)
    _applicability(profile["gpu_utilization"], f"{label}.gpu", 1, 5)
    _applicability(profile["peak_device_memory"], f"{label}.device_memory", 1, 6)
    for field in (
        "contender_transient_release_tail_wall_nanoseconds",
        "unclassified_prepared_scope_wall_nanoseconds",
        "unclassified_cold_scope_exit_wall_nanoseconds",
    ):
        _u64(profile[field], f"{label}.{field}")
    case_classified = sum(case[field] for field in case_fields[4:10])
    if (
        case_classified + case["unclassified_and_release_wall_nanoseconds"]
        != case["component_wall_nanoseconds"]
        or case["component_wall_nanoseconds"] > execution["case_build_elapsed_nanoseconds"]
    ):
        raise EvidenceError(f"{label} case-build intervals do not close")
    nested_cold = (
        execution["case_build_elapsed_nanoseconds"] + execution["prepared_elapsed_nanoseconds"]
    )
    if (
        nested_cold > execution["cold_elapsed_nanoseconds"]
        or profile["unclassified_cold_scope_exit_wall_nanoseconds"]
        != execution["cold_elapsed_nanoseconds"] - nested_cold
    ):
        raise EvidenceError(f"{label} cold intervals do not close")
    budget = semantics["external_budget"]
    if (
        execution["prepared_elapsed_nanoseconds"] > budget["maximum_prepared_elapsed_nanoseconds"]
        or execution["cold_elapsed_nanoseconds"] > budget["maximum_cold_elapsed_nanoseconds"]
    ):
        raise EvidenceError(f"{label} exceeds its semantic timing budget")
    arm = semantics["arm"]
    if arm == 0:
        if lifecycle != {field: 0 for field in _LIFECYCLE_FIELDS}:
            raise EvidenceError(f"{label} baseline lifecycle must be zero")
        if (
            profile["baseline"] is None
            or profile["preparation"] is not None
            or profile["candidate_session"] is not None
        ):
            raise EvidenceError(f"{label} baseline component shape is invalid")
        baseline = _object(profile["baseline"], f"{label}.baseline")
        fields = (
            "component_wall_nanoseconds",
            "validation_and_initialization_wall_nanoseconds",
            "scheduling_and_policy_projection_wall_nanoseconds",
            "candidate_generation_wall_nanoseconds",
            "exact_admission_and_store_publication_wall_nanoseconds",
            "incremental_resource_accumulation_wall_nanoseconds",
            "sweep_selection_and_resource_replay_wall_nanoseconds",
            "price_update_wall_nanoseconds",
            "final_assembly_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
        )
        _fields(baseline, fields, f"{label}.baseline")
        for field in fields:
            _u64(baseline[field], f"{label}.baseline.{field}")
        if (
            sum(baseline[field] for field in fields[1:9]) + baseline[fields[9]]
            != baseline[fields[0]]
            or baseline[fields[0]]
            + profile["contender_transient_release_tail_wall_nanoseconds"]
            + profile["unclassified_prepared_scope_wall_nanoseconds"]
            != execution["prepared_elapsed_nanoseconds"]
        ):
            raise EvidenceError(f"{label} baseline timing does not close")
    else:
        if (
            lifecycle["workers_started_before"] != semantics["preparation_worker_count"]
            or lifecycle["workers_started_after"] != semantics["preparation_worker_count"]
            or lifecycle["invocations_started_before"] != lifecycle["invocations_completed_before"]
            or lifecycle["invocations_completed_before"] == 0
            or lifecycle["invocations_started_after"] != lifecycle["invocations_started_before"] + 1
            or lifecycle["invocations_completed_after"]
            != lifecycle["invocations_completed_before"] + 1
        ):
            raise EvidenceError(f"{label} candidate lifecycle is invalid")
        if (
            profile["baseline"] is not None
            or profile["preparation"] is None
            or profile["candidate_session"] is None
        ):
            raise EvidenceError(f"{label} candidate component shape is invalid")
        preparation = _object(profile["preparation"], f"{label}.preparation")
        preparation_fields = (
            "component_wall_nanoseconds",
            "validation_and_scheduling_wall_nanoseconds",
            "base_worker_wave_wall_nanoseconds",
            "alternative_policy_wall_nanoseconds",
            "alternative_worker_wave_wall_nanoseconds",
            "route_and_candidate_build_worker_sum_nanoseconds",
            "exact_admission_and_store_publication_wall_nanoseconds",
            "publication_correlation_and_pool_materialization_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
            "base_jobs_dispatched",
            "alternative_jobs_dispatched",
        )
        _fields(preparation, preparation_fields, f"{label}.preparation")
        for field in preparation_fields:
            _u64(preparation[field], f"{label}.preparation.{field}")
        if (
            sum(preparation[field] for field in preparation_fields[1:5])
            + preparation["exact_admission_and_store_publication_wall_nanoseconds"]
            + preparation["publication_correlation_and_pool_materialization_wall_nanoseconds"]
            + preparation["unclassified_serial_wall_nanoseconds"]
            != preparation["component_wall_nanoseconds"]
            or preparation["base_jobs_dispatched"] != semantics["workload_net_count"]
            or preparation["base_jobs_dispatched"] + preparation["alternative_jobs_dispatched"]
            != semantics["preparation_route_queries"]
            or preparation["alternative_jobs_dispatched"]
            > semantics["workload_net_count"] * (semantics["requested_pool_size"] - 1)
            or preparation["route_and_candidate_build_worker_sum_nanoseconds"]
            > semantics["preparation_worker_count"]
            * (
                preparation["base_worker_wave_wall_nanoseconds"]
                + preparation["alternative_worker_wave_wall_nanoseconds"]
            )
        ):
            raise EvidenceError(f"{label} candidate preparation does not close")
        _candidate_session(profile["candidate_session"], f"{label}.candidate_session", semantics)
        if (
            preparation["component_wall_nanoseconds"]
            + profile["candidate_session"]["component_wall_nanoseconds"]
            + profile["contender_transient_release_tail_wall_nanoseconds"]
            + profile["unclassified_prepared_scope_wall_nanoseconds"]
            != execution["prepared_elapsed_nanoseconds"]
        ):
            raise EvidenceError(f"{label} candidate prepared scope does not close")
    checksum = _u64(profile["profile_checksum"], f"{label}.profile_checksum")
    if checksum == 0 or checksum != _profile_checksum(profile):
        raise EvidenceError(f"{label}.profile_checksum does not authenticate the profile")
    return profile


def _candidate_session(value: Any, label: str, semantics: Mapping[str, Any]) -> Mapping[str, Any]:
    session = _object(value, label)
    scalar_fields = (
        "component_wall_nanoseconds",
        "validation_and_source_inspection_wall_nanoseconds",
        "initial_price_state_wall_nanoseconds",
        "initial_selection_and_resource_accumulation_wall_nanoseconds",
        "targeted_regeneration_planning_wall_nanoseconds",
        "targeted_regeneration_price_update_wall_nanoseconds",
        "targeted_regeneration_selection_and_target_planning_wall_nanoseconds",
        "targeted_regeneration_execution_wall_nanoseconds",
        "successor_correlation_wall_nanoseconds",
        "terminal_multi_world_component_wall_nanoseconds",
        "terminal_multi_world_price_update_wall_nanoseconds",
        "final_manifest_and_assembly_wall_nanoseconds",
        "unclassified_serial_wall_nanoseconds",
        "planning_expanded_resource_visits",
    )
    fields = scalar_fields + (
        "regeneration_plans",
        "regeneration_epochs",
        "terminal_multi_world",
        "replay_witness",
    )
    _fields(session, fields, label)
    for field in scalar_fields:
        _u64(session[field], f"{label}.{field}")
    plans = _array(session["regeneration_plans"], f"{label}.regeneration_plans")
    epochs = _array(session["regeneration_epochs"], f"{label}.regeneration_epochs")
    if len(plans) != len(epochs):
        raise EvidenceError(f"{label} plan/epoch cardinality differs")
    planning_price = planning_selection = planning_components = execution_components = 0
    sums = {field: 0 for field in _SESSION_COUNTER_FIELDS[2:]}
    plan_fields = (
        "epoch_index",
        "plan_checksum",
        "component_wall_nanoseconds",
        "source_selection_and_resource_accumulation_wall_nanoseconds",
        "price_update_wall_nanoseconds",
        "next_price_selection_and_resource_accumulation_wall_nanoseconds",
        "target_ranking_retention_and_assembly_wall_nanoseconds",
        "unclassified_serial_wall_nanoseconds",
        "price_update_operations",
    )
    epoch_fields = (
        "epoch_index",
        "plan_checksum",
        "execution_checksum",
        "counters",
        "component_wall_nanoseconds",
        "validation_and_preflight_wall_nanoseconds",
        "baseline_selection_and_source_store_preflight_wall_nanoseconds",
        "policy_projection_and_candidate_generation_wall_nanoseconds",
        "exact_admission_and_store_publication_wall_nanoseconds",
        "publication_correlation_wall_nanoseconds",
        "refreshed_selection_and_resource_accumulation_wall_nanoseconds",
        "successor_retention_wall_nanoseconds",
        "final_assembly_wall_nanoseconds",
        "unclassified_serial_wall_nanoseconds",
    )
    for index, (plan_value, epoch_value) in enumerate(zip(plans, epochs, strict=True)):
        plan = _object(plan_value, f"{label}.regeneration_plans[{index}]")
        _fields(plan, plan_fields, f"{label}.regeneration_plans[{index}]")
        for field in plan_fields:
            _u64(plan[field], f"{label}.regeneration_plans[{index}].{field}")
        if (
            plan["epoch_index"] != index
            or plan["plan_checksum"] == 0
            or plan["price_update_operations"] != 1
            or sum(plan[field] for field in plan_fields[3:7])
            + plan["unclassified_serial_wall_nanoseconds"]
            != plan["component_wall_nanoseconds"]
        ):
            raise EvidenceError(f"{label} planning row {index} is invalid")
        planning_price += plan["price_update_wall_nanoseconds"]
        planning_selection += (
            plan["source_selection_and_resource_accumulation_wall_nanoseconds"]
            + plan["next_price_selection_and_resource_accumulation_wall_nanoseconds"]
            + plan["target_ranking_retention_and_assembly_wall_nanoseconds"]
        )
        planning_components += plan["component_wall_nanoseconds"]
        epoch = _object(epoch_value, f"{label}.regeneration_epochs[{index}]")
        _fields(epoch, epoch_fields, f"{label}.regeneration_epochs[{index}]")
        for field in epoch_fields[:3] + epoch_fields[4:]:
            _u64(epoch[field], f"{label}.regeneration_epochs[{index}].{field}")
        counters = _execution_counters(
            epoch["counters"], f"{label}.regeneration_epochs[{index}].counters"
        )
        if (
            epoch["epoch_index"] != index
            or epoch["plan_checksum"] == 0
            or epoch["plan_checksum"] != plan["plan_checksum"]
            or epoch["execution_checksum"] == 0
            or sum(epoch[field] for field in epoch_fields[5:13])
            + epoch["unclassified_serial_wall_nanoseconds"]
            != epoch["component_wall_nanoseconds"]
        ):
            raise EvidenceError(f"{label} execution row {index} is invalid")
        execution_components += epoch["component_wall_nanoseconds"]
        for field in sums:
            if field in counters:
                sums[field] += counters[field]
    worlds = _object(session["terminal_multi_world"], f"{label}.terminal_multi_world")
    world_fields = (
        "component_wall_nanoseconds",
        "validation_and_source_preflight_wall_nanoseconds",
        "selection_and_resource_accumulation_wall_nanoseconds",
        "price_update_and_snapshot_wall_nanoseconds",
        "terminal_retention_and_assembly_wall_nanoseconds",
        "unclassified_serial_wall_nanoseconds",
    )
    _fields(worlds, world_fields, f"{label}.terminal_multi_world")
    for field in world_fields:
        _u64(worlds[field], f"{label}.terminal_multi_world.{field}")
    if sum(worlds[field] for field in world_fields[1:]) != worlds[world_fields[0]]:
        raise EvidenceError(f"{label} multi-world timing does not close")
    witness = _witness(session["replay_witness"], f"{label}.replay_witness")
    counters = witness["counters"]
    expected = {
        "session_checksum": semantics["algorithm_session_checksum"],
        "board_content_hash": semantics["board_content_hash"],
        "workload_checksum": semantics["workload_checksum"],
        "capacity_model_checksum": semantics["capacity_model_checksum"],
        "preparation_checksum": semantics["preparation_checksum"],
        "maximum_regeneration_epochs": semantics["candidate_regeneration_epochs"],
        "route_queries": semantics["regeneration_route_queries"],
        "route_work_units": semantics["regeneration_route_work_units"],
        "final_pool_manifest_checksum": semantics["final_pool_manifest_checksum"],
        "final_rejection_manifest_checksum": semantics["final_rejection_manifest_checksum"],
    }
    if (
        any(
            (witness[field] if field in witness else counters[field]) != expected_value
            for field, expected_value in expected.items()
        )
        or _normalized_candidate_terminal_reason(witness["terminal_reason"])
        != semantics["terminal_reason"]
    ):
        raise EvidenceError(f"{label} witness does not bind its semantics")
    if (
        witness["epoch_record_count"] != len(epochs)
        or counters["completed_regeneration_epochs"] != len(epochs)
        or witness["epoch_record_count"] > witness["maximum_regeneration_epochs"]
        or witness["epoch_association_checksum"] == 0
        or witness["epoch_association_checksum"] != _epoch_association(session)
        or session["planning_expanded_resource_visits"]
        != counters["planning_expanded_resource_visits"]
        or planning_price != session["targeted_regeneration_price_update_wall_nanoseconds"]
        or planning_selection
        != session["targeted_regeneration_selection_and_target_planning_wall_nanoseconds"]
        or planning_components > session["targeted_regeneration_planning_wall_nanoseconds"]
        or execution_components > session["targeted_regeneration_execution_wall_nanoseconds"]
        or worlds["component_wall_nanoseconds"]
        > session["terminal_multi_world_component_wall_nanoseconds"]
        or worlds["price_update_and_snapshot_wall_nanoseconds"]
        != session["terminal_multi_world_price_update_wall_nanoseconds"]
    ):
        raise EvidenceError(f"{label} nested authority or timing does not close")
    for field in sums:
        if sums[field] != counters[field]:
            raise EvidenceError(f"{label} epoch counter {field} does not close")
    session_classified = sum(
        session[field]
        for field in (
            "validation_and_source_inspection_wall_nanoseconds",
            "initial_price_state_wall_nanoseconds",
            "initial_selection_and_resource_accumulation_wall_nanoseconds",
            "targeted_regeneration_planning_wall_nanoseconds",
            "targeted_regeneration_execution_wall_nanoseconds",
            "successor_correlation_wall_nanoseconds",
            "terminal_multi_world_component_wall_nanoseconds",
            "final_manifest_and_assembly_wall_nanoseconds",
            "unclassified_serial_wall_nanoseconds",
        )
    )
    if session_classified != session["component_wall_nanoseconds"]:
        raise EvidenceError(f"{label} top-level session timing does not close")
    return session


def _authority_checksum(authority: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-TRIAL-ARM-REPLAY-AUTHORITY-V1")
    hashed.u32(authority["schema_version"])
    semantics = authority["semantics"]
    hashed.u64(raw_validator.compute_semantic_checksum(semantics))
    hashed.u64(semantics["semantic_checksum"])
    for field in _LIFECYCLE_FIELDS:
        hashed.u64(authority["preparer_lifecycle"][field])
    hashed.u64(authority["recomputed_full_preimage_session_checksum"])
    witness = authority["candidate_session_witness"]
    hashed.boolean(witness is not None)
    if witness is not None:
        _hash_witness(hashed, witness)
    return hashed.finish()


def _authority(value: Any, label: str) -> Mapping[str, Any]:
    authority = _object(value, label)
    _fields(authority, _AUTHORITY_FIELDS, label)
    if _u32(authority["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    semantics = raw_validator._semantics(authority["semantics"], f"{label}.semantics")
    lifecycle = _lifecycle(authority["preparer_lifecycle"], f"{label}.preparer_lifecycle")
    full = _u64(
        authority["recomputed_full_preimage_session_checksum"],
        f"{label}.recomputed_full_preimage_session_checksum",
    )
    if full == 0 or full != semantics["algorithm_session_checksum"]:
        raise EvidenceError(f"{label} full-preimage checksum does not bind semantics")
    witness_value = authority["candidate_session_witness"]
    if semantics["arm"] == 0:
        if witness_value is not None or lifecycle != {field: 0 for field in _LIFECYCLE_FIELDS}:
            raise EvidenceError(f"{label} baseline authority shape is invalid")
    else:
        if (
            lifecycle["workers_started_before"] != semantics["preparation_worker_count"]
            or lifecycle["workers_started_after"] != semantics["preparation_worker_count"]
            or lifecycle["invocations_started_before"] != lifecycle["invocations_completed_before"]
            or lifecycle["invocations_completed_before"] == 0
            or lifecycle["invocations_started_after"] != lifecycle["invocations_started_before"] + 1
            or lifecycle["invocations_completed_after"]
            != lifecycle["invocations_completed_before"] + 1
        ):
            raise EvidenceError(f"{label} candidate authority lifecycle is invalid")
        witness = _witness(witness_value, f"{label}.candidate_session_witness")
        if (
            witness["session_checksum"] != full
            or witness["board_content_hash"] != semantics["board_content_hash"]
            or witness["workload_checksum"] != semantics["workload_checksum"]
            or witness["capacity_model_checksum"] != semantics["capacity_model_checksum"]
            or witness["preparation_checksum"] != semantics["preparation_checksum"]
            or witness["maximum_regeneration_epochs"] != semantics["candidate_regeneration_epochs"]
            or _normalized_candidate_terminal_reason(witness["terminal_reason"])
            != semantics["terminal_reason"]
            or witness["counters"]["route_queries"] != semantics["regeneration_route_queries"]
            or witness["counters"]["route_work_units"] != semantics["regeneration_route_work_units"]
            or witness["final_pool_manifest_checksum"] != semantics["final_pool_manifest_checksum"]
            or witness["final_rejection_manifest_checksum"]
            != semantics["final_rejection_manifest_checksum"]
        ):
            raise EvidenceError(f"{label} candidate authority witness differs from semantics")
    checksum = _u64(authority["authority_checksum"], f"{label}.authority_checksum")
    if checksum == 0 or checksum != _authority_checksum(authority):
        raise EvidenceError(f"{label}.authority_checksum does not authenticate authority")
    return authority


def _process(value: Any, label: str, expected_role: str, controller: int) -> Mapping[str, Any]:
    process = _object(value, label)
    _fields(process, _PROCESS_FIELDS, label)
    if _u32(process["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    if _string(process["role"], f"{label}.role") != expected_role:
        raise EvidenceError(f"{label}.role differs")
    for field in (
        "controller_identity",
        "dispatch_ordinal",
        "process_instance_identity",
        "configured_address_space_limit_bytes",
        "outer_wall_nanoseconds",
        "user_cpu_nanoseconds",
        "system_cpu_nanoseconds",
        "total_cpu_nanoseconds",
        "peak_host_bytes",
        "raw_wait_status",
    ):
        _u64(process[field], f"{label}.{field}")
    _u32(process["process_exit_code"], f"{label}.process_exit_code")
    _u32(process["terminating_signal"], f"{label}.terminating_signal")
    _string(process["measurement_scope"], f"{label}.measurement_scope")
    if (
        process["controller_identity"] != controller
        or process["dispatch_ordinal"] == 0
        or process["process_instance_identity"] == 0
        or process["outer_wall_nanoseconds"] == 0
        or process["peak_host_bytes"] == 0
        or process["raw_wait_status"] != 0
        or process["total_cpu_nanoseconds"]
        != process["user_cpu_nanoseconds"] + process["system_cpu_nanoseconds"]
        or process["process_exit_code"] != 0
        or process["terminating_signal"] != 0
        or _boolean(process["watchdog_kill_sent"], f"{label}.watchdog_kill_sent")
        or not _boolean(process["isolated_exec"], f"{label}.isolated_exec")
        or process["measurement_scope"]
        != "controller_monotonic_fork_to_exact_wait4_reap_including_exec_setup_warmup_replay_serialization_release"
    ):
        raise EvidenceError(f"{label} is not a successful exact-child wait4 observation")
    return process


def _authority_process(
    value: Any, label: str, expected_role: str, controller: int
) -> Mapping[str, Any]:
    process = _object(value, label)
    _fields(process, _AUTHORITY_PROCESS_FIELDS, label)
    if _u32(process["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    if _string(process["role"], f"{label}.role") != expected_role:
        raise EvidenceError(f"{label}.role differs")
    for field in (
        "controller_identity",
        "dispatch_ordinal",
        "process_instance_identity",
        "configured_address_space_limit_bytes",
        "raw_wait_status",
    ):
        _u64(process[field], f"{label}.{field}")
    _u32(process["process_exit_code"], f"{label}.process_exit_code")
    _u32(process["terminating_signal"], f"{label}.terminating_signal")
    availability = _object(process["resource_measurements"], f"{label}.resource_measurements")
    _fields(availability, ("status", "reason"), f"{label}.resource_measurements")
    if (
        process["controller_identity"] != controller
        or process["dispatch_ordinal"] == 0
        or process["process_instance_identity"] == 0
        or process["raw_wait_status"] != 0
        or process["process_exit_code"] != 0
        or process["terminating_signal"] != 0
        or _boolean(process["watchdog_kill_sent"], f"{label}.watchdog_kill_sent")
        or not _boolean(process["isolated_exec"], f"{label}.isolated_exec")
        or availability
        != {
            "status": "not_used",
            "reason": "unmeasured_full_preimage_authority_replay",
        }
    ):
        raise EvidenceError(f"{label} is not a clean unmeasured authority process")
    return process


def _capture_checksum(value: Mapping[str, Any]) -> int:
    return capture_tool._artifact_checksum(value)


def _capture_source_checksum(value: Mapping[str, Any]) -> int:
    return capture_tool._source_checksum(value)


def _provenance_availability(value: Any, label: str, *, status: str, reason: str) -> None:
    availability = _object(value, label)
    _fields(availability, ("status", "reason"), label)
    if availability != {"status": status, "reason": reason}:
        raise EvidenceError(f"{label} applicability is invalid")


def _cgroup_provenance(value: Any, label: str) -> None:
    availability = _object(value, label)
    status = availability.get("status")
    if status == "measured":
        _fields(availability, ("status", "value"), label)
        if not _string(availability["value"], f"{label}.value"):
            raise EvidenceError(f"{label}.value must be nonempty")
    elif status == "unavailable":
        _fields(availability, ("status", "reason"), label)
        if availability["reason"] != "cgroup_file_unavailable":
            raise EvidenceError(f"{label}.reason is invalid")
    else:
        raise EvidenceError(f"{label}.status is invalid")


def _canonical_cgroup_path(value: Any, label: str) -> str:
    path = _string(value, label)
    canonical = pathlib.PurePosixPath(path)
    if (
        not path.startswith("/")
        or str(canonical) != path
        or any(part in {"", ".", ".."} for part in canonical.parts[1:])
    ):
        raise EvidenceError(f"{label} must be a canonical absolute cgroup path")
    return path


def _cgroup_ancestry(value: Any, label: str, leaf_path: str) -> None:
    entries = _array(value, label)
    if not entries:
        raise EvidenceError(f"{label} must be nonempty")
    expected_path = pathlib.PurePosixPath(leaf_path)
    for index, item in enumerate(entries):
        entry_label = f"{label}[{index}]"
        entry = _object(item, entry_label)
        _fields(entry, ("path", "control"), entry_label)
        path = _canonical_cgroup_path(entry["path"], f"{entry_label}.path")
        if pathlib.PurePosixPath(path) != expected_path:
            raise EvidenceError(f"{label} is not the exact leaf-to-root ancestry")
        _cgroup_provenance(entry["control"], f"{entry_label}.control")
        if expected_path == pathlib.PurePosixPath("/"):
            if index + 1 != len(entries):
                raise EvidenceError(f"{label} continues above the cgroup root")
        else:
            expected_path = expected_path.parent
    if pathlib.PurePosixPath(entries[-1]["path"]) != pathlib.PurePosixPath("/"):
        raise EvidenceError(f"{label} does not terminate at the cgroup root")


def _filesystem_provenance_identity(value: Any, label: str) -> Mapping[str, Any]:
    identity = _object(value, label)
    _fields(identity, ("device", "inode", "symlink_target"), label)
    if (
        _u64(identity["device"], f"{label}.device") == 0
        or _u64(identity["inode"], f"{label}.inode") == 0
    ):
        raise EvidenceError(f"{label} device and inode must be nonzero")
    _string(identity["symlink_target"], f"{label}.symlink_target")
    return identity


def _provenance(value: Any, label: str, compiler_identity: str) -> Mapping[str, Any]:
    provenance = _object(value, label)
    fields = (
        "schema_version",
        "publication_invocation",
        "bazel_release",
        "worker_target",
        "worker_sha256",
        "worker_file_identity",
        "compiler_identity",
        "cplusplus_standard",
        "python_implementation",
        "python_version",
        "toolchain_files",
        "host",
        "backend",
        "gpu",
        "device_memory",
        "initial_device_upload",
        "compact_readback",
        "compatible_batch_fill",
        "prepared_view_cache",
        "provenance_checksum",
    )
    _fields(provenance, fields, label)
    if (
        _u32(provenance["schema_version"], f"{label}.schema_version") != 1
        or provenance["publication_invocation"]
        != "bazel --batch run --config=benchmark //:phase4_operational_capture"
        or provenance["worker_target"] != "//:phase4_operational_replay_worker"
        or provenance["cplusplus_standard"] != "c++20"
        or provenance["backend"] != "cpu_only"
        or provenance["compiler_identity"] != compiler_identity
    ):
        raise EvidenceError(f"{label} fixed toolchain or backend identity is invalid")
    for field in (
        "bazel_release",
        "worker_sha256",
        "compiler_identity",
        "python_implementation",
        "python_version",
    ):
        if not _string(provenance[field], f"{label}.{field}"):
            raise EvidenceError(f"{label}.{field} must be nonempty")
    hexadecimal = set("0123456789abcdef")
    if len(provenance["worker_sha256"]) != 64 or any(
        character not in hexadecimal for character in provenance["worker_sha256"]
    ):
        raise EvidenceError(f"{label}.worker_sha256 is invalid")
    worker_identity = _object(provenance["worker_file_identity"], f"{label}.worker_file_identity")
    _fields(
        worker_identity,
        ("device", "inode", "size_bytes", "mode", "mtime_nanoseconds", "sha256"),
        f"{label}.worker_file_identity",
    )
    worker_digest = _string(worker_identity["sha256"], f"{label}.worker_file_identity.sha256")
    worker_mode = _u64(worker_identity["mode"], f"{label}.worker_file_identity.mode")
    if (
        _u64(worker_identity["device"], f"{label}.worker_file_identity.device") > _U64_MAX
        or _u64(worker_identity["inode"], f"{label}.worker_file_identity.inode") == 0
        or _u64(worker_identity["size_bytes"], f"{label}.worker_file_identity.size_bytes") == 0
        or worker_mode > 0o7777
        or worker_mode & 0o111 == 0
        or _u64(
            worker_identity["mtime_nanoseconds"],
            f"{label}.worker_file_identity.mtime_nanoseconds",
        )
        > _U64_MAX
        or len(worker_digest) != 64
        or any(character not in hexadecimal for character in worker_digest)
        or worker_digest != provenance["worker_sha256"]
    ):
        raise EvidenceError(f"{label}.worker_file_identity is invalid")
    files = _array(provenance["toolchain_files"], f"{label}.toolchain_files")
    expected_names = (
        "bazel_version_file",
        "bazel_configuration",
        "module_definition",
        "module_lock",
    )
    if len(files) != len(expected_names):
        raise EvidenceError(f"{label}.toolchain_files is incomplete")
    for index, expected_name in enumerate(expected_names):
        file_value = _object(files[index], f"{label}.toolchain_files[{index}]")
        _fields(
            file_value,
            ("name", "sha256", "size_bytes"),
            f"{label}.toolchain_files[{index}]",
        )
        digest = _string(file_value["sha256"], f"{label}.toolchain_files[{index}].sha256")
        if (
            file_value["name"] != expected_name
            or len(digest) != 64
            or any(character not in hexadecimal for character in digest)
            or _u64(
                file_value["size_bytes"],
                f"{label}.toolchain_files[{index}].size_bytes",
            )
            == 0
        ):
            raise EvidenceError(f"{label}.toolchain_files[{index}] is invalid")
    host = _object(provenance["host"], f"{label}.host")
    host_fields = (
        "os",
        "kernel",
        "architecture",
        "os_release_id",
        "os_release_version",
        "os_release_sha256",
        "libc",
        "cpu_vendor",
        "cpu_model_name",
        "cpu_family",
        "cpu_model",
        "cpu_stepping",
        "cpu_microcode",
        "online_cpu_count",
        "affinity_cpu_ids",
        "affinity_cpu_count",
        "page_size_bytes",
        "total_host_memory_bytes",
        "cgroup_namespace_identity",
        "cgroup_mount_identity",
        "cgroup_ancestry_scope",
        "cgroup_unified_path",
        "cgroup_cpu_max_ancestry",
        "cgroup_cpuset_effective",
        "cgroup_memory_max_ancestry",
        "monotonic_clock",
    )
    _fields(host, host_fields, f"{label}.host")
    for field in (
        "os",
        "kernel",
        "architecture",
        "os_release_id",
        "os_release_version",
        "os_release_sha256",
        "cpu_vendor",
        "cpu_model_name",
        "cpu_family",
        "cpu_model",
        "cpu_stepping",
        "cpu_microcode",
        "monotonic_clock",
    ):
        _string(host[field], f"{label}.host.{field}")
    if (
        host["os"] != "Linux"
        or host["monotonic_clock"] != "CLOCK_MONOTONIC"
        or not host["kernel"]
        or not host["architecture"]
        or len(host["os_release_sha256"]) != 64
        or any(character not in hexadecimal for character in host["os_release_sha256"])
    ):
        raise EvidenceError(f"{label}.host fixed identity is invalid")
    libc = _array(host["libc"], f"{label}.host.libc")
    if len(libc) != 2:
        raise EvidenceError(f"{label}.host.libc must have implementation and version")
    for index, item in enumerate(libc):
        _string(item, f"{label}.host.libc[{index}]")
    online = _u64(host["online_cpu_count"], f"{label}.host.online_cpu_count")
    affinity = _array(host["affinity_cpu_ids"], f"{label}.host.affinity_cpu_ids")
    affinity_ids = [
        _u64(item, f"{label}.host.affinity_cpu_ids[{index}]") for index, item in enumerate(affinity)
    ]
    affinity_count = _u64(host["affinity_cpu_count"], f"{label}.host.affinity_cpu_count")
    if (
        online == 0
        or not affinity_ids
        or affinity_ids != sorted(set(affinity_ids))
        or affinity_count != len(affinity_ids)
        or affinity_count > online
        or _u64(host["page_size_bytes"], f"{label}.host.page_size_bytes") == 0
        or _u64(host["total_host_memory_bytes"], f"{label}.host.total_host_memory_bytes") == 0
    ):
        raise EvidenceError(f"{label}.host numeric topology is invalid")
    unified_path = _canonical_cgroup_path(
        host["cgroup_unified_path"], f"{label}.host.cgroup_unified_path"
    )
    namespace_identity = _filesystem_provenance_identity(
        host["cgroup_namespace_identity"],
        f"{label}.host.cgroup_namespace_identity",
    )
    mount_identity = _filesystem_provenance_identity(
        host["cgroup_mount_identity"],
        f"{label}.host.cgroup_mount_identity",
    )
    if (
        not namespace_identity["symlink_target"]
        or mount_identity["symlink_target"]
        or host["cgroup_ancestry_scope"] != "namespace_visible_unified_v2_leaf_to_root"
    ):
        raise EvidenceError(f"{label}.host cgroup namespace or mount scope is invalid")
    _cgroup_ancestry(
        host["cgroup_cpu_max_ancestry"],
        f"{label}.host.cgroup_cpu_max_ancestry",
        unified_path,
    )
    _cgroup_provenance(host["cgroup_cpuset_effective"], f"{label}.host.cgroup_cpuset_effective")
    _cgroup_ancestry(
        host["cgroup_memory_max_ancestry"],
        f"{label}.host.cgroup_memory_max_ancestry",
        unified_path,
    )
    for field, reason in (
        ("gpu", "cpu_execution_has_no_gpu_dispatch"),
        ("device_memory", "cpu_execution_has_no_device_memory"),
        ("initial_device_upload", "cpu_execution_has_no_device_upload"),
        ("compact_readback", "cpu_execution_has_no_compact_device_readback"),
        ("compatible_batch_fill", "cpu_direct_jobs_not_compatibility_batches"),
        (
            "prepared_view_cache",
            "precompiled_case_owned_views_have_no_runtime_prepared_view_cache",
        ),
    ):
        _provenance_availability(
            provenance[field],
            f"{label}.{field}",
            status="not_applicable",
            reason=reason,
        )
    checksum = _u64(provenance["provenance_checksum"], f"{label}.provenance_checksum")
    payload = {key: item for key, item in provenance.items() if key != "provenance_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-REPRODUCIBILITY-PROVENANCE-V1")
    hashed.string(_canonical(payload))
    if checksum == 0 or checksum != hashed.finish():
        raise EvidenceError(f"{label}.provenance_checksum is invalid")
    return provenance


def validate_capture(value: Any, *, expected_commit: str | None = None) -> Mapping[str, Any]:
    capture = _object(value, "capture")
    _check_depth(capture)
    _fields(capture, _CAPTURE_FIELDS, "capture")
    if _u32(capture["schema_version"], "capture.schema_version") != 1:
        raise EvidenceError("capture.schema_version must be 1")
    source_commit = _string(capture["source_commit"], "capture.source_commit")
    source_stamped = _boolean(capture["source_stamped"], "capture.source_stamped")
    source_dirty = _boolean(capture["source_tree_dirty"], "capture.source_tree_dirty")
    if expected_commit is not None and (
        source_commit != expected_commit or not source_stamped or source_dirty
    ):
        raise EvidenceError("capture is not from the independently expected clean commit")
    if _boolean(capture["standalone_publication_eligible"], "capture.standalone") or not _boolean(
        capture["cell_operational_capture_complete"], "capture.complete"
    ):
        raise EvidenceError("capture eligibility flags are invalid")
    controller = _u64(capture["controller_identity"], "capture.controller_identity")
    run = _u64(capture["capture_run_identity"], "capture.capture_run_identity")
    if controller == 0 or run == 0:
        raise EvidenceError("capture controller/run identities must be nonzero")
    raw_validator._config(capture["cell_config"], 20, 4)
    deadline = (
        capture["cell_config"]["maximum_setup_elapsed_nanoseconds"]
        + 2 * capture["cell_config"]["external_budget"]["maximum_cold_elapsed_nanoseconds"]
    )
    if deadline <= 0 or deadline > _U64_MAX:
        raise EvidenceError("capture per-process absolute deadline is invalid")
    arms = _array(capture["arms"], "capture.arms")
    if len(arms) != 2:
        raise EvidenceError("capture must contain baseline and candidate arms")
    process_ids: set[int] = set()
    compiler_identities: set[str] = set()
    for index, value_arm in enumerate(arms):
        label = f"capture.arms[{index}]"
        arm = _object(value_arm, label)
        _fields(arm, _ARM_FIELDS, label)
        if _enum(arm["arm"], {0, 1}, f"{label}.arm") != index:
            raise EvidenceError(f"{label}.arm order is invalid")
        measured_process = _process(
            arm["measured_process"],
            f"{label}.measured_process",
            f"measured_{'baseline' if index == 0 else 'candidate'}",
            controller,
        )
        authority_process = _authority_process(
            arm["authority_process"],
            f"{label}.authority_process",
            f"authority_{'baseline' if index == 0 else 'candidate'}",
            controller,
        )
        if (
            measured_process["dispatch_ordinal"] != index + 1
            or authority_process["dispatch_ordinal"] != index + 3
        ):
            raise EvidenceError(f"{label} dispatch order is invalid")
        process_ids.update(
            (
                measured_process["process_instance_identity"],
                authority_process["process_instance_identity"],
            )
        )
        measured_worker = capture_tool._parse_worker(
            (
                _canonical(_object(arm["measured_worker"], f"{label}.measured_worker")) + "\n"
            ).encode(),
            0,
        )
        authority_worker = capture_tool._parse_worker(
            (
                _canonical(_object(arm["authority_worker"], f"{label}.authority_worker")) + "\n"
            ).encode(),
            1,
        )
        for worker in (measured_worker, authority_worker):
            if (
                worker["source_commit"] != source_commit
                or worker["source_stamped"] != source_stamped
                or worker["source_tree_dirty"] != source_dirty
            ):
                raise EvidenceError(f"{label} worker source differs from capture")
            compiler_identities.add(worker["compiler_identity"])
        profile = _profile(measured_worker["payload"], f"{label}.profile")
        authority = _authority(authority_worker["payload"], f"{label}.authority")
        if profile["execution"]["semantics"] != authority["semantics"]:
            raise EvidenceError(f"{label} measured and authority semantics differ")
        measured_witness = profile["candidate_session"]["replay_witness"] if index == 1 else None
        if measured_witness != authority["candidate_session_witness"]:
            raise EvidenceError(f"{label} measured and full-preimage witnesses differ")
        if (
            measured_process["configured_address_space_limit_bytes"]
            != capture["cell_config"]["external_budget"]["maximum_address_space_bytes"]
            or authority_process["configured_address_space_limit_bytes"]
            != capture["cell_config"]["external_budget"]["maximum_address_space_bytes"]
        ):
            raise EvidenceError(f"{label} address-space authority differs from cell config")
        if (
            measured_process["peak_host_bytes"]
            > capture["cell_config"]["external_budget"]["maximum_peak_host_bytes"]
        ):
            raise EvidenceError(f"{label} measured peak host bytes exceed the configured cap")
        if measured_process["outer_wall_nanoseconds"] > deadline:
            raise EvidenceError(f"{label} measured process exceeded the absolute deadline")
    if len(process_ids) != 4 or len(compiler_identities) != 1:
        raise EvidenceError("capture does not bind four distinct processes and one compiler")
    _provenance(
        capture["reproducibility_provenance"],
        "capture.reproducibility_provenance",
        next(iter(compiler_identities)),
    )
    artifact = _u64(capture["artifact_checksum"], "capture.artifact_checksum")
    source = _u64(capture["source_envelope_checksum"], "capture.source_envelope_checksum")
    if artifact == 0 or artifact != _capture_checksum(capture):
        raise EvidenceError("capture artifact checksum is invalid")
    if source == 0 or source != _capture_source_checksum(capture):
        raise EvidenceError("capture source envelope checksum is invalid")
    return capture


def read_capture(path: pathlib.Path, *, expected_commit: str | None = None) -> Mapping[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise EvidenceError(f"cannot read capture: {error}") from error
    if len(data) > _MAXIMUM_CAPTURE_BYTES:
        raise EvidenceError("capture exceeds 32 MiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise EvidenceError("capture must be UTF-8 with exactly one terminal LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise EvidenceError(f"cannot parse capture: {error}") from error
    validated = validate_capture(value, expected_commit=expected_commit)
    if text != _canonical(validated) + "\n":
        raise EvidenceError("capture is not canonical compact JSON")
    return validated


def _publication_checksum(value: Mapping[str, Any]) -> int:
    payload = {
        key: item
        for key, item in value.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _publication_source_checksum(value: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V1")
    hashed.u32(value["schema_version"])
    hashed.string(value["source_commit"])
    hashed.boolean(value["source_stamped"])
    hashed.boolean(value["source_tree_dirty"])
    hashed.u64(value["artifact_checksum"])
    return hashed.finish()


def _cell_role(case_id: int, pool: int) -> tuple[str, str]:
    protocol_v4.read_protocol()
    rows = {
        (cell_case, cell_pool): (role, evidence)
        for cell_case, cell_pool, role, evidence in protocol_v4.expanded_cells()
    }
    value = rows.get((case_id, pool))
    if value is None or value[1] not in {"raw_success", "same_run_raw_success"}:
        raise EvidenceError("cell is not a frozen executable operational-publication cell")
    return value


def project_document(
    raw: Mapping[str, Any],
    capture: Mapping[str, Any],
    sidecar: Mapping[str, Any] | None,
) -> dict[str, Any]:
    config = raw["config"]
    role, evidence = _cell_role(config["case_id"], config["requested_pool_size"])
    raw_v2 = raw.get("raw_evidence_schema_version") == 2
    if raw_v2 != (evidence == "same_run_raw_success"):
        raise EvidenceError("Raw authority version differs from the frozen cell disposition")
    if raw_v2 and sidecar is None:
        raise EvidenceError("Raw-v2 operational publication requires same-run telemetry")
    if not raw_v2 and sidecar is not None:
        raise EvidenceError("Raw-v1 operational publication rejects a same-run companion")
    if capture["cell_config"] != config:
        raise EvidenceError("operational capture config differs from Raw")
    if (
        capture["source_commit"] != raw["source_commit"]
        or capture["source_stamped"] != raw["source_stamped"]
        or capture["source_tree_dirty"] != raw["source_tree_dirty"]
    ):
        raise EvidenceError("operational capture and Raw source envelopes differ")
    pair = raw["attempts"][0]
    if pair["repetition_index"] != 0 or pair["execution_order"] != 0 or pair["result"] is None:
        raise EvidenceError("Raw repetition zero is not the canonical successful AB pair")
    if raw_v2:
        assert sidecar is not None
        legacy = projection_v2.project_document(raw, sidecar)
        raw_binding = {
            "kind": "same_run_raw_v2",
            "raw_evidence_schema_version": 2,
            "wire_schema_version": 2,
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
            "same_run_artifact_checksum": sidecar["artifact_checksum"],
            "same_run_source_envelope_checksum": sidecar["source_envelope_checksum"],
            "exact_rejection_guardrail_passed": (
                same_run_validator.exact_rejection_guardrail_passes(sidecar)
            ),
        }
    else:
        legacy = projection_v1.project_document(raw)
        raw_binding = {
            "kind": "raw_v1",
            "raw_evidence_schema_version": 1,
            "wire_schema_version": 1,
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
            "same_run": {
                "status": "not_applicable",
                "reason": "raw_v1_cell_has_no_same_run_companion",
            },
        }
    arms: list[dict[str, Any]] = []
    for index, key in enumerate(("baseline", "candidate")):
        capture_arm = capture["arms"][index]
        profile = capture_arm["measured_worker"]["payload"]
        authority = capture_arm["authority_worker"]["payload"]
        raw_attempt = pair[key]
        raw_semantics = pair["result"][key]["semantics"]
        if (
            profile["execution"]["semantics"] != raw_semantics
            or authority["semantics"] != raw_semantics
        ):
            raise EvidenceError(
                f"operational {key} replay semantics differ from Raw repetition zero"
            )
        measured = capture_arm["measured_process"]
        arms.append(
            {
                "arm": index,
                "raw_repetition_zero_binding": {
                    "pair_attempt_checksum": pair["attempt_checksum"],
                    "arm_attempt_checksum": raw_attempt["attempt_checksum"],
                    "arm_semantic_checksum": raw_semantics["semantic_checksum"],
                    "arm_record_artifact_checksum": pair["result"][key]["artifact_checksum"],
                    "external_authority_checksum": pair["result"][key]["external_observation"][
                        "authority_checksum"
                    ],
                },
                "measured_process_binding": {
                    "process_instance_identity": measured["process_instance_identity"],
                    "dispatch_ordinal": measured["dispatch_ordinal"],
                    "worker_artifact_checksum": capture_arm["measured_worker"]["artifact_checksum"],
                    "worker_source_envelope_checksum": capture_arm["measured_worker"][
                        "source_envelope_checksum"
                    ],
                },
                "authority_process_binding": {
                    "process_instance_identity": capture_arm["authority_process"][
                        "process_instance_identity"
                    ],
                    "dispatch_ordinal": capture_arm["authority_process"]["dispatch_ordinal"],
                    "worker_artifact_checksum": capture_arm["authority_worker"][
                        "artifact_checksum"
                    ],
                    "worker_source_envelope_checksum": capture_arm["authority_worker"][
                        "source_envelope_checksum"
                    ],
                    "recomputed_full_preimage_session_checksum": authority[
                        "recomputed_full_preimage_session_checksum"
                    ],
                },
                "process_cpu": {
                    "status": "measured",
                    "user_nanoseconds": measured["user_cpu_nanoseconds"],
                    "system_nanoseconds": measured["system_cpu_nanoseconds"],
                    "total_nanoseconds": measured["total_cpu_nanoseconds"],
                    "scope": measured["measurement_scope"],
                },
                "peak_host_memory": {
                    "status": "measured",
                    "bytes": measured["peak_host_bytes"],
                    "scope": measured["measurement_scope"],
                },
                "outer_wall": {
                    "status": "measured",
                    "nanoseconds": measured["outer_wall_nanoseconds"],
                    "scope": measured["measurement_scope"],
                },
                "operational_profile": copy.deepcopy(profile),
                "replay_authority_checksum": authority["authority_checksum"],
            }
        )
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "eligible_input_to_phase4_aggregation": True,
        "standalone_decision_eligible": False,
        "statistical_timing_eligible": False,
        "coverage_complete": False,
        "cell_operational_telemetry_complete": True,
        "cell_role": role,
        "raw_authority_binding": raw_binding,
        "legacy_operational_projection_binding": {
            "schema_version": legacy["schema_version"],
            "artifact_checksum": legacy["artifact_checksum"],
            "source_envelope_checksum": legacy["source_envelope_checksum"],
        },
        "cell_config": copy.deepcopy(config),
        "cell_identity": copy.deepcopy(legacy["cell_identity"]),
        "reproducibility_provenance": copy.deepcopy(capture["reproducibility_provenance"]),
        "capture_binding": {
            "controller_identity": capture["controller_identity"],
            "capture_run_identity": capture["capture_run_identity"],
            "capture_artifact_checksum": capture["artifact_checksum"],
            "capture_source_envelope_checksum": capture["source_envelope_checksum"],
        },
        "arms": arms,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = _publication_checksum(result)
    result["source_envelope_checksum"] = _publication_source_checksum(result)
    return result


def validate_publication(
    raw: Mapping[str, Any],
    capture: Mapping[str, Any],
    sidecar: Mapping[str, Any] | None,
    publication: Mapping[str, Any],
) -> None:
    expected = project_document(raw, capture, sidecar)
    projection_v1.assert_exact_projection(expected, publication)
    if publication["artifact_checksum"] != _publication_checksum(publication):
        raise EvidenceError("publication artifact checksum is invalid")
    if publication["source_envelope_checksum"] != _publication_source_checksum(publication):
        raise EvidenceError("publication source envelope checksum is invalid")


def read_publication(path: pathlib.Path) -> Mapping[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise EvidenceError(f"cannot read publication: {error}") from error
    if len(data) > _MAXIMUM_PUBLICATION_BYTES:
        raise EvidenceError("publication exceeds 32 MiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise EvidenceError("publication must be UTF-8 with exactly one terminal LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise EvidenceError(f"cannot parse publication: {error}") from error
    _check_depth(value)
    if not isinstance(value, dict) or text != _canonical(value) + "\n":
        raise EvidenceError("publication is not canonical compact JSON")
    return value


def _write_publication_no_replace(path: pathlib.Path, encoded: bytes) -> None:
    requested = path if path.is_absolute() else pathlib.Path.cwd() / path
    try:
        parent = requested.parent.resolve(strict=True)
    except OSError as error:
        raise EvidenceError("publication output parent is not a directory") from error
    if not parent.is_dir() or requested.name in {"", ".", ".."}:
        raise EvidenceError("publication output parent or name is invalid")
    destination = parent / requested.name
    temporary = parent / (f".{destination.name}.phase4-tmp-{os.getpid()}-{secrets.token_hex(8)}")
    descriptor: int | None = None
    directory_descriptor: int | None = None
    installed = False
    temporary_identity: tuple[int, int] | None = None
    try:
        directory_descriptor = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
            0o600,
        )
        offset = 0
        while offset < len(encoded):
            written = os.write(descriptor, encoded[offset:])
            if written <= 0:
                raise EvidenceError("publication output write made no progress")
            offset += written
        os.fsync(descriptor)
        os.close(descriptor)
        descriptor = None
        status = temporary.stat(follow_symlinks=False)
        temporary_identity = (status.st_dev, status.st_ino)
        os.link(temporary, destination)
        installed = True
        os.fsync(directory_descriptor)
        temporary.unlink()
        os.fsync(directory_descriptor)
    except FileExistsError as error:
        raise EvidenceError("publication output already exists") from error
    except OSError as error:
        rollback_error: OSError | None = None
        if installed and temporary_identity is not None:
            try:
                status = destination.stat(follow_symlinks=False)
                if (status.st_dev, status.st_ino) == temporary_identity:
                    destination.unlink()
                    installed = False
                    if directory_descriptor is not None:
                        try:
                            os.fsync(directory_descriptor)
                        except OSError as cleanup_error:
                            rollback_error = cleanup_error
            except FileNotFoundError:
                installed = False
            except OSError as cleanup_error:
                rollback_error = cleanup_error
        if rollback_error is not None:
            raise EvidenceError(
                "cannot atomically install publication output and rollback failed: "
                f"{rollback_error}"
            ) from error
        raise EvidenceError(f"cannot atomically install publication output: {error}") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if directory_descriptor is not None:
            os.close(directory_descriptor)
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        except OSError as cleanup_error:
            if installed:
                raise EvidenceError(
                    f"cannot remove private publication temporary: {cleanup_error}"
                ) from cleanup_error


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", type=pathlib.Path)
    parser.add_argument("--capture", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--validate", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        if options.validate is not None and options.output is not None:
            raise EvidenceError("--validate and --output are mutually exclusive")
        raw = raw_validator.read_document(options.raw)
        sidecar: Mapping[str, Any] | None = None
        if raw.get("raw_evidence_schema_version") == 2:
            if options.same_run_telemetry is None:
                raise EvidenceError("Raw-v2 requires --same-run-telemetry")
            sidecar = same_run_validator.read_document(options.same_run_telemetry)
            same_run_validator.validate_join(raw, sidecar, expected_commit=options.expected_commit)
        else:
            if options.same_run_telemetry is not None:
                raise EvidenceError("Raw-v1 rejects --same-run-telemetry")
            raw = raw_validator.read_validated_publication_document(
                options.raw, expected_commit=options.expected_commit
            )
        capture = read_capture(options.capture, expected_commit=options.expected_commit)
        if options.validate is None:
            encoded = _canonical(project_document(raw, capture, sidecar)) + "\n"
            encoded_bytes = encoded.encode("utf-8")
            if len(encoded_bytes) > _MAXIMUM_PUBLICATION_BYTES:
                raise EvidenceError("publication exceeds 32 MiB")
            if options.output is None:
                sys.stdout.write(encoded)
            else:
                _write_publication_no_replace(options.output, encoded_bytes)
        else:
            validate_publication(raw, capture, sidecar, read_publication(options.validate))
    except (
        CaptureError,
        EvidenceError,
        raw_validator.EvidenceError,
        protocol_v4.ProtocolV4Error,
        ValueError,
    ) as error:
        print(f"Phase 4 operational publication failed: {error}", file=sys.stderr)
        return 1
    if options.validate is not None:
        print("validated one Phase 4 operational measurement publication")
    return 0


CaptureError = capture_tool.CaptureError


if __name__ == "__main__":
    raise SystemExit(main())
