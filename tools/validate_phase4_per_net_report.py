"""Strict independent join for Phase 4 Raw v1 and per-net report companions."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_representative_manifest_v2 as authority_validator_v2
from tools import validate_phase4_workload_net_roster_manifest as roster_validator
from tools.phase4_bounded_json_input import read_regular_file

_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_MAX_REPORT_BYTES = 32 * 1024 * 1024
_MAX_NETS = 4096
_MAXIMUM_JSON_NESTING_DEPTH = 64
_OVERLAP_PPM = 1_000_000
_MAXIMUM_WATCHDOG_NANOSECONDS = 24 * 60 * 60 * 1_000_000_000
_COMMIT = re.compile(r"[0-9a-f]{40}")
_ROOT = pathlib.Path(__file__).resolve().parent.parent
_ROSTER_MANIFEST = _ROOT / "schemas/benchmark/phase4_workload_net_roster_manifest_v1.json"
_REPRESENTATIVE_MANIFEST = _ROOT / "schemas/benchmark/phase4_representative_manifest_v1.json"

_TOP_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "raw_wire_schema_version",
    "config",
    "corpus_checksum",
    "raw_cell_plan_checksum",
    "raw_cell_artifact_checksum",
    "raw_source_envelope_checksum",
    "raw_reference",
    "decision_eligible",
    "workload_net_roster_checksum",
    "arms",
    "artifact_checksum",
)
_RAW_REFERENCE_FIELDS = (
    "repetition_index",
    "execution_order",
    "pair_attempt_checksum",
    "paired_semantic_checksum",
    "paired_artifact_checksum",
    "baseline_semantic_checksum",
    "baseline_arm_artifact_checksum",
    "candidate_semantic_checksum",
    "candidate_arm_artifact_checksum",
)
_ARM_FIELDS = ("arm", "raw_semantic_checksum", "diagnostic")
_DIAGNOSTIC_FIELDS = ("semantics", "telemetry")
_TELEMETRY_FIELDS = (
    "schema_version",
    "associated_semantic_checksum",
    "per_net",
    "telemetry_checksum",
)
_PER_NET_FIELDS = (
    "schema_version",
    "net",
    "columns",
    "final_pool_size",
    "unique_geometry_signature_count",
    "unique_resource_signature_count",
    "candidate_pair_count",
    "mean_resource_overlap_ppm",
    "minimum_resource_overlap_ppm",
    "mean_geometric_overlap_ppm",
    "minimum_geometric_overlap_ppm",
    "selected_status",
    "selected_candidate_id",
    "selected_candidate_payload_checksum",
    "selected_candidate_metrics",
    "pool_best_intrinsic_cost",
)
_NET_FIELDS = ("id", "generation")
_COLUMN_FIELDS = (
    "requested_columns",
    "executed_route_queries",
    "admitted_candidates",
    "duplicate_candidates",
    "disconnected_columns",
    "unsupported_columns",
    "skipped_columns",
    "exact_validation_rejections",
    "other_rejections",
)
_CANDIDATE_ID_FIELDS = ("high", "low")
_METRICS_FIELDS = (
    "scalar_policy_cost",
    "intrinsic_base_cost",
    "orthogonal_step_count",
    "diagonal_step_count",
    "bend_count",
    "line_primitive_count",
    "via_count",
    "axis_aligned_length_dbu",
    "diagonal_projection_dbu",
)
_BUDGET_FIELDS = (
    "maximum_prepared_elapsed_nanoseconds",
    "maximum_cold_elapsed_nanoseconds",
    "maximum_address_space_bytes",
    "maximum_peak_host_bytes",
)
_LIMIT_FIELDS = (
    "maximum_nets",
    "maximum_compiled_nodes",
    "maximum_compiled_host_bytes",
    "maximum_active_regions",
    "maximum_board_entities",
)
_MAXIMUM_CORPUS_LIMITS = {
    "maximum_nets": 4_096,
    "maximum_compiled_nodes": 100_000_000,
    "maximum_compiled_host_bytes": 8 * 1024 * 1024 * 1024,
    "maximum_active_regions": 1_000_000,
    "maximum_board_entities": 1_000_000,
}
_CONFIG_FIELDS = (
    "schema_version",
    "case_id",
    "requested_pool_size",
    "preparation_worker_count",
    "repetitions",
    "maximum_setup_elapsed_nanoseconds",
    "external_budget",
    "corpus_limits",
)
_OPPORTUNITY_FIELDS = ("route_queries", "route_work_units")
_OUTCOME_FIELDS = (
    "selected_net_count",
    "no_candidate_net_count",
    "overused_resource_count",
    "total_overuse_units",
    "total_intrinsic_cost",
    "world_checksum",
)
_SEMANTICS_FIELDS = (
    "schema_version",
    "arm",
    "execution_order",
    "corpus_version",
    "corpus_checksum",
    "case_id",
    "descriptor_fingerprint",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "capacity_model_checksum",
    "budget_checksum",
    "workload_net_count",
    "requested_pool_size",
    "repetition_index",
    "root_seed",
    "preparation_worker_count",
    "baseline_sweeps",
    "candidate_regeneration_epochs",
    "candidate_columns_per_epoch",
    "candidate_terminal_selection_rounds",
    "external_budget",
    "opportunity",
    "actual",
    "preparation_route_queries",
    "preparation_route_work_units",
    "regeneration_route_queries",
    "regeneration_route_work_units",
    "requested_columns",
    "admitted_candidates",
    "rejected_columns",
    "final_candidate_count",
    "preparation_checksum",
    "algorithm_session_checksum",
    "final_pool_manifest_checksum",
    "final_rejection_manifest_checksum",
    "terminal_reason",
    "candidate_outcome_source",
    "outcome",
    "semantic_checksum",
)


EvidenceError = raw_validator.EvidenceError


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be a JSON object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be a JSON array")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value) != tuple(expected):
        missing = sorted(set(expected) - set(value))
        extra = sorted(set(value) - set(expected))
        raise EvidenceError(
            f"{label} fields are missing, extra, or outside canonical order: "
            f"missing={missing}, extra={extra}"
        )


def _u32(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= _U32_MAX:
        raise EvidenceError(f"{label} must be an unsigned 32-bit integer")
    return value


def _u64(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= _U64_MAX:
        raise EvidenceError(f"{label} must be an unsigned 64-bit integer")
    return value


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise EvidenceError(f"{label} must be a boolean")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise EvidenceError(f"{label} must be a string")
    return value


def _enum(value: Any, allowed: set[int], label: str) -> int:
    result = _u32(value, label)
    if result not in allowed:
        raise EvidenceError(f"{label} has unknown enum value {result}")
    return result


def _reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _reject_non_json_constant(value: str) -> Any:
    raise EvidenceError(f"non-JSON numeric constant: {value}")


def _check_json_nesting(value: Any, label: str) -> None:
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > _MAXIMUM_JSON_NESTING_DEPTH:
            raise EvidenceError(
                f"{label} exceeds the {_MAXIMUM_JSON_NESTING_DEPTH}-level nesting bound"
            )
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def _read_canonical(path: pathlib.Path, label: str, maximum_bytes: int) -> Any:
    try:
        encoded = read_regular_file(path, maximum_bytes, label=label)
        if len(encoded) > maximum_bytes:
            raise EvidenceError(f"{label} exceeds the {maximum_bytes}-byte input bound")
        text = encoded.decode("utf-8")
        document = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_json_nesting(document, label)
        canonical = (
            json.dumps(
                document,
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
            + "\n"
        )
        if text != canonical:
            raise EvidenceError(f"{label} must be canonical one-line JSON followed by one LF")
        return document
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as error:
        if isinstance(error, EvidenceError):
            raise
        raise EvidenceError(f"cannot read {label} {path}: {error}") from error


def _frozen_roster_row(
    case_id: int,
    *,
    corpus_version: int = 1,
) -> tuple[Mapping[str, Any], tuple[tuple[int, int], ...]]:
    if corpus_version == 1:
        try:
            return roster_validator.validated_successful_case_roster(
                case_id, _ROSTER_MANIFEST, _REPRESENTATIVE_MANIFEST
            )
        except roster_validator.ManifestError as error:
            raise EvidenceError(
                f"frozen workload-net roster manifest is invalid: {error}"
            ) from error
    if corpus_version == 2:
        try:
            return authority_validator_v2.validated_successful_case_roster(case_id)
        except authority_validator_v2.AuthorityError as error:
            raise EvidenceError(
                f"frozen confirmatory workload-net roster manifest is invalid: {error}"
            ) from error
    raise EvidenceError(f"unsupported per-net report corpus version {corpus_version}")


def _budget(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _BUDGET_FIELDS, label)
    for field in _BUDGET_FIELDS:
        if _u64(result[field], f"{label}.{field}") == 0:
            raise EvidenceError(f"{label}.{field} must be nonzero")
    if result["maximum_prepared_elapsed_nanoseconds"] > result["maximum_cold_elapsed_nanoseconds"]:
        raise EvidenceError(f"{label} prepared cap exceeds cold cap")
    if result["maximum_cold_elapsed_nanoseconds"] > _MAXIMUM_WATCHDOG_NANOSECONDS:
        raise EvidenceError(f"{label} cold cap exceeds 24 hours")
    if result["maximum_address_space_bytes"] == _U64_MAX:
        raise EvidenceError(f"{label} address-space cap equals RLIM_INFINITY")
    return result


def _limits(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _LIMIT_FIELDS, label)
    for field in _LIMIT_FIELDS:
        limit = _u64(result[field], f"{label}.{field}")
        if limit == 0 or limit > _MAXIMUM_CORPUS_LIMITS[field]:
            raise EvidenceError(f"{label}.{field} is outside the Phase 4 bound")
    return result


def _config(value: Any) -> Mapping[str, Any]:
    result = _object(value, "report.config")
    _fields(result, _CONFIG_FIELDS, "report.config")
    if _u32(result["schema_version"], "report.config.schema_version") != 1:
        raise EvidenceError("report.config.schema_version must be 1")
    if _u32(result["case_id"], "report.config.case_id") == 0:
        raise EvidenceError("report.config.case_id must be nonzero")
    if _u32(result["requested_pool_size"], "report.config.requested_pool_size") not in {4, 8, 16}:
        raise EvidenceError("report.config.requested_pool_size must be 4, 8, or 16")
    if _u32(result["preparation_worker_count"], "report.config.preparation_worker_count") != 4:
        raise EvidenceError("report config must use exactly four workers")
    if _u32(result["repetitions"], "report.config.repetitions") != 20:
        raise EvidenceError("report config must use exactly 20 repetitions")
    setup = _u64(
        result["maximum_setup_elapsed_nanoseconds"],
        "report.config.maximum_setup_elapsed_nanoseconds",
    )
    if setup == 0 or setup > _MAXIMUM_WATCHDOG_NANOSECONDS:
        raise EvidenceError("report config setup cap is outside the 24-hour bound")
    _budget(result["external_budget"], "report.config.external_budget")
    _limits(result["corpus_limits"], "report.config.corpus_limits")
    return result


def _opportunity(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _OPPORTUNITY_FIELDS, label)
    for field in _OPPORTUNITY_FIELDS:
        _u64(result[field], f"{label}.{field}")
    return result


def _outcome(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _OUTCOME_FIELDS, label)
    for field in _OUTCOME_FIELDS:
        _u64(result[field], f"{label}.{field}")
    return result


def _semantics(value: Any, label: str, *, corpus_version: int = 1) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _SEMANTICS_FIELDS, label)
    if _u32(result["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    _enum(result["arm"], {0, 1}, f"{label}.arm")
    _enum(result["execution_order"], {0, 1}, f"{label}.execution_order")
    if _u32(result["corpus_version"], f"{label}.corpus_version") != corpus_version:
        raise EvidenceError(f"{label}.corpus_version must be {corpus_version}")
    for field in (
        "corpus_checksum",
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
        "budget_checksum",
        "root_seed",
        "candidate_columns_per_epoch",
        "preparation_route_queries",
        "preparation_route_work_units",
        "regeneration_route_queries",
        "regeneration_route_work_units",
        "requested_columns",
        "admitted_candidates",
        "rejected_columns",
        "final_candidate_count",
        "preparation_checksum",
        "algorithm_session_checksum",
        "final_pool_manifest_checksum",
        "final_rejection_manifest_checksum",
        "semantic_checksum",
    ):
        _u64(result[field], f"{label}.{field}")
    for field in (
        "case_id",
        "workload_net_count",
        "requested_pool_size",
        "repetition_index",
        "preparation_worker_count",
        "baseline_sweeps",
        "candidate_regeneration_epochs",
        "candidate_terminal_selection_rounds",
    ):
        _u32(result[field], f"{label}.{field}")
    _enum(result["terminal_reason"], set(range(5)), f"{label}.terminal_reason")
    _enum(result["candidate_outcome_source"], {0, 1, 2}, f"{label}.candidate_outcome_source")
    _budget(result["external_budget"], f"{label}.external_budget")
    opportunity = _opportunity(result["opportunity"], f"{label}.opportunity")
    actual = _opportunity(result["actual"], f"{label}.actual")
    outcome = _outcome(result["outcome"], f"{label}.outcome")
    if result["workload_net_count"] == 0 or not 1 <= result["preparation_worker_count"] <= 64:
        raise EvidenceError(f"{label} workload or worker count is invalid")
    if (
        outcome["selected_net_count"] + outcome["no_candidate_net_count"]
        != result["workload_net_count"]
    ):
        raise EvidenceError(f"{label} outcome roster does not partition the workload")
    if (
        actual["route_queries"] > opportunity["route_queries"]
        or actual["route_work_units"] > opportunity["route_work_units"]
    ):
        raise EvidenceError(f"{label} actual route work exceeds opportunity")
    if (
        opportunity["route_queries"] == 0
        or opportunity["route_work_units"] % opportunity["route_queries"] != 0
    ):
        raise EvidenceError(f"{label} has no exact per-query work bound")
    per_query = opportunity["route_work_units"] // opportunity["route_queries"]
    if per_query == 0:
        raise EvidenceError(f"{label} per-query work bound is zero")

    def within(queries: int, work: int) -> bool:
        return work <= queries * per_query

    if (
        not within(actual["route_queries"], actual["route_work_units"])
        or actual["route_queries"] > result["requested_columns"]
        or result["admitted_candidates"] + result["rejected_columns"] != result["requested_columns"]
        or result["final_candidate_count"] > result["admitted_candidates"]
        or outcome["selected_net_count"] > result["final_candidate_count"]
        or outcome["overused_resource_count"] > outcome["total_overuse_units"]
        or result["capacity_model_checksum"] == 0
        or outcome["world_checksum"] == 0
    ):
        raise EvidenceError(f"{label} counters or required identities are inconsistent")
    if result["arm"] == 0:
        if (
            result["candidate_outcome_source"] != 0
            or any(
                result[field] != 0
                for field in (
                    "preparation_route_queries",
                    "preparation_route_work_units",
                    "regeneration_route_queries",
                    "regeneration_route_work_units",
                    "preparation_checksum",
                    "final_pool_manifest_checksum",
                    "final_rejection_manifest_checksum",
                )
            )
            or result["requested_columns"] != actual["route_queries"]
            or result["algorithm_session_checksum"] == 0
        ):
            raise EvidenceError(f"{label} baseline accounting is inconsistent")
    elif (
        result["candidate_outcome_source"] == 0
        or result["preparation_route_queries"] + result["regeneration_route_queries"]
        != actual["route_queries"]
        or result["preparation_route_work_units"] + result["regeneration_route_work_units"]
        != actual["route_work_units"]
        or not within(result["preparation_route_queries"], result["preparation_route_work_units"])
        or not within(result["regeneration_route_queries"], result["regeneration_route_work_units"])
        or any(
            result[field] == 0
            for field in (
                "preparation_checksum",
                "algorithm_session_checksum",
                "final_pool_manifest_checksum",
                "final_rejection_manifest_checksum",
            )
        )
    ):
        raise EvidenceError(f"{label} candidate accounting is inconsistent")
    if result["terminal_reason"] == 0 and (
        outcome["selected_net_count"] != result["workload_net_count"]
        or outcome["no_candidate_net_count"] != 0
        or outcome["total_overuse_units"] != 0
    ):
        raise EvidenceError(f"{label} feasible terminal claim is inconsistent")
    if result["semantic_checksum"] == 0 or result[
        "semantic_checksum"
    ] != raw_validator.compute_semantic_checksum(result):
        raise EvidenceError(f"{label}.semantic_checksum does not authenticate semantics")
    return result


def _parse_optional_u64(value: Any, label: str) -> int | None:
    return None if value is None else _u64(value, label)


def _parse_per_net(value: Any, label: str) -> Mapping[str, Any]:
    report = _object(value, label)
    _fields(report, _PER_NET_FIELDS, label)
    if _u32(report["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    net = _object(report["net"], f"{label}.net")
    _fields(net, _NET_FIELDS, f"{label}.net")
    _u64(net["id"], f"{label}.net.id")
    _u32(net["generation"], f"{label}.net.generation")
    columns = _object(report["columns"], f"{label}.columns")
    _fields(columns, _COLUMN_FIELDS, f"{label}.columns")
    for field in _COLUMN_FIELDS:
        _u64(columns[field], f"{label}.columns.{field}")
    for field in (
        "final_pool_size",
        "unique_geometry_signature_count",
        "unique_resource_signature_count",
        "candidate_pair_count",
        "mean_resource_overlap_ppm",
        "minimum_resource_overlap_ppm",
        "mean_geometric_overlap_ppm",
        "minimum_geometric_overlap_ppm",
    ):
        _u64(report[field], f"{label}.{field}")
    status = _enum(report["selected_status"], {0, 1}, f"{label}.selected_status")
    candidate_id = report["selected_candidate_id"]
    if candidate_id is not None:
        candidate_id = _object(candidate_id, f"{label}.selected_candidate_id")
        _fields(candidate_id, _CANDIDATE_ID_FIELDS, f"{label}.selected_candidate_id")
        _u64(candidate_id["high"], f"{label}.selected_candidate_id.high")
        _u64(candidate_id["low"], f"{label}.selected_candidate_id.low")
    payload = _parse_optional_u64(
        report["selected_candidate_payload_checksum"],
        f"{label}.selected_candidate_payload_checksum",
    )
    metrics = report["selected_candidate_metrics"]
    if metrics is not None:
        metrics = _object(metrics, f"{label}.selected_candidate_metrics")
        _fields(metrics, _METRICS_FIELDS, f"{label}.selected_candidate_metrics")
        for field in _METRICS_FIELDS:
            _u64(metrics[field], f"{label}.selected_candidate_metrics.{field}")
    best = _parse_optional_u64(
        report["pool_best_intrinsic_cost"], f"{label}.pool_best_intrinsic_cost"
    )

    terminal_columns = sum(columns[field] for field in _COLUMN_FIELDS[2:])
    final_pool = report["final_pool_size"]
    expected_pairs = final_pool * (final_pool - 1) // 2 if final_pool >= 2 else 0
    pairless = report["candidate_pair_count"] == 0 and all(
        report[field] == 0
        for field in (
            "mean_resource_overlap_ppm",
            "minimum_resource_overlap_ppm",
            "mean_geometric_overlap_ppm",
            "minimum_geometric_overlap_ppm",
        )
    )
    paired = (
        report["candidate_pair_count"] != 0
        and report["mean_resource_overlap_ppm"] <= _OVERLAP_PPM
        and report["minimum_resource_overlap_ppm"] <= report["mean_resource_overlap_ppm"]
        and report["mean_geometric_overlap_ppm"] <= _OVERLAP_PPM
        and report["minimum_geometric_overlap_ppm"] <= report["mean_geometric_overlap_ppm"]
    )
    if (
        terminal_columns != columns["requested_columns"]
        or columns["executed_route_queries"] + columns["skipped_columns"]
        != columns["requested_columns"]
        or expected_pairs > _U64_MAX
        or report["candidate_pair_count"] != expected_pairs
        or report["unique_geometry_signature_count"] > final_pool
        or report["unique_resource_signature_count"] > final_pool
        or final_pool > columns["admitted_candidates"]
        or (
            final_pool == 0
            and (
                report["unique_geometry_signature_count"] != 0
                or report["unique_resource_signature_count"] != 0
                or best is not None
            )
        )
        or (
            final_pool != 0
            and (
                report["unique_geometry_signature_count"] == 0
                or report["unique_resource_signature_count"] == 0
                or best is None
            )
        )
        or (not pairless if report["candidate_pair_count"] == 0 else not paired)
    ):
        raise EvidenceError(f"{label} column, pool, signature, pair, or overlap closure is invalid")
    selected = (
        status == 0
        and final_pool != 0
        and candidate_id is not None
        and (candidate_id["high"] != 0 or candidate_id["low"] != 0)
        and payload is not None
        and metrics is not None
        and best is not None
        and best <= metrics["intrinsic_base_cost"]
    )
    no_candidate = (
        status == 1
        and final_pool == 0
        and candidate_id is None
        and payload is None
        and metrics is None
        and best is None
    )
    if not selected and not no_candidate:
        raise EvidenceError(f"{label} selected/no-candidate optional fields do not close")
    return report


def compute_telemetry_checksum(telemetry: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-ARM-REPORT-TELEMETRY-V1")
    hashed.u32(telemetry["schema_version"])
    hashed.u64(telemetry["associated_semantic_checksum"])
    hashed.u64(len(telemetry["per_net"]))
    for report in telemetry["per_net"]:
        hashed.u32(report["schema_version"])
        hashed.u64(report["net"]["id"])
        hashed.u32(report["net"]["generation"])
        for field in _COLUMN_FIELDS:
            hashed.u64(report["columns"][field])
        for field in (
            "final_pool_size",
            "unique_geometry_signature_count",
            "unique_resource_signature_count",
            "candidate_pair_count",
            "mean_resource_overlap_ppm",
            "minimum_resource_overlap_ppm",
            "mean_geometric_overlap_ppm",
            "minimum_geometric_overlap_ppm",
        ):
            hashed.u64(report[field])
        hashed.byte(report["selected_status"])
        candidate_id = report["selected_candidate_id"]
        hashed.boolean(candidate_id is not None)
        if candidate_id is not None:
            hashed.u64(candidate_id["high"])
            hashed.u64(candidate_id["low"])
        payload = report["selected_candidate_payload_checksum"]
        hashed.boolean(payload is not None)
        if payload is not None:
            hashed.u64(payload)
        metrics = report["selected_candidate_metrics"]
        hashed.boolean(metrics is not None)
        if metrics is not None:
            for field in _METRICS_FIELDS:
                hashed.u64(metrics[field])
        best = report["pool_best_intrinsic_cost"]
        hashed.boolean(best is not None)
        if best is not None:
            hashed.u64(best)
    return hashed.finish()


def compute_roster_checksum(
    row: Mapping[str, Any],
    nets: Sequence[tuple[int, int]],
    *,
    corpus_version: int = 1,
) -> int:
    if corpus_version not in {1, 2}:
        raise EvidenceError(f"unsupported per-net roster corpus version {corpus_version}")
    hashed = raw_validator.StableHashBuilder()
    hashed.string(
        "APGAR-PHASE4-WORKLOAD-NET-ROSTER-V1"
        if corpus_version == 1
        else "APGAR-PHASE4-WORKLOAD-NET-ROSTER-V2"
    )
    hashed.u32(row["schema_version"])
    hashed.u32(row["corpus_version"])
    hashed.u64(row["corpus_checksum"])
    hashed.u32(row["case_id"])
    hashed.u64(row["descriptor_fingerprint"])
    hashed.u64(row["case_checksum"])
    hashed.u64(row["board_content_hash"])
    hashed.u64(row["workload_checksum"])
    hashed.u64(len(nets))
    for net_id, generation in nets:
        hashed.u64(net_id)
        hashed.u32(generation)
    return hashed.finish()


def _parse_telemetry(
    value: Any,
    label: str,
    semantics: Mapping[str, Any],
    roster_row: Mapping[str, Any],
    expected_nets: Sequence[tuple[int, int]],
    *,
    corpus_version: int = 1,
) -> Mapping[str, Any]:
    telemetry = _object(value, label)
    _fields(telemetry, _TELEMETRY_FIELDS, label)
    if _u32(telemetry["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    associated = _u64(
        telemetry["associated_semantic_checksum"], f"{label}.associated_semantic_checksum"
    )
    reports = _array(telemetry["per_net"], f"{label}.per_net")
    if len(reports) > _MAX_NETS or len(reports) != semantics["workload_net_count"]:
        raise EvidenceError(f"{label}.per_net exceeds its bound or differs from workload count")
    parsed_reports = [
        _parse_per_net(report, f"{label}.per_net[{index}]") for index, report in enumerate(reports)
    ]
    nets = [(report["net"]["id"], report["net"]["generation"]) for report in parsed_reports]
    if nets != sorted(set(nets)):
        raise EvidenceError(f"{label}.per_net must use a strictly increasing full-EntityRef roster")
    if nets != list(expected_nets):
        raise EvidenceError(f"{label}.per_net differs from the independently frozen net roster")
    if (
        associated == 0
        or associated != semantics["semantic_checksum"]
        or _u64(telemetry["telemetry_checksum"], f"{label}.telemetry_checksum") == 0
        or telemetry["telemetry_checksum"] != compute_telemetry_checksum(telemetry)
    ):
        raise EvidenceError(f"{label} semantic association or telemetry checksum is invalid")

    totals = {
        "requested": sum(report["columns"]["requested_columns"] for report in parsed_reports),
        "executed": sum(report["columns"]["executed_route_queries"] for report in parsed_reports),
        "admitted": sum(report["columns"]["admitted_candidates"] for report in parsed_reports),
        "rejected": sum(
            sum(report["columns"][field] for field in _COLUMN_FIELDS[3:])
            for report in parsed_reports
        ),
        "final": sum(report["final_pool_size"] for report in parsed_reports),
        "selected": sum(report["selected_status"] == 0 for report in parsed_reports),
        "no_candidate": sum(report["selected_status"] == 1 for report in parsed_reports),
        "cost": sum(
            report["selected_candidate_metrics"]["intrinsic_base_cost"]
            for report in parsed_reports
            if report["selected_status"] == 0
        ),
    }
    if any(total > _U64_MAX for total in totals.values()) or (
        totals["requested"] != semantics["requested_columns"]
        or totals["executed"] != semantics["actual"]["route_queries"]
        or totals["admitted"] != semantics["admitted_candidates"]
        or totals["rejected"] != semantics["rejected_columns"]
        or totals["final"] != semantics["final_candidate_count"]
        or totals["selected"] != semantics["outcome"]["selected_net_count"]
        or totals["no_candidate"] != semantics["outcome"]["no_candidate_net_count"]
        or totals["cost"] != semantics["outcome"]["total_intrinsic_cost"]
    ):
        raise EvidenceError(f"{label} per-net totals do not close complete arm semantics")
    if (
        compute_roster_checksum(roster_row, nets, corpus_version=corpus_version)
        != roster_row["roster_checksum"]
    ):
        raise EvidenceError(f"{label} full EntityRef roster checksum differs from the frozen row")
    return telemetry


def compute_report_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-PER-NET-REPORT-ARTIFACT-V1")
    hashed.u32(document["schema_version"])
    hashed.u32(document["raw_wire_schema_version"])
    config = document["config"]
    for field in (
        "schema_version",
        "case_id",
        "requested_pool_size",
        "preparation_worker_count",
        "repetitions",
    ):
        hashed.u32(config[field])
    hashed.u64(config["maximum_setup_elapsed_nanoseconds"])
    for field in _BUDGET_FIELDS:
        hashed.u64(config["external_budget"][field])
    for field in _LIMIT_FIELDS:
        hashed.u64(config["corpus_limits"][field])
    for field in (
        "corpus_checksum",
        "raw_cell_plan_checksum",
        "raw_cell_artifact_checksum",
        "raw_source_envelope_checksum",
    ):
        hashed.u64(document[field])
    reference = document["raw_reference"]
    hashed.u32(reference["repetition_index"])
    hashed.byte(reference["execution_order"])
    for field in _RAW_REFERENCE_FIELDS[2:]:
        hashed.u64(reference[field])
    hashed.boolean(document["decision_eligible"])
    hashed.u64(document["workload_net_roster_checksum"])
    hashed.u64(len(document["arms"]))
    for arm in document["arms"]:
        hashed.byte(arm["arm"])
        hashed.u64(arm["raw_semantic_checksum"])
        hashed.u64(arm["diagnostic"]["semantics"]["semantic_checksum"])
        hashed.u64(arm["diagnostic"]["telemetry"]["telemetry_checksum"])
    return hashed.finish()


def compute_report_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-PER-NET-REPORT-SOURCE-ENVELOPE-V1")
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def _validate_join_documents(
    raw_value: Any,
    report_value: Any,
    *,
    expected_commit: str,
    corpus_version: int = 1,
) -> None:
    raw_document = _object(raw_value, "raw cell")
    report = _object(report_value, "per-net report")
    _fields(report, _TOP_FIELDS, "per-net report")
    source_commit = _string(report["source_commit"], "report.source_commit")
    source_stamped = _boolean(report["source_stamped"], "report.source_stamped")
    source_dirty = _boolean(report["source_tree_dirty"], "report.source_tree_dirty")
    if (
        source_commit != expected_commit
        or not source_stamped
        or source_dirty
        or source_commit != raw_document["source_commit"]
        or source_stamped != raw_document["source_stamped"]
        or source_dirty != raw_document["source_tree_dirty"]
    ):
        raise EvidenceError("raw and report must name the same clean independently expected commit")
    if _u32(report["schema_version"], "report.schema_version") != 1:
        raise EvidenceError("report.schema_version must be 1")
    if (
        _u32(report["raw_wire_schema_version"], "report.raw_wire_schema_version")
        != raw_document["wire_schema_version"]
    ):
        raise EvidenceError("report raw wire schema differs from the raw artifact")
    config = _config(report["config"])
    if config != raw_document["config"]:
        raise EvidenceError("report config does not exactly match the raw cell")
    for report_field, raw_field in (
        ("corpus_checksum", "corpus_checksum"),
        ("raw_cell_plan_checksum", "cell_plan_checksum"),
        ("raw_cell_artifact_checksum", "artifact_checksum"),
        ("raw_source_envelope_checksum", "source_envelope_checksum"),
    ):
        if _u64(report[report_field], f"report.{report_field}") != raw_document[raw_field]:
            raise EvidenceError(f"report.{report_field} does not join the raw cell")
    if report["raw_cell_plan_checksum"] != raw_validator.compute_cell_plan_checksum(
        raw_document, corpus_version=corpus_version
    ):
        raise EvidenceError("report raw cell plan is not independently reconstructible")
    if _boolean(report["decision_eligible"], "report.decision_eligible"):
        raise EvidenceError("per-net diagnostics must never be decision eligible")

    reference = _object(report["raw_reference"], "report.raw_reference")
    _fields(reference, _RAW_REFERENCE_FIELDS, "report.raw_reference")
    if _u32(reference["repetition_index"], "report.raw_reference.repetition_index") != 0:
        raise EvidenceError("report must reference raw repetition zero")
    if _enum(reference["execution_order"], {0, 1}, "report.raw_reference.execution_order") != 0:
        raise EvidenceError("report must reference a baseline-first raw pair")
    for field in _RAW_REFERENCE_FIELDS[2:]:
        if _u64(reference[field], f"report.raw_reference.{field}") == 0:
            raise EvidenceError(f"report.raw_reference.{field} must be nonzero")
    raw_attempt = raw_document["attempts"][0]
    raw_result = raw_attempt["result"]
    raw_baseline = raw_attempt["baseline"]["record"]
    raw_candidate = raw_attempt["candidate"]["record"]
    expected_reference = {
        "repetition_index": 0,
        "execution_order": 0,
        "pair_attempt_checksum": raw_attempt["attempt_checksum"],
        "paired_semantic_checksum": raw_result["semantic_checksum"],
        "paired_artifact_checksum": raw_result["artifact_checksum"],
        "baseline_semantic_checksum": raw_baseline["semantics"]["semantic_checksum"],
        "baseline_arm_artifact_checksum": raw_baseline["artifact_checksum"],
        "candidate_semantic_checksum": raw_candidate["semantics"]["semantic_checksum"],
        "candidate_arm_artifact_checksum": raw_candidate["artifact_checksum"],
    }
    if reference != expected_reference:
        raise EvidenceError("report raw reference does not exactly join repetition-zero records")

    roster_row, expected_nets = _frozen_roster_row(config["case_id"], corpus_version=corpus_version)
    if (
        _u64(report["workload_net_roster_checksum"], "report.workload_net_roster_checksum")
        != roster_row["roster_checksum"]
        or report["corpus_checksum"] != roster_row["corpus_checksum"]
    ):
        raise EvidenceError("report roster or corpus checksum differs from the frozen row")
    arms = _array(report["arms"], "report.arms")
    if len(arms) != 2:
        raise EvidenceError("report.arms must contain exactly baseline then candidate")
    raw_semantics = (raw_baseline["semantics"], raw_candidate["semantics"])
    parsed_telemetry: list[Mapping[str, Any]] = []
    for index, value in enumerate(arms):
        label = f"report.arms[{index}]"
        arm = _object(value, label)
        _fields(arm, _ARM_FIELDS, label)
        if _enum(arm["arm"], {0, 1}, f"{label}.arm") != index:
            raise EvidenceError("report arms must be ordered baseline then candidate")
        diagnostic = _object(arm["diagnostic"], f"{label}.diagnostic")
        _fields(diagnostic, _DIAGNOSTIC_FIELDS, f"{label}.diagnostic")
        semantics = _semantics(
            diagnostic["semantics"],
            f"{label}.semantics",
            corpus_version=corpus_version,
        )
        if (
            _u64(arm["raw_semantic_checksum"], f"{label}.raw_semantic_checksum")
            != raw_semantics[index]["semantic_checksum"]
            or semantics != raw_semantics[index]
        ):
            raise EvidenceError(f"{label} complete semantics differ from the raw arm")
        parsed_telemetry.append(
            _parse_telemetry(
                diagnostic["telemetry"],
                f"{label}.telemetry",
                semantics,
                roster_row,
                expected_nets,
                corpus_version=corpus_version,
            )
        )
    first_roster = [report["net"] for report in parsed_telemetry[0]["per_net"]]
    second_roster = [report["net"] for report in parsed_telemetry[1]["per_net"]]
    if first_roster != second_roster:
        raise EvidenceError("baseline and candidate diagnostics name different workload rosters")

    if _u64(report["artifact_checksum"], "report.artifact_checksum") == 0 or report[
        "artifact_checksum"
    ] != compute_report_artifact_checksum(report):
        raise EvidenceError("report artifact checksum does not authenticate the report")
    if _u64(report["source_envelope_checksum"], "report.source_envelope_checksum") == 0 or report[
        "source_envelope_checksum"
    ] != compute_report_source_envelope_checksum(report):
        raise EvidenceError("report source envelope does not authenticate source provenance")


def validate_join(raw_value: Any, report_value: Any, *, expected_commit: str) -> None:
    """Validate already parsed documents; intended for bounded in-memory tests."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise EvidenceError("publication requires an independently supplied expected commit")
    raw_validator.validate_document(raw_value, expected_commit=expected_commit)
    _validate_join_documents(
        raw_value, report_value, expected_commit=expected_commit, corpus_version=1
    )


