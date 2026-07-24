"""Strict validator for one-cell Phase 4 isolated raw evidence artifacts."""

from __future__ import annotations

import argparse
import functools
import json
import os
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools.phase4_bounded_json_input import read_regular_file

_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_I32_MIN = -(1 << 31)
_I32_MAX = (1 << 31) - 1
_COMMIT = re.compile(r"[0-9a-f]{40}")
_CANONICAL_REPETITIONS = 20
_CANONICAL_WORKERS = 4
_CANONICAL_ROUTE_WORK_UNITS_PER_QUERY = 1_000_000_000
_MAXIMUM_WATCHDOG_NANOSECONDS = 24 * 60 * 60 * 1_000_000_000
_MAXIMUM_RAW_JSON_BYTES = 64 * 1024 * 1024
_MAXIMUM_JSON_NESTING_DEPTH = 64
_MAXIMUM_CORPUS_LIMITS = {
    "maximum_nets": 4_096,
    "maximum_compiled_nodes": 100_000_000,
    "maximum_compiled_host_bytes": 8 * 1024 * 1024 * 1024,
    "maximum_active_regions": 1_000_000,
    "maximum_board_entities": 1_000_000,
}
_REPRESENTATIVE_MANIFEST_PATH = (
    pathlib.Path(__file__).resolve().parent.parent
    / "schemas/benchmark/phase4_representative_manifest_v1.json"
)
_MANIFEST_FIELDS = (
    "schema_version",
    "corpus_checksum",
    "cases",
    "canonical_algorithm_budgets",
)
_MANIFEST_CASE_FIELDS = (
    "case_id",
    "requested_pool_sizes",
    "workload_net_count",
    "descriptor_fingerprint",
    "build_status",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "capacity_model_checksum",
    "required_compiled_nodes",
    "required_compiled_host_bytes",
    "required_active_regions",
    "required_board_entities",
)
_MANIFEST_BUDGET_FIELDS = ("case_id", "pool_checksums")
_MANIFEST_POOL_CHECKSUM_FIELDS = ("pool", "checksum")


class EvidenceError(ValueError):
    """A stable validation failure suitable for a Bazel test diagnostic."""


class StableHashBuilder:
    """Board IR v1 byte-stable FNV-1a, matching stable_hash.h."""

    def __init__(self) -> None:
        self._value = 14695981039346656037

    def byte(self, value: int) -> None:
        self._value ^= value & 0xFF
        self._value = (self._value * 1099511628211) & _U64_MAX

    def boolean(self, value: bool) -> None:
        self.byte(1 if value else 0)

    def u32(self, value: int) -> None:
        for _ in range(4):
            self.byte(value)
            value >>= 8

    def u64(self, value: int) -> None:
        for _ in range(8):
            self.byte(value)
            value >>= 8

    def i32(self, value: int) -> None:
        self.u32(value & _U32_MAX)

    def string(self, value: str) -> None:
        encoded = value.encode("utf-8")
        self.u64(len(encoded))
        for byte in encoded:
            self.byte(byte)

    def finish(self) -> int:
        return self._value


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be a JSON object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be a JSON array")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    actual_keys = tuple(value)
    expected_keys = tuple(expected)
    actual = set(actual_keys)
    expected_set = set(expected_keys)
    if actual != expected_set:
        missing = sorted(expected_set - actual)
        extra = sorted(actual - expected_set)
        raise EvidenceError(f"{label} fields differ: missing={missing}, extra={extra}")
    if actual_keys != expected_keys:
        raise EvidenceError(f"{label} fields are not in canonical key order")


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise EvidenceError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _i32(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not _I32_MIN <= value <= _I32_MAX:
        raise EvidenceError(f"{label} must be a signed 32-bit integer")
    return value


def _bool(value: Any, label: str) -> bool:
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


def _version(value: Mapping[str, Any], label: str) -> None:
    if _u32(value["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")


@functools.cache
def _frozen_representative_manifest() -> tuple[
    int, Mapping[int, Mapping[str, Any]], Mapping[tuple[int, int], int]
]:
    try:
        document = json.loads(
            _REPRESENTATIVE_MANIFEST_PATH.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_pairs,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read frozen representative manifest: {error}") from error
    manifest = _object(document, "representative manifest")
    _fields(manifest, _MANIFEST_FIELDS, "representative manifest")
    if _u32(manifest["schema_version"], "representative manifest.schema_version") != 1:
        raise EvidenceError("representative manifest.schema_version must be 1")
    corpus_checksum = _u64(manifest["corpus_checksum"], "representative manifest.corpus_checksum")
    if corpus_checksum == 0:
        raise EvidenceError("representative manifest.corpus_checksum must be nonzero")
    indexed: dict[int, Mapping[str, Any]] = {}
    previous_case_id = 0
    for index, raw_case in enumerate(_array(manifest["cases"], "representative manifest.cases")):
        label = f"representative manifest.cases[{index}]"
        case = _object(raw_case, label)
        _fields(case, _MANIFEST_CASE_FIELDS, label)
        case_id = _u32(case["case_id"], f"{label}.case_id")
        if case_id <= previous_case_id or case_id in indexed:
            raise EvidenceError("representative manifest cases must have unique increasing IDs")
        previous_case_id = case_id
        pools = _array(case["requested_pool_sizes"], f"{label}.requested_pool_sizes")
        if not pools or any(
            _u32(pool, f"{label}.requested_pool_sizes") not in {4, 8, 16} for pool in pools
        ):
            raise EvidenceError(f"{label}.requested_pool_sizes is outside the canonical set")
        if list(pools) != sorted(set(pools)):
            raise EvidenceError(f"{label}.requested_pool_sizes must be unique and increasing")
        if _u32(case["workload_net_count"], f"{label}.workload_net_count") == 0:
            raise EvidenceError(f"{label}.workload_net_count must be nonzero")
        if _u64(case["descriptor_fingerprint"], f"{label}.descriptor_fingerprint") == 0:
            raise EvidenceError(f"{label}.descriptor_fingerprint must be nonzero")
        status = _string(case["build_status"], f"{label}.build_status")
        for field in (
            "case_checksum",
            "board_content_hash",
            "workload_checksum",
            "capacity_model_checksum",
            "required_compiled_nodes",
            "required_compiled_host_bytes",
            "required_active_regions",
            "required_board_entities",
        ):
            _u64(case[field], f"{label}.{field}")
        identity_fields = (
            "case_checksum",
            "board_content_hash",
            "workload_checksum",
            "capacity_model_checksum",
        )
        if status == "success":
            if any(case[field] == 0 for field in identity_fields) or any(
                case[field] == 0
                for field in (
                    "required_compiled_nodes",
                    "required_compiled_host_bytes",
                    "required_active_regions",
                    "required_board_entities",
                )
            ):
                raise EvidenceError(f"{label} has a contradictory successful build witness")
        elif status == "compiled_work_bound":
            if (
                any(case[field] != 0 for field in identity_fields)
                or (
                    case["required_compiled_nodes"] == 0
                    and case["required_compiled_host_bytes"] == 0
                )
                or case["required_active_regions"] != 0
                or case["required_board_entities"] != 0
            ):
                raise EvidenceError(f"{label} has a contradictory work-bound witness")
        else:
            raise EvidenceError(f"{label}.build_status is unknown")
        indexed[case_id] = case
    budgets: dict[tuple[int, int], int] = {}
    previous_budget_case_id = 0
    for index, raw_budget in enumerate(
        _array(
            manifest["canonical_algorithm_budgets"],
            "representative manifest.canonical_algorithm_budgets",
        )
    ):
        label = f"representative manifest.canonical_algorithm_budgets[{index}]"
        budget = _object(raw_budget, label)
        _fields(budget, _MANIFEST_BUDGET_FIELDS, label)
        case_id = _u32(budget["case_id"], f"{label}.case_id")
        if case_id <= previous_budget_case_id or case_id not in indexed:
            raise EvidenceError("representative budget cases must match unique increasing cases")
        previous_budget_case_id = case_id
        pool_checksums = _array(budget["pool_checksums"], f"{label}.pool_checksums")
        observed_pools: list[int] = []
        for pool_index, raw_pool_checksum in enumerate(pool_checksums):
            pool_label = f"{label}.pool_checksums[{pool_index}]"
            pool_checksum = _object(raw_pool_checksum, pool_label)
            _fields(pool_checksum, _MANIFEST_POOL_CHECKSUM_FIELDS, pool_label)
            pool = _u32(pool_checksum["pool"], f"{pool_label}.pool")
            checksum = _u64(pool_checksum["checksum"], f"{pool_label}.checksum")
            if pool not in {4, 8, 16} or checksum == 0 or (case_id, pool) in budgets:
                raise EvidenceError(f"{pool_label} is not a unique canonical budget checksum")
            observed_pools.append(pool)
            budgets[(case_id, pool)] = checksum
        if observed_pools != list(indexed[case_id]["requested_pool_sizes"]):
            raise EvidenceError(f"{label} does not cover the exact requested pool roster")
    if set(indexed) != {case_id for case_id, _ in budgets}:
        raise EvidenceError("representative budget roster does not cover every case")
    return corpus_checksum, indexed, budgets


def _representative_manifest() -> tuple[
    int, Mapping[int, Mapping[str, Any]], Mapping[tuple[int, int], int]
]:
    return _frozen_representative_manifest()


@functools.cache
def _frozen_confirmatory_representative_manifest() -> tuple[
    int, Mapping[int, Mapping[str, Any]], Mapping[tuple[int, int], int]
]:
    # Import lazily: the V2 authority validator reuses this module's stable
    # hashing primitive, so a top-level import would create a cycle.
    from tools import validate_phase4_representative_manifest_v2 as authority_validator

    try:
        representative, _ = authority_validator.validate_authorities()
    except authority_validator.AuthorityError as error:
        raise EvidenceError(
            f"cannot authenticate frozen confirmatory authorities: {error}"
        ) from error
    cases = {
        _u32(case["case_id"], "confirmatory representative case_id"): case
        for case in _array(representative["cases"], "confirmatory representative cases")
    }
    budgets: dict[tuple[int, int], int] = {}
    for raw_budget in _array(
        representative["canonical_algorithm_budgets"],
        "confirmatory representative canonical_algorithm_budgets",
    ):
        budget = _object(raw_budget, "confirmatory representative budget")
        case_id = _u32(budget["case_id"], "confirmatory representative budget.case_id")
        for raw_pool in _array(
            budget["pool_checksums"], "confirmatory representative budget.pool_checksums"
        ):
            pool = _object(raw_pool, "confirmatory representative pool checksum")
            budgets[
                (
                    case_id,
                    _u32(pool["pool"], "confirmatory representative pool"),
                )
            ] = _u64(pool["checksum"], "confirmatory representative budget checksum")
    return (
        _u64(representative["corpus_checksum"], "confirmatory representative corpus_checksum"),
        cases,
        budgets,
    )


def _representative_manifest_for_corpus(
    corpus_version: int,
) -> tuple[int, Mapping[int, Mapping[str, Any]], Mapping[tuple[int, int], int]]:
    if corpus_version == 1:
        return _representative_manifest()
    if corpus_version == 2:
        return _frozen_confirmatory_representative_manifest()
    raise EvidenceError(f"unsupported representative corpus version {corpus_version}")


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
_LIFECYCLE_FIELDS = (
    "workers_started_before",
    "workers_started_after",
    "invocations_started_before",
    "invocations_started_after",
    "invocations_completed_before",
    "invocations_completed_after",
)
_OUTCOME_FIELDS = (
    "selected_net_count",
    "no_candidate_net_count",
    "overused_resource_count",
    "total_overuse_units",
    "total_intrinsic_cost",
    "world_checksum",
)
_OPPORTUNITY_FIELDS = ("route_queries", "route_work_units")


def _budget(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _BUDGET_FIELDS, label)
    for field in _BUDGET_FIELDS:
        if _u64(result[field], f"{label}.{field}") == 0:
            raise EvidenceError(f"{label}.{field} must be nonzero")
    if result["maximum_prepared_elapsed_nanoseconds"] > result["maximum_cold_elapsed_nanoseconds"]:
        raise EvidenceError(f"{label} prepared elapsed cap must not exceed cold elapsed cap")
    if result["maximum_cold_elapsed_nanoseconds"] > _MAXIMUM_WATCHDOG_NANOSECONDS:
        raise EvidenceError(f"{label} cold elapsed cap must not exceed 24 hours")
    if result["maximum_address_space_bytes"] == _U64_MAX:
        raise EvidenceError(f"{label}.maximum_address_space_bytes must not equal RLIM_INFINITY")
    return result


def _limits(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _LIMIT_FIELDS, label)
    for field in _LIMIT_FIELDS:
        limit = _u64(result[field], f"{label}.{field}")
        if limit == 0:
            raise EvidenceError(f"{label}.{field} must be nonzero")
        if limit > _MAXIMUM_CORPUS_LIMITS[field]:
            raise EvidenceError(f"{label}.{field} must not exceed {_MAXIMUM_CORPUS_LIMITS[field]}")
    return result


def _lifecycle(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _LIFECYCLE_FIELDS, label)
    for field in _LIFECYCLE_FIELDS:
        _u64(result[field], f"{label}.{field}")
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


def _config(value: Any, expected_repetitions: int, expected_workers: int) -> Mapping[str, Any]:
    result = _object(value, "config")
    _fields(result, _CONFIG_FIELDS, "config")
    _version(result, "config")
    if _u32(result["case_id"], "config.case_id") == 0:
        raise EvidenceError("config.case_id must be nonzero")
    if _u32(result["requested_pool_size"], "config.requested_pool_size") not in {4, 8, 16}:
        raise EvidenceError("config.requested_pool_size must be 4, 8, or 16")
    workers = _u32(result["preparation_worker_count"], "config.preparation_worker_count")
    if workers != expected_workers:
        raise EvidenceError(
            f"config.preparation_worker_count must be exactly {expected_workers}; "
            f"observed {workers}"
        )
    repetitions = _u32(result["repetitions"], "config.repetitions")
    if repetitions != expected_repetitions:
        raise EvidenceError(
            f"config.repetitions must be exactly {expected_repetitions}; observed {repetitions}"
        )
    setup_limit = _u64(
        result["maximum_setup_elapsed_nanoseconds"], "config.maximum_setup_elapsed_nanoseconds"
    )
    if setup_limit == 0:
        raise EvidenceError("config.maximum_setup_elapsed_nanoseconds must be nonzero")
    if setup_limit > _MAXIMUM_WATCHDOG_NANOSECONDS:
        raise EvidenceError("config.maximum_setup_elapsed_nanoseconds must not exceed 24 hours")
    _budget(result["external_budget"], "config.external_budget")
    _limits(result["corpus_limits"], "config.corpus_limits")
    return result


_ENVIRONMENT_FIELDS = (
    "schema_version",
    "host_os",
    "host_kernel",
    "host_architecture",
    "cpu_model",
    "compiler_identity",
    "monotonic_clock",
    "online_cpu_count",
    "affinity_cpu_count",
    "total_host_memory_bytes",
    "environment_checksum",
)


def compute_environment_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-HOST-ENVIRONMENT-V1")
    hashed.u32(value["schema_version"])
    for field in (
        "host_os",
        "host_kernel",
        "host_architecture",
        "cpu_model",
        "compiler_identity",
        "monotonic_clock",
    ):
        hashed.string(value[field])
    hashed.u64(value["online_cpu_count"])
    hashed.u64(value["affinity_cpu_count"])
    hashed.u64(value["total_host_memory_bytes"])
    return hashed.finish()


def _environment(value: Any) -> Mapping[str, Any]:
    result = _object(value, "environment")
    _fields(result, _ENVIRONMENT_FIELDS, "environment")
    _version(result, "environment")
    for field in (
        "host_os",
        "host_kernel",
        "host_architecture",
        "cpu_model",
        "compiler_identity",
        "monotonic_clock",
    ):
        if not _string(result[field], f"environment.{field}"):
            raise EvidenceError(f"environment.{field} must be nonempty")
    for field in ("online_cpu_count", "affinity_cpu_count", "total_host_memory_bytes"):
        if _u64(result[field], f"environment.{field}") == 0:
            raise EvidenceError(f"environment.{field} must be nonzero")
    expected = compute_environment_checksum(result)
    if _u64(result["environment_checksum"], "environment.environment_checksum") != expected:
        raise EvidenceError("environment.environment_checksum does not authenticate environment")
    return result


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


def compute_semantic_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-TRIAL-ARM-SEMANTIC-V1")
    hashed.u32(value["schema_version"])
    hashed.byte(value["arm"])
    hashed.u32(value["corpus_version"])
    for field, width in (
        ("corpus_checksum", 64),
        ("case_id", 32),
        ("descriptor_fingerprint", 64),
        ("case_checksum", 64),
        ("board_content_hash", 64),
        ("workload_checksum", 64),
        ("capacity_model_checksum", 64),
        ("budget_checksum", 64),
        ("workload_net_count", 32),
        ("requested_pool_size", 32),
        ("repetition_index", 32),
        ("root_seed", 64),
        ("baseline_sweeps", 32),
        ("candidate_regeneration_epochs", 32),
        ("candidate_columns_per_epoch", 64),
        ("candidate_terminal_selection_rounds", 32),
    ):
        (hashed.u32 if width == 32 else hashed.u64)(value[field])
    for field in (
        "maximum_prepared_elapsed_nanoseconds",
        "maximum_cold_elapsed_nanoseconds",
        "maximum_address_space_bytes",
        "maximum_peak_host_bytes",
    ):
        hashed.u64(value["external_budget"][field])
    for part in ("opportunity", "actual"):
        hashed.u64(value[part]["route_queries"])
        hashed.u64(value[part]["route_work_units"])
    for field in (
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
    ):
        hashed.u64(value[field])
    hashed.byte(value["terminal_reason"])
    hashed.byte(value["candidate_outcome_source"])
    for field in (
        "selected_net_count",
        "no_candidate_net_count",
        "overused_resource_count",
        "total_overuse_units",
        "total_intrinsic_cost",
        "world_checksum",
    ):
        hashed.u64(value["outcome"][field])
    return hashed.finish()


def _semantics(value: Any, label: str, *, expected_corpus_version: int = 1) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _SEMANTICS_FIELDS, label)
    _version(result, label)
    _enum(result["arm"], {0, 1}, f"{label}.arm")
    _enum(result["execution_order"], {0, 1}, f"{label}.execution_order")
    if _u32(result["corpus_version"], f"{label}.corpus_version") != expected_corpus_version:
        raise EvidenceError(f"{label}.corpus_version must be {expected_corpus_version}")
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
    if result["workload_net_count"] == 0 or result["preparation_worker_count"] == 0:
        raise EvidenceError(f"{label} has zero workload or worker count")
    if (
        outcome["selected_net_count"] + outcome["no_candidate_net_count"]
        != result["workload_net_count"]
    ):
        raise EvidenceError(f"{label} outcome roster does not partition the workload")
    if (
        actual["route_queries"] > opportunity["route_queries"]
        or actual["route_work_units"] > opportunity["route_work_units"]
    ):
        raise EvidenceError(f"{label} actual route work exceeds its opportunity")
    if (
        opportunity["route_queries"] == 0
        or opportunity["route_work_units"] % opportunity["route_queries"] != 0
    ):
        raise EvidenceError(f"{label} opportunity has no exact per-query work bound")
    per_query_work = opportunity["route_work_units"] // opportunity["route_queries"]
    if per_query_work == 0:
        raise EvidenceError(f"{label} opportunity has a zero per-query work bound")

    def work_within_query_count(route_queries: int, route_work_units: int) -> bool:
        return route_work_units <= route_queries * per_query_work

    if (
        not work_within_query_count(actual["route_queries"], actual["route_work_units"])
        or actual["route_queries"] > result["requested_columns"]
        or result["admitted_candidates"] + result["rejected_columns"] != result["requested_columns"]
        or result["final_candidate_count"] > result["admitted_candidates"]
        or outcome["selected_net_count"] > result["final_candidate_count"]
        or outcome["overused_resource_count"] > outcome["total_overuse_units"]
        or result["capacity_model_checksum"] == 0
        or outcome["world_checksum"] == 0
    ):
        raise EvidenceError(f"{label} route, candidate, or outcome counters are inconsistent")
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
                )
            )
            or result["requested_columns"] != actual["route_queries"]
            or result["preparation_checksum"] != 0
            or result["algorithm_session_checksum"] == 0
            or result["final_pool_manifest_checksum"] != 0
            or result["final_rejection_manifest_checksum"] != 0
        ):
            raise EvidenceError(f"{label} baseline accounting is inconsistent")
    else:
        if (
            result["candidate_outcome_source"] == 0
            or result["preparation_route_queries"] + result["regeneration_route_queries"]
            != actual["route_queries"]
            or result["preparation_route_work_units"] + result["regeneration_route_work_units"]
            != actual["route_work_units"]
            or not work_within_query_count(
                result["preparation_route_queries"],
                result["preparation_route_work_units"],
            )
            or not work_within_query_count(
                result["regeneration_route_queries"],
                result["regeneration_route_work_units"],
            )
            or result["preparation_checksum"] == 0
            or result["algorithm_session_checksum"] == 0
            or result["final_pool_manifest_checksum"] == 0
            or result["final_rejection_manifest_checksum"] == 0
        ):
            raise EvidenceError(f"{label} candidate accounting is inconsistent")
    if result["terminal_reason"] == 0 and (
        outcome["selected_net_count"] != result["workload_net_count"]
        or outcome["no_candidate_net_count"] != 0
        or outcome["total_overuse_units"] != 0
    ):
        raise EvidenceError(f"{label} feasible terminal claim is inconsistent")
    expected = compute_semantic_checksum(result)
    if result["semantic_checksum"] == 0 or result["semantic_checksum"] != expected:
        raise EvidenceError(f"{label}.semantic_checksum does not authenticate semantics")
    return result


_OBSERVATION_FIELDS = (
    "schema_version",
    "authority_kind",
    "authority_run_identity",
    "controller_identity",
    "process_instance_identity",
    "associated_semantic_checksum",
    "configured_wall_limit_nanoseconds",
    "configured_address_space_limit_bytes",
    "configured_peak_host_limit_bytes",
    "outer_elapsed_nanoseconds",
    "peak_host_bytes",
    "process_exit_code",
    "isolated_process",
    "wall_authority_enforced",
    "memory_authority_enforced",
    "persistent_preparer_reused",
    "preparer_lifecycle",
    "authority_checksum",
)


def compute_authority_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-EXTERNAL-AUTHORITY-V1")
    hashed.u32(value["schema_version"])
    hashed.byte(value["authority_kind"])
    for field in (
        "authority_run_identity",
        "controller_identity",
        "process_instance_identity",
        "associated_semantic_checksum",
        "configured_wall_limit_nanoseconds",
        "configured_address_space_limit_bytes",
        "configured_peak_host_limit_bytes",
        "outer_elapsed_nanoseconds",
        "peak_host_bytes",
    ):
        hashed.u64(value[field])
    hashed.i32(value["process_exit_code"])
    for field in (
        "isolated_process",
        "wall_authority_enforced",
        "memory_authority_enforced",
        "persistent_preparer_reused",
    ):
        hashed.boolean(value[field])
    for field in (
        "workers_started_before",
        "workers_started_after",
        "invocations_started_before",
        "invocations_started_after",
        "invocations_completed_before",
        "invocations_completed_after",
    ):
        hashed.u64(value["preparer_lifecycle"][field])
    return hashed.finish()