def validate_report_against_validated_raw(
    raw_value: Any, report_value: Any, *, expected_commit: str
) -> None:
    """Structurally join a report after its versioned Raw authority was validated."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise EvidenceError("publication requires an independently supplied expected commit")
    _validate_join_documents(
        raw_value, report_value, expected_commit=expected_commit, corpus_version=1
    )


def validate_confirmatory_report_against_validated_raw(
    raw_value: Any, report_value: Any, *, expected_commit: str
) -> None:
    """Structurally join a Corpus V2 report after its Raw authority was validated."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise EvidenceError("publication requires an independently supplied expected commit")
    _validate_join_documents(
        raw_value, report_value, expected_commit=expected_commit, corpus_version=2
    )


def validate_confirmatory_join(
    raw_value: Any,
    report_value: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate the frozen (10200,4) ordinary Corpus V2 report join."""
    raw_validator.validate_confirmatory_document(
        raw_value,
        expected_commit=expected_commit,
    )
    raw_document = _object(raw_value, "raw cell")
    config = _object(raw_document["config"], "raw cell.config")
    if (
        raw_document["wire_schema_version"] != 1
        or config["case_id"] != 10200
        or config["requested_pool_size"] != 4
    ):
        raise EvidenceError(
            "confirmatory per-net report authority is restricted to ordinary (10200,4)"
        )
    _validate_join_documents(
        raw_document,
        report_value,
        expected_commit=expected_commit,
        corpus_version=2,
    )


def validate_config(value: Any) -> Mapping[str, Any]:
    """Validate one canonical report/snapshot cell configuration."""
    return _config(value)


def validate_semantics(value: Any, label: str) -> Mapping[str, Any]:
    """Validate one complete Phase 4 arm semantic object."""
    return _semantics(value, label)


def read_report_document(path: pathlib.Path) -> Any:
    """Read one canonical per-net report under its frozen input bound."""
    return _read_canonical(path, "per-net report", _MAX_REPORT_BYTES)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw_document = raw_validator.read_validated_publication_document(
            options.raw, expected_commit=options.expected_commit
        )
        report_document = read_report_document(options.report)
        _validate_join_documents(
            raw_document, report_document, expected_commit=options.expected_commit
        )
    except EvidenceError as error:
        print(f"Phase 4 raw/report join validation failed: {error}", file=sys.stderr)
        return 1
    print("validated one Phase 4 Raw v1 and per-net report companion join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