def _observation(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _OBSERVATION_FIELDS, label)
    _version(result, label)
    _enum(result["authority_kind"], {0}, f"{label}.authority_kind")
    for field in (
        "authority_run_identity",
        "controller_identity",
        "process_instance_identity",
        "associated_semantic_checksum",
        "configured_wall_limit_nanoseconds",
        "configured_address_space_limit_bytes",
        "configured_peak_host_limit_bytes",
        "outer_elapsed_nanoseconds",
        "peak_host_bytes",
        "authority_checksum",
    ):
        _u64(result[field], f"{label}.{field}")
    _i32(result["process_exit_code"], f"{label}.process_exit_code")
    for field in (
        "isolated_process",
        "wall_authority_enforced",
        "memory_authority_enforced",
        "persistent_preparer_reused",
    ):
        _bool(result[field], f"{label}.{field}")
    _lifecycle(result["preparer_lifecycle"], f"{label}.preparer_lifecycle")
    if result["authority_checksum"] == 0 or result[
        "authority_checksum"
    ] != compute_authority_checksum(result):
        raise EvidenceError(f"{label}.authority_checksum does not authenticate observation")
    return result


_RECORD_FIELDS = (
    "semantics",
    "case_build_elapsed_nanoseconds",
    "prepared_elapsed_nanoseconds",
    "cold_elapsed_nanoseconds",
    "preparer_lifecycle",
    "external_observation",
    "artifact_checksum",
)


def compute_record_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-TRIAL-ARM-ARTIFACT-V1")
    hashed.u64(value["semantics"]["semantic_checksum"])
    hashed.byte(value["semantics"]["execution_order"])
    hashed.u32(value["semantics"]["preparation_worker_count"])
    for field in (
        "case_build_elapsed_nanoseconds",
        "prepared_elapsed_nanoseconds",
        "cold_elapsed_nanoseconds",
    ):
        hashed.u64(value[field])
    for field in (
        "workers_started_before",
        "workers_started_after",
        "invocations_started_before",
        "invocations_started_after",
        "invocations_completed_before",
        "invocations_completed_after",
    ):
        hashed.u64(value["preparer_lifecycle"][field])
    hashed.u64(value["external_observation"]["authority_checksum"])
    return hashed.finish()


def _record(value: Any, label: str, *, expected_corpus_version: int = 1) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _RECORD_FIELDS, label)
    semantics = _semantics(
        result["semantics"],
        f"{label}.semantics",
        expected_corpus_version=expected_corpus_version,
    )
    for field in (
        "case_build_elapsed_nanoseconds",
        "prepared_elapsed_nanoseconds",
        "cold_elapsed_nanoseconds",
        "artifact_checksum",
    ):
        _u64(result[field], f"{label}.{field}")
    lifecycle = _lifecycle(result["preparer_lifecycle"], f"{label}.preparer_lifecycle")
    observation = _observation(result["external_observation"], f"{label}.external_observation")
    if lifecycle != observation["preparer_lifecycle"]:
        raise EvidenceError(f"{label} lifecycle copies differ")
    if observation["associated_semantic_checksum"] != semantics["semantic_checksum"]:
        raise EvidenceError(f"{label} observation is associated with another semantic result")
    budget = semantics["external_budget"]
    if (
        observation["configured_wall_limit_nanoseconds"]
        != budget["maximum_cold_elapsed_nanoseconds"]
        or observation["configured_address_space_limit_bytes"]
        != budget["maximum_address_space_bytes"]
        or observation["configured_peak_host_limit_bytes"] != budget["maximum_peak_host_bytes"]
    ):
        raise EvidenceError(f"{label} external authority caps differ from semantic budget")
    if (
        not observation["isolated_process"]
        or not observation["wall_authority_enforced"]
        or not observation["memory_authority_enforced"]
        or observation["process_exit_code"] != 0
        or observation["peak_host_bytes"] == 0
    ):
        raise EvidenceError(f"{label} lacks a successful isolated external authority")
    if (
        result["prepared_elapsed_nanoseconds"] > budget["maximum_prepared_elapsed_nanoseconds"]
        or result["cold_elapsed_nanoseconds"] > budget["maximum_cold_elapsed_nanoseconds"]
        or observation["outer_elapsed_nanoseconds"] > budget["maximum_cold_elapsed_nanoseconds"]
        or observation["peak_host_bytes"] > budget["maximum_peak_host_bytes"]
    ):
        raise EvidenceError(f"{label} exceeds an external budget")
    if (
        result["case_build_elapsed_nanoseconds"] + result["prepared_elapsed_nanoseconds"]
        > result["cold_elapsed_nanoseconds"]
        or observation["outer_elapsed_nanoseconds"] < result["cold_elapsed_nanoseconds"]
    ):
        raise EvidenceError(f"{label} timing intervals are not properly nested")
    if result["artifact_checksum"] == 0 or result["artifact_checksum"] != compute_record_checksum(
        result
    ):
        raise EvidenceError(f"{label}.artifact_checksum does not authenticate record")
    return result


_RESULT_FIELDS = (
    "schema_version",
    "baseline",
    "candidate",
    "comparison",
    "semantic_checksum",
    "artifact_checksum",
)


def _comparison(baseline: Mapping[str, Any], candidate: Mapping[str, Any]) -> int:
    left = baseline["semantics"]["outcome"]
    right = candidate["semantics"]["outcome"]
    if left["selected_net_count"] != right["selected_net_count"]:
        return 1 if left["selected_net_count"] > right["selected_net_count"] else 2
    if left["total_overuse_units"] != right["total_overuse_units"]:
        return 1 if left["total_overuse_units"] < right["total_overuse_units"] else 2
    if left["total_intrinsic_cost"] != right["total_intrinsic_cost"]:
        return 1 if left["total_intrinsic_cost"] < right["total_intrinsic_cost"] else 2
    return 0


def compute_paired_semantic_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-PAIRED-TRIAL-SEMANTIC-V1")
    hashed.u32(value["schema_version"])
    hashed.u64(value["baseline"]["semantics"]["semantic_checksum"])
    hashed.u64(value["candidate"]["semantics"]["semantic_checksum"])
    hashed.byte(value["comparison"])
    return hashed.finish()


def compute_paired_artifact_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-PAIRED-TRIAL-ARTIFACT-V1")
    hashed.u64(value["semantic_checksum"])
    hashed.byte(value["baseline"]["semantics"]["execution_order"])
    hashed.u64(value["baseline"]["artifact_checksum"])
    hashed.u64(value["candidate"]["artifact_checksum"])
    return hashed.finish()


def _pair_identity(semantics: Mapping[str, Any]) -> tuple[Any, ...]:
    return tuple(
        semantics[field]
        for field in (
            "schema_version",
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
        )
    )


def _paired_result(
    value: Any, label: str, *, expected_corpus_version: int = 1
) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _RESULT_FIELDS, label)
    _version(result, label)
    baseline = _record(
        result["baseline"],
        f"{label}.baseline",
        expected_corpus_version=expected_corpus_version,
    )
    candidate = _record(
        result["candidate"],
        f"{label}.candidate",
        expected_corpus_version=expected_corpus_version,
    )
    comparison = _enum(result["comparison"], {0, 1, 2}, f"{label}.comparison")
    _u64(result["semantic_checksum"], f"{label}.semantic_checksum")
    _u64(result["artifact_checksum"], f"{label}.artifact_checksum")
    if baseline["semantics"]["arm"] != 0 or candidate["semantics"]["arm"] != 1:
        raise EvidenceError(f"{label} arms are reversed or duplicated")
    if _pair_identity(baseline["semantics"]) != _pair_identity(candidate["semantics"]):
        raise EvidenceError(f"{label} arm identities or equal-budget opportunities differ")
    if comparison != _comparison(baseline, candidate):
        raise EvidenceError(f"{label}.comparison is inconsistent with board outcomes")
    if result["semantic_checksum"] == 0 or result[
        "semantic_checksum"
    ] != compute_paired_semantic_checksum(result):
        raise EvidenceError(f"{label}.semantic_checksum does not authenticate pair")
    if result["artifact_checksum"] == 0 or result[
        "artifact_checksum"
    ] != compute_paired_artifact_checksum(result):
        raise EvidenceError(f"{label}.artifact_checksum does not authenticate pair")
    return result


_ATTEMPT_FIELDS = (
    "schema_version",
    "arm",
    "repetition_index",
    "execution_order",
    "disposition",
    "dispatch_ordinal",
    "process_instance_identity",
    "outer_elapsed_nanoseconds",
    "process_lifetime_peak_host_bytes",
    "raw_wait_status",
    "process_exit_code",
    "terminating_signal",
    "watchdog_kill_sent",
    "controller_invariant_id",
    "controller_detail",
    "record",
    "child_failure",
    "attempt_checksum",
)

_FAILURE_FIELDS = (
    "schema_version",
    "summary_code",
    "arm",
    "summary_required",
    "summary_configured",
    "summary_invariant_id",
    "summary_detail",
    "payload_kind",
    "child_error_code",
    "child_invariant_id",
    "child_detail",
    "has_child_net",
    "child_net_id",
    "child_net_generation",
    "child_required",
    "child_configured",
    "child_bound_kind",
    "child_secondary_required",
    "child_secondary_configured",
    "has_case_identity",
    "case_id",
    "descriptor_fingerprint",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "capacity_model_checksum",
    "has_epoch_index",
    "epoch_index",
    "has_failed_observation",
    "failed_observation_checksum",
    "attempted_column_count",
    "attempted_route_queries",
    "attempted_route_work_units",
    "candidate_store_publication_committed",
    "authoritative_candidate_store_present",
    "reconciled_candidate_count",
    "reconciled_rejection_count",
    "reconciled_candidate_store_checksum",
    "payload_checksum",
)


def compute_failure_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-DURABLE-ARM-FAILURE-V1")
    hashed.u32(value["schema_version"])
    hashed.byte(value["summary_code"])
    hashed.byte(value["arm"])
    hashed.u64(value["summary_required"])
    hashed.u64(value["summary_configured"])
    hashed.string(value["summary_invariant_id"])
    hashed.string(value["summary_detail"])
    hashed.byte(value["payload_kind"])
    hashed.byte(value["child_error_code"])
    hashed.string(value["child_invariant_id"])
    hashed.string(value["child_detail"])
    hashed.boolean(value["has_child_net"])
    hashed.u64(value["child_net_id"])
    hashed.u32(value["child_net_generation"])
    hashed.u64(value["child_required"])
    hashed.u64(value["child_configured"])
    hashed.byte(value["child_bound_kind"])
    hashed.u64(value["child_secondary_required"])
    hashed.u64(value["child_secondary_configured"])
    hashed.boolean(value["has_case_identity"])
    hashed.u32(value["case_id"])
    for field in (
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
    ):
        hashed.u64(value[field])
    hashed.boolean(value["has_epoch_index"])
    hashed.u32(value["epoch_index"])
    hashed.boolean(value["has_failed_observation"])
    for field in (
        "failed_observation_checksum",
        "attempted_column_count",
        "attempted_route_queries",
        "attempted_route_work_units",
    ):
        hashed.u64(value[field])
    hashed.boolean(value["candidate_store_publication_committed"])
    hashed.boolean(value["authoritative_candidate_store_present"])
    for field in (
        "reconciled_candidate_count",
        "reconciled_rejection_count",
        "reconciled_candidate_store_checksum",
    ):
        hashed.u64(value[field])
    return hashed.finish()


def _failure(
    value: Any,
    label: str,
    *,
    expected_manifest_case: Mapping[str, Any] | None = None,
    requested_pool_size: int | None = None,
) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _FAILURE_FIELDS, label)
    _version(result, label)
    _enum(result["summary_code"], set(range(15)), f"{label}.summary_code")
    arm = _enum(result["arm"], {0, 1}, f"{label}.arm")
    payload_kind = _enum(result["payload_kind"], set(range(5)), f"{label}.payload_kind")
    child_code_maximum = {0: 0, 1: 10, 2: 12, 3: 10, 4: 10}[payload_kind]
    _uint(result["child_error_code"], child_code_maximum, f"{label}.child_error_code")
    child_bound_maximum = 3 if payload_kind == 1 else 0
    _uint(result["child_bound_kind"], child_bound_maximum, f"{label}.child_bound_kind")
    for field in (
        "summary_required",
        "summary_configured",
        "child_net_id",
        "child_required",
        "child_configured",
        "child_secondary_required",
        "child_secondary_configured",
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
        "failed_observation_checksum",
        "attempted_column_count",
        "attempted_route_queries",
        "attempted_route_work_units",
        "reconciled_candidate_count",
        "reconciled_rejection_count",
        "reconciled_candidate_store_checksum",
        "payload_checksum",
    ):
        _u64(result[field], f"{label}.{field}")
    for field in ("child_net_generation", "case_id", "epoch_index"):
        _u32(result[field], f"{label}.{field}")
    for field in (
        "summary_invariant_id",
        "summary_detail",
        "child_invariant_id",
        "child_detail",
    ):
        _string(result[field], f"{label}.{field}")
    for field in (
        "has_child_net",
        "has_case_identity",
        "has_epoch_index",
        "has_failed_observation",
        "candidate_store_publication_committed",
        "authoritative_candidate_store_present",
    ):
        _bool(result[field], f"{label}.{field}")

    if not result["has_child_net"] and (
        result["child_net_id"] != 0 or result["child_net_generation"] != 0
    ):
        raise EvidenceError(f"{label} absent child net must use canonical zero identity")
    identity_fields = (
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
    )
    if result["has_case_identity"]:
        if result["case_id"] == 0 or any(result[field] == 0 for field in identity_fields):
            raise EvidenceError(f"{label} present case identity must be complete and nonzero")
    elif any(result[field] != 0 for field in identity_fields):
        raise EvidenceError(f"{label} absent case identity must use canonical zero fingerprints")
    if not result["has_epoch_index"] and result["epoch_index"] != 0:
        raise EvidenceError(f"{label} absent epoch index must use canonical zero")
    if not result["has_failed_observation"] and (
        result["failed_observation_checksum"] != 0
        or (payload_kind != 1 and result["attempted_column_count"] != 0)
        or result["attempted_route_queries"] != 0
        or result["attempted_route_work_units"] != 0
    ):
        raise EvidenceError(f"{label} absent failed observation must use canonical zero counters")
    if result["has_failed_observation"] and result["failed_observation_checksum"] == 0:
        raise EvidenceError(f"{label} present failed observation must have a checksum")
    if result["has_failed_observation"] and (
        result["attempted_column_count"] != result["attempted_route_queries"]
        or result["attempted_route_work_units"]
        > result["attempted_route_queries"] * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
    ):
        raise EvidenceError(f"{label} observed attempt counters exceed canonical query work")
    if not result["authoritative_candidate_store_present"] and (
        result["reconciled_candidate_count"] != 0
        or result["reconciled_rejection_count"] != 0
        or result["reconciled_candidate_store_checksum"] != 0
    ):
        raise EvidenceError(f"{label} absent candidate store must use canonical zero roster")
    if (
        result["authoritative_candidate_store_present"]
        and result["reconciled_candidate_store_checksum"] == 0
    ):
        raise EvidenceError(f"{label} present candidate store must have a reconciliation checksum")
    if (
        result["candidate_store_publication_committed"]
        and not result["authoritative_candidate_store_present"]
    ):
        raise EvidenceError(f"{label} committed candidate store must remain authoritative")

    if (result["summary_code"] == 6 and arm != 0) or (
        result["summary_code"] in {7, 8} and arm != 1
    ):
        raise EvidenceError(f"{label} summary code contradicts its arm")
    has_child_diagnostic = (
        result["child_error_code"] != 0
        or bool(result["child_invariant_id"])
        or bool(result["child_detail"])
        or result["has_child_net"]
        or result["child_required"] != 0
        or result["child_configured"] != 0
        or result["child_bound_kind"] != 0
        or result["child_secondary_required"] != 0
        or result["child_secondary_configured"] != 0
    )
    has_case_fingerprint = (
        result["has_case_identity"]
        or result["case_id"] != 0
        or any(result[field] != 0 for field in identity_fields)
    )
    has_observation = (
        result["has_failed_observation"]
        or result["failed_observation_checksum"] != 0
        or result["attempted_column_count"] != 0
        or result["attempted_route_queries"] != 0
        or result["attempted_route_work_units"] != 0
    )
    has_store = (
        result["candidate_store_publication_committed"]
        or result["authoritative_candidate_store_present"]
        or result["reconciled_candidate_count"] != 0
        or result["reconciled_rejection_count"] != 0
        or result["reconciled_candidate_store_checksum"] != 0
    )

    if payload_kind == 0:
        if (
            result["summary_code"] in {4, 6, 7, 8, 9, 11, 12}
            or has_child_diagnostic
            or has_case_fingerprint
            or result["has_epoch_index"]
            or result["epoch_index"] != 0
            or has_observation
            or has_store
        ):
            raise EvidenceError(f"{label} summary-only payload contains child state")
    elif payload_kind == 1:
        work_bound = result["child_error_code"] == 10
        node_witness = result["child_required"] > result["child_configured"]
        host_witness = result["child_secondary_required"] > result["child_secondary_configured"]
        valid_work_bound_witness = (
            result["child_bound_kind"] == 1
            and node_witness
            or result["child_bound_kind"] == 2
            and host_witness
            or result["child_bound_kind"] == 3
            and node_witness
            and host_witness
        )
        if (
            result["summary_code"] != 4
            or result["has_case_identity"]
            or result["has_epoch_index"]
            or result["has_failed_observation"]
            or result["attempted_route_queries"] != 0
            or result["attempted_route_work_units"] != 0
            or has_store
            or (
                work_bound
                and (
                    not result["has_child_net"]
                    or result["child_net_id"] == 0
                    or not valid_work_bound_witness
                )
            )
            or (
                not work_bound
                and (
                    result["has_child_net"]
                    or result["child_required"] != 0
                    or result["child_configured"] != 0
                    or result["child_bound_kind"] != 0
                    or result["child_secondary_required"] != 0
                    or result["child_secondary_configured"] != 0
                    or result["attempted_column_count"] != 0
                )
            )
        ):
            raise EvidenceError(f"{label} corpus payload contradicts its bound-witness state")
    elif payload_kind == 2:
        if (
            result["summary_code"] != 6
            or arm != 0
            or not result["has_case_identity"]
            or result["child_bound_kind"] != 0
            or result["child_secondary_required"] != 0
            or result["child_secondary_configured"] != 0
            or result["has_epoch_index"]
            or has_observation
            or has_store
        ):
            raise EvidenceError(f"{label} sequential payload contains incompatible state")
    elif payload_kind == 3:
        if (
            result["summary_code"] != 7
            or arm != 1
            or not result["has_case_identity"]
            or result["child_bound_kind"] != 0
            or result["child_secondary_required"] != 0
            or result["child_secondary_configured"] != 0
            or result["has_epoch_index"]
            or (
                result["has_failed_observation"]
                and result["candidate_store_publication_committed"]
                != result["authoritative_candidate_store_present"]
            )
            or (not result["has_failed_observation"] and has_store)
        ):
            raise EvidenceError(
                f"{label} candidate-preparation payload contains incompatible state"
            )
    elif (
        result["summary_code"] != 8
        or arm != 1
        or not result["has_case_identity"]
        or result["has_child_net"]
        or result["child_required"] != 0
        or result["child_configured"] != 0
        or result["child_bound_kind"] != 0
        or result["child_secondary_required"] != 0
        or result["child_secondary_configured"] != 0
        or not result["authoritative_candidate_store_present"]
    ):
        raise EvidenceError(f"{label} candidate-session payload contains incompatible state")
    if expected_manifest_case is not None:
        expected_case_id = expected_manifest_case["case_id"]
        if payload_kind == 1:
            if result["case_id"] != expected_case_id:
                raise EvidenceError(f"{label} corpus failure is associated with another case")
        elif payload_kind in {2, 3, 4} and (
            result["case_id"] != expected_case_id
            or any(result[field] != expected_manifest_case[field] for field in identity_fields)
        ):
            raise EvidenceError(f"{label} typed failure is associated with another case")
        if result["has_failed_observation"]:
            net_count = expected_manifest_case["workload_net_count"]
            if payload_kind == 3:
                if requested_pool_size is None:
                    raise EvidenceError(f"{label} has no preparation pool context")
                maximum_attempted_columns = net_count * requested_pool_size
            elif payload_kind == 4:
                maximum_attempted_columns = net_count
            else:
                maximum_attempted_columns = 0
            if result["attempted_column_count"] > maximum_attempted_columns:
                raise EvidenceError(
                    f"{label} observed attempt columns exceed the active case bound"
                )
    if result["payload_checksum"] == 0 or result["payload_checksum"] != compute_failure_checksum(
        result
    ):
        raise EvidenceError(f"{label}.payload_checksum does not authenticate failure")
    return result


def compute_arm_attempt_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-ISOLATED-ARM-ATTEMPT-V1")
    hashed.u32(value["schema_version"])
    hashed.byte(value["arm"])
    hashed.u32(value["repetition_index"])
    hashed.byte(value["execution_order"])
    hashed.byte(value["disposition"])
    hashed.u64(value["dispatch_ordinal"])
    hashed.u64(value["process_instance_identity"])
    hashed.u64(value["outer_elapsed_nanoseconds"])
    hashed.u64(value["process_lifetime_peak_host_bytes"])
    hashed.u32(value["raw_wait_status"] & _U32_MAX)
    hashed.u32(value["process_exit_code"] & _U32_MAX)
    hashed.u32(value["terminating_signal"] & _U32_MAX)
    hashed.boolean(value["watchdog_kill_sent"])
    hashed.string(value["controller_invariant_id"])
    hashed.string(value["controller_detail"])
    hashed.boolean(value["record"] is not None)
    hashed.u64(value["record"]["artifact_checksum"] if value["record"] is not None else 0)
    hashed.boolean(value["child_failure"] is not None)
    hashed.u64(
        value["child_failure"]["payload_checksum"] if value["child_failure"] is not None else 0
    )
    return hashed.finish()


def _arm_attempt(
    value: Any,
    label: str,
    *,
    expected_manifest_case: Mapping[str, Any] | None = None,
    requested_pool_size: int | None = None,
    require_success: bool = True,
    expected_corpus_version: int = 1,
) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _ATTEMPT_FIELDS, label)
    _version(result, label)
    _enum(result["arm"], {0, 1}, f"{label}.arm")
    _enum(result["execution_order"], {0, 1}, f"{label}.execution_order")
    disposition = _enum(result["disposition"], set(range(11)), f"{label}.disposition")
    for field in (
        "repetition_index",
        "dispatch_ordinal",
        "process_instance_identity",
        "outer_elapsed_nanoseconds",
        "process_lifetime_peak_host_bytes",
        "attempt_checksum",
    ):
        (_u32 if field == "repetition_index" else _u64)(result[field], f"{label}.{field}")
    raw_wait_status = _i32(result["raw_wait_status"], f"{label}.raw_wait_status")
    if not 0 <= raw_wait_status <= 0xFFFF:
        raise EvidenceError(f"{label}.raw_wait_status is not an exact Linux wait status")
    for field in ("process_exit_code", "terminating_signal"):
        _i32(result[field], f"{label}.{field}")
    _bool(result["watchdog_kill_sent"], f"{label}.watchdog_kill_sent")
    _string(result["controller_invariant_id"], f"{label}.controller_invariant_id")
    _string(result["controller_detail"], f"{label}.controller_detail")
    if result["record"] is not None:
        _record(
            result["record"],
            f"{label}.record",
            expected_corpus_version=expected_corpus_version,
        )
    if result["child_failure"] is not None:
        _failure(
            result["child_failure"],
            f"{label}.child_failure",
            expected_manifest_case=expected_manifest_case,
            requested_pool_size=requested_pool_size,
        )
    if disposition == 0 and (result["record"] is None or result["child_failure"] is not None):
        raise EvidenceError(f"{label} success must contain exactly one finalized record")
    if disposition == 1 and (result["record"] is not None or result["child_failure"] is None):
        raise EvidenceError(f"{label} typed failure must contain exactly one child failure")
    if disposition >= 2 and (result["record"] is not None or result["child_failure"] is not None):
        raise EvidenceError(f"{label} controller failure must not contain a child payload")
    if disposition == 0:
        if any(
            result[field] != 0
            for field in ("raw_wait_status", "process_exit_code", "terminating_signal")
        ):
            raise EvidenceError(f"{label} successful worker did not exit cleanly")
        if (
            result["watchdog_kill_sent"]
            or result["controller_invariant_id"]
            or result["controller_detail"]
        ):
            raise EvidenceError(f"{label} success contains a controller failure witness")
        if (
            result["process_instance_identity"] == 0
            or result["process_lifetime_peak_host_bytes"] == 0
        ):
            raise EvidenceError(f"{label} successful process identity and peak must be nonzero")
    elif disposition == 1:
        process_authority_witness = result["controller_invariant_id"] in {
            "P4HARNESS-REAP-BOUNDED-001",
            "P4HARNESS-WAIT4-AUTHORITY-001",
        } and bool(result["controller_detail"])
        if (
            result["process_instance_identity"] == 0
            or result["watchdog_kill_sent"]
            or bool(result["controller_invariant_id"]) != bool(result["controller_detail"])
            or (result["controller_invariant_id"] and not process_authority_witness)
            or (result["process_lifetime_peak_host_bytes"] == 0 and not process_authority_witness)
        ):
            raise EvidenceError(f"{label} typed child failure has incompatible process state")
    elif disposition == 4:
        if (
            result["process_instance_identity"] == 0
            or result["terminating_signal"] <= 0
            or result["raw_wait_status"] == 0
        ):
            raise EvidenceError(f"{label} signal disposition lacks a signal exit witness")
    elif disposition == 5:
        if (
            result["process_instance_identity"] == 0
            or result["process_exit_code"] <= 0
            or result["raw_wait_status"] == 0
        ):
            raise EvidenceError(f"{label} nonzero-exit disposition lacks an exit witness")
    elif disposition == 7 and (
        result["process_instance_identity"] != 0 or result["process_lifetime_peak_host_bytes"] != 0
    ):
        raise EvidenceError(f"{label} launch failure cannot name a child process")
    if result["process_instance_identity"] == 0 and (
        result["process_lifetime_peak_host_bytes"] != 0
        or result["raw_wait_status"] != 0
        or result["process_exit_code"] != -1
        or result["terminating_signal"] != 0
        or result["watchdog_kill_sent"]
    ):
        raise EvidenceError(f"{label} processless attempt contains child process state")
    if disposition in {2, 3, 4, 5, 6, 7, 8, 9} and (
        not result["controller_invariant_id"] or not result["controller_detail"]
    ):
        raise EvidenceError(f"{label} controller failure lacks its durable witness")
    if result["attempt_checksum"] == 0 or result[
        "attempt_checksum"
    ] != compute_arm_attempt_checksum(result):
        raise EvidenceError(f"{label}.attempt_checksum does not authenticate attempt")
    if require_success and disposition != 0:
        raise EvidenceError(f"{label} is not a successful attempt")
    return result


_PAIR_ATTEMPT_FIELDS = (
    "schema_version",
    "case_id",
    "requested_pool_size",
    "repetition_index",
    "root_seed",
    "execution_order",
    "baseline",
    "candidate",
    "result",
    "attempt_checksum",
)


def compute_pair_attempt_checksum(value: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-ISOLATED-PAIR-ATTEMPT-V1")
    hashed.u32(value["schema_version"])
    hashed.u32(value["case_id"])
    hashed.u32(value["requested_pool_size"])
    hashed.u32(value["repetition_index"])
    hashed.u64(value["root_seed"])
    hashed.byte(value["execution_order"])
    hashed.u64(value["baseline"]["attempt_checksum"])
    hashed.u64(value["candidate"]["attempt_checksum"])
    hashed.boolean(value["result"] is not None)
    hashed.u64(value["result"]["artifact_checksum"] if value["result"] is not None else 0)
    return hashed.finish()


def _pair_attempt(
    value: Any,
    label: str,
    *,
    expected_manifest_case: Mapping[str, Any] | None = None,
    requested_pool_size: int | None = None,
    require_success: bool = True,
    expected_corpus_version: int = 1,
) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _PAIR_ATTEMPT_FIELDS, label)
    _version(result, label)
    for field in ("case_id", "requested_pool_size", "repetition_index"):
        _u32(result[field], f"{label}.{field}")
    _u64(result["root_seed"], f"{label}.root_seed")
    _enum(result["execution_order"], {0, 1}, f"{label}.execution_order")
    baseline = _arm_attempt(
        result["baseline"],
        f"{label}.baseline",
        expected_manifest_case=expected_manifest_case,
        requested_pool_size=requested_pool_size,
        require_success=require_success,
        expected_corpus_version=expected_corpus_version,
    )
    candidate = _arm_attempt(
        result["candidate"],
        f"{label}.candidate",
        expected_manifest_case=expected_manifest_case,
        requested_pool_size=requested_pool_size,
        require_success=require_success,
        expected_corpus_version=expected_corpus_version,
    )
    if result["result"] is None:
        if require_success:
            raise EvidenceError(f"{label}.result must contain a completed pair")
        if baseline["disposition"] == 0 and candidate["disposition"] == 0:
            raise EvidenceError(f"{label} omits a pair despite two successful arms")
    else:
        paired = _paired_result(
            result["result"],
            f"{label}.result",
            expected_corpus_version=expected_corpus_version,
        )
        if baseline["disposition"] != 0 or candidate["disposition"] != 0:
            raise EvidenceError(f"{label} failed arm cannot carry a completed pair")
        if baseline["record"] != paired["baseline"] or candidate["record"] != paired["candidate"]:
            raise EvidenceError(f"{label} attempt records differ from paired result copies")
    if result["attempt_checksum"] == 0 or result[
        "attempt_checksum"
    ] != compute_pair_attempt_checksum(result):
        raise EvidenceError(f"{label}.attempt_checksum does not authenticate pair attempt")
    return result


_TOP_FIELDS = (
    "wire_schema_version",
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "config",
    "environment",
    "corpus_checksum",
    "cell_plan_checksum",
    "authority_run_identity",
    "controller_identity",
    "attempts",
    "artifact_checksum",
)
_SAME_RUN_TOP_FIELDS = ("raw_evidence_schema_version",) + _TOP_FIELDS
_SAME_RUN_RAW_EVIDENCE_SCHEMA_VERSION = 2
_SAME_RUN_WIRE_SCHEMA_VERSION = 2


def compute_cell_plan_checksum(document: Mapping[str, Any], *, corpus_version: int = 1) -> int:
    if corpus_version not in {1, 2}:
        raise EvidenceError("cell-plan checksum corpus version must be 1 or 2")
    config = document["config"]
    hashed = StableHashBuilder()
    hashed.string(
        "APGAR-PHASE4-CANONICAL-CELL-PLAN-V1"
        if corpus_version == 1
        else "APGAR-PHASE4-CANONICAL-CELL-PLAN-V2"
    )
    hashed.u32(config["schema_version"])
    hashed.u64(document["corpus_checksum"])
    for field in ("case_id", "requested_pool_size", "preparation_worker_count", "repetitions"):
        hashed.u32(config[field])
    hashed.u64(config["maximum_setup_elapsed_nanoseconds"])
    for field in (
        "maximum_prepared_elapsed_nanoseconds",
        "maximum_cold_elapsed_nanoseconds",
        "maximum_address_space_bytes",
        "maximum_peak_host_bytes",
    ):
        hashed.u64(config["external_budget"][field])
    for field in (
        "maximum_nets",
        "maximum_compiled_nodes",
        "maximum_compiled_host_bytes",
        "maximum_active_regions",
        "maximum_board_entities",
    ):
        hashed.u64(config["corpus_limits"][field])
    return hashed.finish()


def compute_cell_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    if "raw_evidence_schema_version" in document:
        hashed.string("APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V2")
        hashed.u32(document["raw_evidence_schema_version"])
        hashed.u32(document["wire_schema_version"])
    else:
        hashed.string("APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V1")
    hashed.u32(document["schema_version"])
    hashed.u64(document["corpus_checksum"])
    hashed.u64(document["cell_plan_checksum"])
    hashed.u64(document["environment"]["environment_checksum"])
    hashed.u64(document["authority_run_identity"])
    hashed.u64(document["controller_identity"])
    hashed.u64(len(document["attempts"]))
    for attempt in document["attempts"]:
        hashed.u64(attempt["attempt_checksum"])
    return hashed.finish()


def compute_canonical_budget_checksum(
    document: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    canonical_algorithm_budget_checksum: int,
    *,
    corpus_version: int = 1,
) -> int:
    if corpus_version not in {1, 2}:
        raise EvidenceError("canonical budget checksum corpus version must be 1 or 2")
    config = document["config"]
    net_count = manifest_case["workload_net_count"]
    pool_size = config["requested_pool_size"]
    route_queries = net_count * (pool_size + 2)
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-PAIRED-BUDGET-V1")
    hashed.u32(1)
    hashed.u32(corpus_version)
    hashed.u64(document["corpus_checksum"])
    hashed.u32(config["case_id"])
    hashed.u64(manifest_case["descriptor_fingerprint"])
    hashed.u32(pool_size)
    hashed.u64(compute_canonical_root_seed(document, corpus_version=corpus_version))
    for field in _LIMIT_FIELDS:
        hashed.u64(config["corpus_limits"][field])
    hashed.u32(net_count)
    hashed.u64(route_queries)
    hashed.u64(route_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY)
    hashed.u64(net_count)
    hashed.u32(pool_size + 1)
    hashed.u64(canonical_algorithm_budget_checksum)
    for field in _BUDGET_FIELDS:
        hashed.u64(config["external_budget"][field])
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    if "raw_evidence_schema_version" in document:
        hashed.string("APGAR-PHASE4-SOURCE-ENVELOPE-V2")
        hashed.u32(document["raw_evidence_schema_version"])
    else:
        hashed.string("APGAR-PHASE4-SOURCE-ENVELOPE-V1")
    hashed.u32(document["wire_schema_version"])
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def compute_canonical_root_seed(document: Mapping[str, Any], *, corpus_version: int = 1) -> int:
    if corpus_version not in {1, 2}:
        raise EvidenceError("canonical root corpus version must be 1 or 2")
    config = document["config"]
    hashed = StableHashBuilder()
    hashed.string(
        "APGAR-PHASE4-CANONICAL-ROOT-V1"
        if corpus_version == 1
        else "APGAR-PHASE4-CANONICAL-ROOT-V2"
    )
    hashed.u64(document["corpus_checksum"])
    hashed.u32(config["case_id"])
    hashed.u32(config["requested_pool_size"])
    result = hashed.finish()
    return result if result != 0 else 1


def _validate_root_checksums(document: Mapping[str, Any], source_envelope_checksum: int) -> None:
    if document["artifact_checksum"] != compute_cell_artifact_checksum(document):
        raise EvidenceError("artifact_checksum does not authenticate the raw cell")
    if (
        source_envelope_checksum == 0
        or source_envelope_checksum != compute_source_envelope_checksum(document)
    ):
        raise EvidenceError("source_envelope_checksum does not authenticate source provenance")


def _validate_success_record_association(
    document: Mapping[str, Any],
    config: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    expected_budget_checksum: int,
    attempt: Mapping[str, Any],
    arm: Mapping[str, Any],
    *,
    repetition: int,
    expected_order: int,
    expected_arm: int,
    label: str,
) -> None:
    record = arm["record"]
    if record is None:
        raise EvidenceError(f"{label} successful arm is missing its record")
    semantics = record["semantics"]
    observation = record["external_observation"]
    net_count = semantics["workload_net_count"]
    pool_size = config["requested_pool_size"]
    expected_queries = net_count * (pool_size + 2)
    if (
        semantics["baseline_sweeps"] != pool_size + 2
        or semantics["candidate_regeneration_epochs"] != 2
        or semantics["candidate_columns_per_epoch"] != net_count
        or semantics["candidate_terminal_selection_rounds"] != pool_size + 1
        or semantics["opportunity"]["route_queries"] != expected_queries
        or semantics["opportunity"]["route_work_units"]
        != expected_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
    ):
        raise EvidenceError(f"{label} does not use the canonical equal-budget shape")
    if expected_arm == 1:
        maximum_preparation_queries = net_count * pool_size
        maximum_regeneration_queries = 2 * net_count
        if (
            semantics["preparation_route_queries"] > maximum_preparation_queries
            or semantics["preparation_route_work_units"]
            > maximum_preparation_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
            or semantics["regeneration_route_queries"] > maximum_regeneration_queries
            or semantics["regeneration_route_work_units"]
            > maximum_regeneration_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
        ):
            raise EvidenceError(f"{label} candidate component work exceeds its canonical cap")
    if (
        arm["arm"] != expected_arm
        or arm["repetition_index"] != repetition
        or arm["execution_order"] != expected_order
        or semantics["arm"] != expected_arm
        or semantics["repetition_index"] != repetition
        or semantics["execution_order"] != expected_order
        or semantics["case_id"] != config["case_id"]
        or semantics["requested_pool_size"] != config["requested_pool_size"]
        or semantics["preparation_worker_count"] != config["preparation_worker_count"]
        or semantics["root_seed"] != attempt["root_seed"]
        or semantics["corpus_checksum"] != document["corpus_checksum"]
        or semantics["workload_net_count"] != manifest_case["workload_net_count"]
        or semantics["descriptor_fingerprint"] != manifest_case["descriptor_fingerprint"]
        or semantics["case_checksum"] != manifest_case["case_checksum"]
        or semantics["board_content_hash"] != manifest_case["board_content_hash"]
        or semantics["workload_checksum"] != manifest_case["workload_checksum"]
        or semantics["capacity_model_checksum"] != manifest_case["capacity_model_checksum"]
        or semantics["budget_checksum"] != expected_budget_checksum
        or semantics["external_budget"] != config["external_budget"]
    ):
        raise EvidenceError(f"{label} arm record is associated with another command or cell")
    if (
        observation["authority_run_identity"] != document["authority_run_identity"]
        or observation["controller_identity"] != document["controller_identity"]
        or observation["process_instance_identity"] != arm["process_instance_identity"]
        or observation["outer_elapsed_nanoseconds"] != arm["outer_elapsed_nanoseconds"]
        or observation["peak_host_bytes"] != arm["process_lifetime_peak_host_bytes"]
    ):
        raise EvidenceError(f"{label} external observation is associated with another process")
    lifecycle = record["preparer_lifecycle"]
    if expected_arm == 0:
        if (
            any(lifecycle[field] != 0 for field in _LIFECYCLE_FIELDS)
            or observation["persistent_preparer_reused"]
        ):
            raise EvidenceError(f"{label} baseline lifecycle is invalid")
    else:
        workers = config["preparation_worker_count"]
        if (
            not observation["persistent_preparer_reused"]
            or lifecycle["workers_started_before"] != workers
            or lifecycle["workers_started_after"] != workers
            or lifecycle["invocations_started_before"] != lifecycle["invocations_completed_before"]
            or lifecycle["invocations_started_before"] == 0
            or lifecycle["invocations_started_after"] != lifecycle["invocations_started_before"] + 1
            or lifecycle["invocations_completed_after"]
            != lifecycle["invocations_completed_before"] + 1
        ):
            raise EvidenceError(f"{label} candidate lifecycle is invalid")


def _validate_total_attempts(
    document: Mapping[str, Any],
    attempts: Sequence[Any],
    config: Mapping[str, Any],
    manifest_case: Mapping[str, Any],
    expected_budget_checksum: int,
    *,
    expected_corpus_version: int,
) -> bool:
    """Authenticate every attempt before classifying cell completeness."""
    complete = True
    expected_root_seed = compute_canonical_root_seed(
        document, corpus_version=expected_corpus_version
    )
    ordered_arms: list[Mapping[str, Any]] = []
    process_states: list[tuple[int, int, int, int, int] | None] = [None, None]
    process_attempts: list[list[Mapping[str, Any]]] = [[], []]
    deterministic_semantics: list[Mapping[str, Any] | None] = [None, None]
    deterministic_comparison: int | None = None
    # The persistent preparer performs one warm-up before any measured arm.
    # Successful records expose the counter around their own invocation. A
    # controller-side finalization or pair-assembly failure discards that
    # record after the invocation has already completed, so retain those
    # authenticated lost invocations in the expected counter as well.
    expected_candidate_invocations_before = 1
    lost_candidate_invocation_witnesses = {
        (6, "P4PAIR-FINALIZE-001"),
        (6, "P4PAIR-FINALIZE-002"),
        (6, "P4PAIR-FINALIZE-003"),
        (6, "P4PAIR-FINALIZE-004"),
        (8, "P4PAIR-FINALIZE-005"),
        (6, "P4PAIR-FINALIZE-006"),
        (6, "P4PAIR-FINALIZE-AUTHORITY-001"),
        (6, "P4PAIR-FINALIZE-LIFECYCLE-001"),
        (6, "P4PAIR-FINALIZE-SEMANTICS-001"),
        (6, "P4PAIR-ASSEMBLE-001"),
        (6, "P4PAIR-ASSEMBLE-002"),
        (6, "P4PAIR-ASSEMBLE-ARTIFACT-001"),
        (6, "P4PAIR-ASSEMBLE-AUTHORITY-001"),
        (6, "P4HARNESS-SAME-RUN-MISSING-001"),
    }
    for repetition, raw_attempt in enumerate(attempts):
        label = f"attempts[{repetition}]"
        attempt = _pair_attempt(
            raw_attempt,
            label,
            expected_manifest_case=manifest_case,
            requested_pool_size=config["requested_pool_size"],
            require_success=False,
            expected_corpus_version=expected_corpus_version,
        )
        expected_order = repetition % 2
        if (
            attempt["case_id"] != config["case_id"]
            or attempt["requested_pool_size"] != config["requested_pool_size"]
            or attempt["repetition_index"] != repetition
            or attempt["execution_order"] != expected_order
        ):
            raise EvidenceError(
                f"{label} is missing, extra, reordered, or associated with another cell"
            )
        if attempt["root_seed"] != expected_root_seed:
            raise EvidenceError(f"{label}.root_seed is not the canonical cell root")
        for arm_name, expected_arm in (("baseline", 0), ("candidate", 1)):
            arm = attempt[arm_name]
            if (
                arm["arm"] != expected_arm
                or arm["repetition_index"] != repetition
                or arm["execution_order"] != expected_order
            ):
                raise EvidenceError(f"{label}.{arm_name} is associated with another command")
            if arm["disposition"] == 0:
                _validate_success_record_association(
                    document,
                    config,
                    manifest_case,
                    expected_budget_checksum,
                    attempt,
                    arm,
                    repetition=repetition,
                    expected_order=expected_order,
                    expected_arm=expected_arm,
                    label=f"{label}.{arm_name}",
                )
                if expected_arm == 1:
                    lifecycle = arm["record"]["preparer_lifecycle"]
                    if (
                        lifecycle["invocations_started_before"]
                        != expected_candidate_invocations_before
                        or lifecycle["invocations_completed_before"]
                        != expected_candidate_invocations_before
                    ):
                        raise EvidenceError("successful candidate lifecycle is not continuous")
                    expected_candidate_invocations_before += 1
                semantics = arm["record"]["semantics"]
                normalized_semantics = {
                    field: field_value
                    for field, field_value in semantics.items()
                    if field not in {"execution_order", "repetition_index", "semantic_checksum"}
                }
                if deterministic_semantics[expected_arm] is None:
                    deterministic_semantics[expected_arm] = normalized_semantics
                elif deterministic_semantics[expected_arm] != normalized_semantics:
                    raise EvidenceError(
                        f"{label}.{arm_name} semantics are not deterministic across repetitions"
                    )
            process = arm["process_instance_identity"]
            if process != 0:
                state = (
                    process,
                    arm["process_lifetime_peak_host_bytes"],
                    arm["raw_wait_status"],
                    arm["process_exit_code"],
                    arm["terminating_signal"],
                )
                if process_states[expected_arm] is None:
                    process_states[expected_arm] = state
                elif process_states[expected_arm] != state:
                    raise EvidenceError(
                        "one contender must retain one process, wait4 peak, and exit state"
                    )
                process_attempts[expected_arm].append(arm)
            elif arm["disposition"] not in {2, 7, 10}:
                raise EvidenceError("dispatched or child failure is missing its process identity")
        ordered_arms.extend(
            (attempt["baseline"], attempt["candidate"])
            if expected_order == 0
            else (attempt["candidate"], attempt["baseline"])
        )
        candidate = attempt["candidate"]
        if (
            candidate["disposition"],
            candidate["controller_invariant_id"],
        ) in lost_candidate_invocation_witnesses:
            expected_candidate_invocations_before += 1
        if attempt["result"] is not None:
            comparison = attempt["result"]["comparison"]
            if deterministic_comparison is None:
                deterministic_comparison = comparison
            elif comparison != deterministic_comparison:
                raise EvidenceError(f"{label} pair comparison is not deterministic")
        if attempt["result"] is None:
            complete = False
    if process_states[0] is not None and process_states[1] is not None:
        if process_states[0][0] == process_states[1][0]:
            raise EvidenceError("baseline and candidate observations share a process")
    unavailable_invariants = {
        "P4HARNESS-REAP-BOUNDED-001",
        "P4HARNESS-WAIT4-AUTHORITY-001",
    }
    for expected_arm, state in enumerate(process_states):
        if state is None:
            continue
        authority_unavailable = any(
            arm["controller_invariant_id"] in unavailable_invariants
            for arm in process_attempts[expected_arm]
        )
        if state[1] == 0 and not authority_unavailable:
            raise EvidenceError("launched process must retain its wait4 lifetime peak")
        process_wide_failure = any(
            arm["controller_invariant_id"] in unavailable_invariants
            or (
                arm["disposition"] not in {0, 1, 8, 10}
                and (
                    arm["disposition"],
                    arm["controller_invariant_id"],
                )
                not in lost_candidate_invocation_witnesses
            )
            for arm in process_attempts[expected_arm]
        )
        if process_wide_failure and any(
            arm["disposition"] == 0 for arm in process_attempts[expected_arm]
        ):
            raise EvidenceError(
                "process-wide controller failure must invalidate every prior success"
            )
        if authority_unavailable:
            continue
        raw_status, exit_code, signal = state[2], state[3], state[4]
        if os.WIFEXITED(raw_status):
            matches_wait = exit_code == os.WEXITSTATUS(raw_status) and signal == 0
        elif os.WIFSIGNALED(raw_status):
            matches_wait = exit_code == -1 and signal == os.WTERMSIG(raw_status)
        else:
            matches_wait = False
        if not matches_wait:
            raise EvidenceError("serialized exit tuple does not match exact wait4 status")

    def pristine_not_run(arm: Mapping[str, Any]) -> bool:
        return (
            arm["disposition"] == 10
            and arm["dispatch_ordinal"] == 0
            and arm["process_instance_identity"] == 0
            and arm["outer_elapsed_nanoseconds"] == 0
            and arm["process_lifetime_peak_host_bytes"] == 0
            and arm["raw_wait_status"] == 0
            and arm["process_exit_code"] == -1
            and arm["terminating_signal"] == 0
            and not arm["watchdog_kill_sent"]
            and not arm["controller_invariant_id"]
            and not arm["controller_detail"]
            and arm["record"] is None
            and arm["child_failure"] is None
        )

    positive_count = sum(arm["dispatch_ordinal"] != 0 for arm in ordered_arms)
    if positive_count == 0:
        first_failures = [
            arm
            for arm in (attempts[0]["baseline"], attempts[0]["candidate"])
            if arm["disposition"] != 10
        ]
        if not first_failures:
            raise EvidenceError("zero-dispatch cell has no authenticated setup failure")
        if any(arm["disposition"] not in {1, 2, 4, 5, 6, 7} for arm in first_failures):
            raise EvidenceError("zero-dispatch failure has a measured-only disposition")
        if len(first_failures) > 1 and not all(
            arm["disposition"] == 6 and arm["controller_invariant_id"] == "P4HARNESS-READY-PAIR-001"
            for arm in first_failures
        ):
            raise EvidenceError("setup short-circuit contains multiple independent failures")
        if any(
            not pristine_not_run(arm)
            for arm in (attempts[0]["baseline"], attempts[0]["candidate"])
            if arm["disposition"] == 10
        ):
            raise EvidenceError("unreleased setup peer is not a pristine not-run attempt")
        if any(
            not pristine_not_run(arm)
            for repetition in attempts[1:]
            for arm in (repetition["baseline"], repetition["candidate"])
        ):
            raise EvidenceError("setup failure must leave every later attempt not run")
    else:
        for index, arm in enumerate(ordered_arms):
            if index < positive_count:
                if arm["dispatch_ordinal"] != index + 1 or arm["disposition"] == 10:
                    raise EvidenceError("dispatch ordinals must form the prescribed serial prefix")
            elif not pristine_not_run(arm):
                raise EvidenceError("no measured arm may be released after the fatal prefix")
        if any(arm["disposition"] == 1 for arm in ordered_arms[: max(positive_count - 1, 0)]):
            raise EvidenceError("typed child failure must terminate the dispatch prefix")
    return complete


def validate_document(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
    _expected_raw_evidence_schema_version: int | None = None,
    _total_attempt_mode: bool = False,
    _expected_corpus_version: int = 1,
) -> None:
    """Validate one complete Raw-v1 cell; testing relaxations must be explicit."""
    if (
        isinstance(expected_repetitions, bool)
        or not 1 <= expected_repetitions <= _CANONICAL_REPETITIONS
    ):
        raise EvidenceError("expected repetitions must be between 1 and 20")
    if isinstance(expected_workers, bool) or not 1 <= expected_workers <= 64:
        raise EvidenceError("expected workers must be between 1 and 64")
    if _expected_corpus_version not in {1, 2}:
        raise EvidenceError("expected corpus version must be 1 or 2")
    document = _object(value, "raw cell")
    if _expected_raw_evidence_schema_version is None:
        _fields(document, _TOP_FIELDS, "raw cell")
        expected_wire_schema_version = 1
    else:
        _fields(document, _SAME_RUN_TOP_FIELDS, "same-run raw cell")
        if (
            _u32(document["raw_evidence_schema_version"], "raw_evidence_schema_version")
            != _expected_raw_evidence_schema_version
        ):
            raise EvidenceError(
                "raw_evidence_schema_version does not name the expected authority contract"
            )
        expected_wire_schema_version = _SAME_RUN_WIRE_SCHEMA_VERSION
    if _u32(document["wire_schema_version"], "wire_schema_version") != expected_wire_schema_version:
        raise EvidenceError(f"wire_schema_version must be {expected_wire_schema_version}")
    if _u32(document["schema_version"], "schema_version") != 1:
        raise EvidenceError("schema_version must be 1")
    source_commit = _string(document["source_commit"], "source_commit")
    source_stamped = _bool(document["source_stamped"], "source_stamped")
    source_dirty = _bool(document["source_tree_dirty"], "source_tree_dirty")
    source_envelope_checksum = _u64(
        document["source_envelope_checksum"], "source_envelope_checksum"
    )
    if not allow_unstamped:
        if expected_commit is None or _COMMIT.fullmatch(expected_commit) is None:
            raise EvidenceError("publication requires an independently supplied expected commit")
        if _COMMIT.fullmatch(source_commit) is None or not source_stamped or source_dirty:
            raise EvidenceError(
                "publication requires a clean, stamped 40-character lowercase commit"
            )
        if source_commit != expected_commit:
            raise EvidenceError("source_commit does not match the independently supplied commit")
    config = _config(document["config"], expected_repetitions, expected_workers)
    _environment(document["environment"])
    for field in (
        "corpus_checksum",
        "cell_plan_checksum",
        "authority_run_identity",
        "controller_identity",
        "artifact_checksum",
    ):
        if _u64(document[field], field) == 0:
            raise EvidenceError(f"{field} must be nonzero")
    manifest_checksum, manifest_cases, manifest_budgets = _representative_manifest_for_corpus(
        _expected_corpus_version
    )
    if document["corpus_checksum"] != manifest_checksum:
        raise EvidenceError("corpus_checksum does not match the frozen representative manifest")
    manifest_case = manifest_cases.get(config["case_id"])
    if manifest_case is None:
        raise EvidenceError("config.case_id is absent from the frozen representative manifest")
    if manifest_case["build_status"] != "success":
        raise EvidenceError("config.case_id is a bounded stress witness, not a successful cell")
    if config["requested_pool_size"] not in manifest_case["requested_pool_sizes"]:
        raise EvidenceError("config.requested_pool_size is absent from the frozen case roster")
    required_resources = (
        ("maximum_nets", "workload_net_count"),
        ("maximum_compiled_nodes", "required_compiled_nodes"),
        ("maximum_compiled_host_bytes", "required_compiled_host_bytes"),
        ("maximum_active_regions", "required_active_regions"),
        ("maximum_board_entities", "required_board_entities"),
    )
    for configured_field, required_field in required_resources:
        if config["corpus_limits"][configured_field] < manifest_case[required_field]:
            raise EvidenceError(
                f"config.corpus_limits.{configured_field} cannot build the frozen case"
            )
    canonical_algorithm_budget_checksum = manifest_budgets.get(
        (config["case_id"], config["requested_pool_size"])
    )
    if canonical_algorithm_budget_checksum is None:
        raise EvidenceError("cell has no frozen canonical algorithm budget")
    expected_budget_checksum = compute_canonical_budget_checksum(
        document,
        manifest_case,
        canonical_algorithm_budget_checksum,
        corpus_version=_expected_corpus_version,
    )
    if document["cell_plan_checksum"] != compute_cell_plan_checksum(
        document, corpus_version=_expected_corpus_version
    ):
        raise EvidenceError("cell_plan_checksum does not authenticate the cell plan")
    attempts = _array(document["attempts"], "attempts")
    if len(attempts) != expected_repetitions:
        raise EvidenceError(f"attempts must contain exactly {expected_repetitions} pairs")
    if _total_attempt_mode:
        complete = _validate_total_attempts(
            document,
            attempts,
            config,
            manifest_case,
            expected_budget_checksum,
            expected_corpus_version=_expected_corpus_version,
        )
        _validate_root_checksums(document, source_envelope_checksum)
        if not complete:
            return

    baseline_process: int | None = None
    candidate_process: int | None = None
    baseline_peak: int | None = None
    candidate_peak: int | None = None
    root_seed: int | None = None
    previous_candidate_lifecycle: Mapping[str, Any] | None = None
    deterministic_semantics: list[Mapping[str, Any] | None] = [None, None]
    deterministic_comparison: int | None = None
    expected_dispatch = 1
    expected_root_seed = compute_canonical_root_seed(
        document, corpus_version=_expected_corpus_version
    )
    for repetition, raw_attempt in enumerate(attempts):
        label = f"attempts[{repetition}]"
        attempt = _pair_attempt(
            raw_attempt,
            label,
            expected_manifest_case=manifest_case,
            requested_pool_size=config["requested_pool_size"],
            expected_corpus_version=_expected_corpus_version,
        )
        expected_order = repetition % 2
        if (
            attempt["case_id"] != config["case_id"]
            or attempt["requested_pool_size"] != config["requested_pool_size"]
            or attempt["repetition_index"] != repetition
            or attempt["execution_order"] != expected_order
        ):
            raise EvidenceError(
                f"{label} is missing, extra, reordered, or associated with another cell"
            )
        if attempt["root_seed"] != expected_root_seed:
            raise EvidenceError(f"{label}.root_seed is not the canonical cell root")
        if root_seed is None:
            root_seed = attempt["root_seed"]
        elif root_seed != attempt["root_seed"]:
            raise EvidenceError("root_seed must remain stable across repetitions")

        baseline = attempt["baseline"]
        candidate = attempt["candidate"]
        expected_arm_order = (baseline, candidate) if expected_order == 0 else (candidate, baseline)
        if (
            expected_arm_order[0]["dispatch_ordinal"] != expected_dispatch
            or expected_arm_order[1]["dispatch_ordinal"] != expected_dispatch + 1
        ):
            raise EvidenceError(f"{label} dispatch ordinals do not prove prescribed serial order")
        expected_dispatch += 2
        comparison = attempt["result"]["comparison"]
        if deterministic_comparison is None:
            deterministic_comparison = comparison
        elif comparison != deterministic_comparison:
            raise EvidenceError(f"{label} pair comparison is not deterministic")
        for arm, expected_arm in ((baseline, 0), (candidate, 1)):
            semantics = arm["record"]["semantics"]
            observation = arm["record"]["external_observation"]
            net_count = semantics["workload_net_count"]
            pool_size = config["requested_pool_size"]
            expected_queries = net_count * (pool_size + 2)
            if (
                semantics["baseline_sweeps"] != pool_size + 2
                or semantics["candidate_regeneration_epochs"] != 2
                or semantics["candidate_columns_per_epoch"] != net_count
                or semantics["candidate_terminal_selection_rounds"] != pool_size + 1
                or semantics["opportunity"]["route_queries"] != expected_queries
                or semantics["opportunity"]["route_work_units"]
                != expected_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
            ):
                raise EvidenceError(f"{label} does not use the canonical equal-budget shape")
            if expected_arm == 1:
                maximum_preparation_queries = net_count * pool_size
                maximum_regeneration_queries = 2 * net_count
                if (
                    semantics["preparation_route_queries"] > maximum_preparation_queries
                    or semantics["preparation_route_work_units"]
                    > maximum_preparation_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
                    or semantics["regeneration_route_queries"] > maximum_regeneration_queries
                    or semantics["regeneration_route_work_units"]
                    > maximum_regeneration_queries * _CANONICAL_ROUTE_WORK_UNITS_PER_QUERY
                ):
                    raise EvidenceError(
                        f"{label} candidate component work exceeds its canonical cap"
                    )
            if (
                arm["arm"] != expected_arm
                or arm["repetition_index"] != repetition
                or arm["execution_order"] != expected_order
                or semantics["arm"] != expected_arm
                or semantics["repetition_index"] != repetition
                or semantics["execution_order"] != expected_order
                or semantics["case_id"] != config["case_id"]
                or semantics["requested_pool_size"] != config["requested_pool_size"]
                or semantics["preparation_worker_count"] != config["preparation_worker_count"]
                or semantics["root_seed"] != attempt["root_seed"]
                or semantics["corpus_checksum"] != document["corpus_checksum"]
                or semantics["workload_net_count"] != manifest_case["workload_net_count"]
                or semantics["descriptor_fingerprint"] != manifest_case["descriptor_fingerprint"]
                or semantics["case_checksum"] != manifest_case["case_checksum"]
                or semantics["board_content_hash"] != manifest_case["board_content_hash"]
                or semantics["workload_checksum"] != manifest_case["workload_checksum"]
                or semantics["capacity_model_checksum"] != manifest_case["capacity_model_checksum"]
                or semantics["budget_checksum"] != expected_budget_checksum
                or semantics["external_budget"] != config["external_budget"]
            ):
                raise EvidenceError(
                    f"{label} arm record is associated with another command or cell"
                )
            normalized_semantics = {
                field: field_value
                for field, field_value in semantics.items()
                if field not in {"execution_order", "repetition_index", "semantic_checksum"}
            }
            if deterministic_semantics[expected_arm] is None:
                deterministic_semantics[expected_arm] = normalized_semantics
            elif deterministic_semantics[expected_arm] != normalized_semantics:
                raise EvidenceError(
                    f"{label} arm semantics are not deterministic across repetitions"
                )
            if (
                observation["authority_run_identity"] != document["authority_run_identity"]
                or observation["controller_identity"] != document["controller_identity"]
                or observation["process_instance_identity"] != arm["process_instance_identity"]
                or observation["outer_elapsed_nanoseconds"] != arm["outer_elapsed_nanoseconds"]
                or observation["peak_host_bytes"] != arm["process_lifetime_peak_host_bytes"]
            ):
                raise EvidenceError(
                    f"{label} external observation is associated with another process"
                )

        if baseline_process is None:
            baseline_process = baseline["process_instance_identity"]
            candidate_process = candidate["process_instance_identity"]
            baseline_peak = baseline["process_lifetime_peak_host_bytes"]
            candidate_peak = candidate["process_lifetime_peak_host_bytes"]
        if (
            baseline["process_instance_identity"] != baseline_process
            or candidate["process_instance_identity"] != candidate_process
            or baseline["process_lifetime_peak_host_bytes"] != baseline_peak
            or candidate["process_lifetime_peak_host_bytes"] != candidate_peak
        ):
            raise EvidenceError(
                "each arm must share one long-lived process and wait4 peak across the cell"
            )
        if baseline_process == candidate_process:
            raise EvidenceError("baseline and candidate must use distinct isolated processes")

        baseline_lifecycle = baseline["record"]["preparer_lifecycle"]
        candidate_lifecycle = candidate["record"]["preparer_lifecycle"]
        if any(baseline_lifecycle[field] != 0 for field in _LIFECYCLE_FIELDS):
            raise EvidenceError(f"{label} baseline lifecycle must be zero")
        if (
            baseline["record"]["external_observation"]["persistent_preparer_reused"]
            or not candidate["record"]["external_observation"]["persistent_preparer_reused"]
        ):
            raise EvidenceError(f"{label} persistent-preparer association is invalid")
        workers = config["preparation_worker_count"]
        if (
            candidate_lifecycle["workers_started_before"] != workers
            or candidate_lifecycle["workers_started_after"] != workers
            or candidate_lifecycle["invocations_started_before"]
            != candidate_lifecycle["invocations_completed_before"]
            or candidate_lifecycle["invocations_started_after"]
            != candidate_lifecycle["invocations_started_before"] + 1
            or candidate_lifecycle["invocations_completed_after"]
            != candidate_lifecycle["invocations_completed_before"] + 1
            or candidate_lifecycle["invocations_completed_before"] == 0
        ):
            raise EvidenceError(f"{label} candidate lifecycle does not prove persistent reuse")
        if repetition == 0 and (
            candidate_lifecycle["invocations_started_before"] != 1
            or candidate_lifecycle["invocations_completed_before"] != 1
        ):
            raise EvidenceError(
                f"{label} candidate lifecycle does not begin after exactly one warm-up"
            )
        if previous_candidate_lifecycle is not None and (
            candidate_lifecycle["invocations_started_before"]
            != previous_candidate_lifecycle["invocations_started_after"]
            or candidate_lifecycle["invocations_completed_before"]
            != previous_candidate_lifecycle["invocations_completed_after"]
        ):
            raise EvidenceError(f"{label} candidate lifecycle is not continuous")
        previous_candidate_lifecycle = candidate_lifecycle

    if previous_candidate_lifecycle is None or (
        previous_candidate_lifecycle["invocations_started_after"] != expected_repetitions + 1
        or previous_candidate_lifecycle["invocations_completed_after"] != expected_repetitions + 1
    ):
        raise EvidenceError(
            "candidate lifecycle does not end after one warm-up and all measured repetitions"
        )

    _validate_root_checksums(document, source_envelope_checksum)


def validate_same_run_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
) -> None:
    """Validate Raw-v2 output from the telemetry-aware Wire-v2 controller."""
    validate_document(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=_SAME_RUN_RAW_EVIDENCE_SCHEMA_VERSION,
    )


def validate_same_run_total_attempt_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
) -> None:
    """Authenticate complete or incomplete Raw-v2 total-attempt evidence."""
    validate_document(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=_SAME_RUN_RAW_EVIDENCE_SCHEMA_VERSION,
        _total_attempt_mode=True,
    )


def validate_confirmatory_document(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
) -> None:
    """Validate ordinary Raw-v1 wire output against frozen Corpus V2 authority."""
    validate_document(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_corpus_version=2,
    )
    _validate_confirmatory_protocol_scope(value, same_run=False)


def validate_confirmatory_same_run_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
) -> None:
    """Validate same-run Raw-v2 wire output against frozen Corpus V2 authority."""
    validate_document(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=_SAME_RUN_RAW_EVIDENCE_SCHEMA_VERSION,
        _expected_corpus_version=2,
    )
    _validate_confirmatory_protocol_scope(value, same_run=True)


def validate_confirmatory_same_run_total_attempt_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = _CANONICAL_REPETITIONS,
    expected_workers: int = _CANONICAL_WORKERS,
) -> bool:
    """Authenticate complete or incomplete confirmatory same-run Raw-v2 evidence."""
    validate_document(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=_SAME_RUN_RAW_EVIDENCE_SCHEMA_VERSION,
        _total_attempt_mode=True,
        _expected_corpus_version=2,
    )
    _validate_confirmatory_protocol_scope(value, same_run=True)
    document = _object(value, "confirmatory same-run total-attempt cell")
    return all(
        _object(attempt, "confirmatory same-run total-attempt pair")["result"] is not None
        for attempt in _array(
            document["attempts"], "confirmatory same-run total-attempt cell.attempts"
        )
    )


def _validate_confirmatory_protocol_scope(value: Any, *, same_run: bool) -> None:
    from tools import validate_phase4_confirmatory_decision_protocol as protocol_validator

    try:
        protocol_validator.read_protocol()
        permitted_roles = (
            {"exact", "heldout", "imported"}
            if same_run
            else {"calibration", "fixed_query", "stress"}
        )
        cells = {
            (case_id, pool)
            for case_id, pool, role, disposition in protocol_validator.expanded_cells()
            if disposition == "confirmatory_raw_success" and role in permitted_roles
        }
    except ValueError as error:
        raise EvidenceError(
            f"cannot authenticate the frozen confirmatory protocol: {error}"
        ) from error
    document = _object(value, "confirmatory raw cell")
    config = _object(document["config"], "confirmatory raw cell.config")
    cell = (config["case_id"], config["requested_pool_size"])
    if cell not in cells:
        authority = "same-run" if same_run else "ordinary"
        raise EvidenceError(
            f"confirmatory cell is outside the protocol-assigned {authority} Raw authority"
        )


def _reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def _check_canonical_key_order(value: Any, label: str = "raw cell") -> None:
    pending = [(value, label, 0)]
    while pending:
        current, current_label, depth = pending.pop()
        if depth > _MAXIMUM_JSON_NESTING_DEPTH:
            raise EvidenceError(
                f"{current_label} exceeds the {_MAXIMUM_JSON_NESTING_DEPTH}-level nesting bound"
            )
        if isinstance(current, dict):
            keys = tuple(current)
            key_set = set(keys)
            known_orders = (
                _TOP_FIELDS,
                _SAME_RUN_TOP_FIELDS,
                _CONFIG_FIELDS,
                _BUDGET_FIELDS,
                _LIMIT_FIELDS,
                _ENVIRONMENT_FIELDS,
                _PAIR_ATTEMPT_FIELDS,
                _ATTEMPT_FIELDS,
                _RECORD_FIELDS,
                _SEMANTICS_FIELDS,
                _OPPORTUNITY_FIELDS,
                _OUTCOME_FIELDS,
                _LIFECYCLE_FIELDS,
                _OBSERVATION_FIELDS,
                _FAILURE_FIELDS,
                _RESULT_FIELDS,
            )
            for expected in known_orders:
                if key_set == set(expected):
                    if keys != tuple(expected):
                        raise EvidenceError(
                            f"{current_label} fields are not in canonical key order"
                        )
                    break
            pending.extend(
                (child, f"{current_label}.{key}", depth + 1)
                for key, child in reversed(tuple(current.items()))
            )
        elif isinstance(current, list):
            pending.extend(
                (child, f"{current_label}[{index}]", depth + 1)
                for index, child in reversed(tuple(enumerate(current)))
            )


def _reject_non_json_constant(value: str) -> Any:
    raise EvidenceError(f"non-JSON numeric constant: {value}")


def read_document(path: pathlib.Path) -> Any:
    try:
        encoded = read_regular_file(path, _MAXIMUM_RAW_JSON_BYTES, label="raw cell")
        if len(encoded) > _MAXIMUM_RAW_JSON_BYTES:
            raise EvidenceError(f"raw cell exceeds the {_MAXIMUM_RAW_JSON_BYTES}-byte input bound")
        raw = encoded.decode("utf-8")
        document = json.loads(
            raw,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_canonical_key_order(document)
        canonical = (
            json.dumps(
                document,
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
            + "\n"
        )
        if raw != canonical:
            raise EvidenceError("raw cell must be canonical one-line JSON followed by one LF")
        return document
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as error:
        if isinstance(error, EvidenceError):
            raise
        raise EvidenceError(f"cannot read raw cell {path}: {error}") from error


def read_validated_publication_document(
    path: pathlib.Path, *, expected_commit: str
) -> Mapping[str, Any]:
    """Read once and fully validate one canonical publication Raw v1 cell."""
    document = read_document(path)
    validate_document(document, expected_commit=expected_commit)
    return _object(document, "raw cell")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--testing-repetitions", type=int, default=_CANONICAL_REPETITIONS)
    parser.add_argument("--testing-workers", type=int, default=_CANONICAL_WORKERS)
    parser.add_argument("--expected-commit")
    parser.add_argument("--same-run-total-attempt-v2", action="store_true")
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        for path in options.paths:
            if options.same_run_total_attempt_v2:
                validate_same_run_total_attempt_document_v2(
                    read_document(path),
                    allow_unstamped=options.testing_allow_unstamped,
                    expected_commit=options.expected_commit,
                    expected_repetitions=options.testing_repetitions,
                    expected_workers=options.testing_workers,
                )
            elif (
                not options.testing_allow_unstamped
                and options.testing_repetitions == _CANONICAL_REPETITIONS
                and options.testing_workers == _CANONICAL_WORKERS
            ):
                read_validated_publication_document(path, expected_commit=options.expected_commit)
            else:
                validate_document(
                    read_document(path),
                    allow_unstamped=options.testing_allow_unstamped,
                    expected_commit=options.expected_commit,
                    expected_repetitions=options.testing_repetitions,
                    expected_workers=options.testing_workers,
                )
    except EvidenceError as error:
        print(f"Phase 4 raw evidence validation failed: {error}", file=sys.stderr)
        return 1
    print(f"validated {len(options.paths)} Phase 4 raw cell artifact(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
