"""Validates the committed Phase 3 benchmark artifact and its conclusions."""

from __future__ import annotations

import hashlib
import json
import math
import os
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

_MANIFEST_SCHEMA = "phase3_evidence_manifest_v1"
_RESULT_SCHEMAS = {"phase3_candidate_bakeoff_v1", "phase3_candidate_bakeoff_v2"}
_GENERATORS = (
    "sequential_cpu_astar",
    "parallel_cpu_astar",
    "batched_cuda_frontier",
    "batched_cuda_sweep",
)
_STAGES = ("execution_readback", "exact_admission_store", "end_to_end")
_CASES = (
    "cross_tile_edges",
    "dense_corridors",
    "disconnected_fields",
    "fragmented_runs",
    "high_turn_maze",
    "kicad_fixture",
    "multi_channel_bottleneck",
    "negative_coordinates",
    "policy_alternatives",
    "sparse_regions",
    "symmetric_dual_corridor",
)
_CANDIDATE_COUNTS = (4, 8, 16, 32, 64, 128)
_AGGREGATES = {"mean", "median", "stddev", "cv"}
_ROW_PATTERN = re.compile(r"^phase3/([^/]+)/([^/]+)/([^/]+)/k_([0-9]+)/.*_([a-z]+)$")
_UPLOAD_PATTERN = re.compile(r"^phase3/shared_cuda/prepared_upload/([^/]+)/.*_([a-z]+)$")
_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
_UNSIGNED_DECIMAL_PATTERN = re.compile(r"0|[1-9][0-9]*")
_UINT32_MAX = (1 << 32) - 1
_UINT64_MAX = (1 << 64) - 1
_COMMON_COUNTERS = {
    "batch_cuda_event_milliseconds",
    "candidate_queries_per_second",
    "blocking_status_readback_count",
    "chunk_rounds",
    "deterministic",
    "differential_match",
    "dispatched_rounds",
    "ordered_results",
    "examined_states",
    "examined_work_items",
    "failed_queries",
    "finalization_launch_count",
    "generated_routes_per_second",
    "kernel_launch_count",
    "rounds_total",
    "rounds_maximum",
    "persistent_owned_vram_bytes",
    "batch_owned_vram_bytes",
    "peak_owned_vram_bytes",
    "gpu_batch_owned_host_bytes",
    "process_lifetime_peak_rss_bytes",
    "process_peak_rss_available",
    "parallel_host_workers",
    "reached_queries",
    "unreachable_queries",
    "invalid_queries",
    "unsupported_queries",
    "resource_exhausted_queries",
    "cancelled_queries",
    "backend_failure_queries",
    "validation_failure_queries",
    "invariant_failure_queries",
    "base_policy_scalar_cost",
    "minimum_reported_policy_scalar_cost",
    "reachable_policy_scalar_cost_sum",
    "requested_candidate_count",
    "outcome_checksum_hi",
    "outcome_checksum_lo",
}
_V2_COMMON_COUNTERS = {
    "semantic_outcome_checksum_hi",
    "semantic_outcome_checksum_lo",
}
_ADMISSION_COUNTERS_V1 = {
    "accepted_candidate_yield",
    "accepted_candidates",
    "accepted_candidates_per_second",
    "rejected_candidates",
    "builder_rejections",
    "store_rejections",
    "generated_candidate_acceptance",
    "accepted_logical_bytes",
    "rejection_logical_bytes",
    "peak_deterministic_host_bytes",
    "retained_candidate_pool_size",
    "unique_geometry_signatures",
    "unique_resource_signatures",
    "mean_resource_jaccard",
    "minimum_resource_jaccard",
    "mean_geometric_overlap",
    "minimum_geometric_overlap",
    "resource_diversity",
    "geometric_diversity",
    "nondominated_candidates",
    "minimum_accepted_policy_scalar_cost",
    "base_candidate_intrinsic_base_cost",
    "best_of_k_intrinsic_base_cost",
    "candidate_order_checksum_hi",
    "candidate_order_checksum_lo",
}
_ADMISSION_COUNTERS_V2 = _ADMISSION_COUNTERS_V1 | {"retained_rejection_records"}
_ZERO_FAILURE_COUNTERS = (
    "invalid_queries",
    "unsupported_queries",
    "resource_exhausted_queries",
    "cancelled_queries",
    "backend_failure_queries",
    "validation_failure_queries",
    "invariant_failure_queries",
)
_FAILURE_COUNTERS = ("unreachable_queries",) + _ZERO_FAILURE_COUNTERS
_SCALAR_COST_COUNTERS = (
    "base_policy_scalar_cost",
    "minimum_reported_policy_scalar_cost",
    "reachable_policy_scalar_cost_sum",
)
_GPU_ONLY_COUNTERS = (
    "batch_cuda_event_milliseconds",
    "batch_owned_vram_bytes",
    "blocking_status_readback_count",
    "chunk_rounds",
    "dispatched_rounds",
    "finalization_launch_count",
    "gpu_batch_owned_host_bytes",
    "kernel_launch_count",
    "peak_owned_vram_bytes",
    "persistent_owned_vram_bytes",
    "rounds_maximum",
    "rounds_total",
)
_REQUIRED_CONTEXT = {
    "json_schema_version": 1,
    "library_version": "1.9.5",
    "apgar_corpus_version": "1",
    "apgar_board_ir_schema_version": "1",
    "apgar_compiled_board_schema_version": "1",
    "apgar_candidate_schema_version": "1.0",
    "apgar_candidate_geometry_schema_version": "1",
    "apgar_candidate_resource_schema_version": "1",
    "apgar_candidate_policy_schema_version": "1",
    "apgar_device_candidate_batch_schema_version": "1",
    "apgar_candidate_counts": "4,8,16,32,64,128",
    "apgar_repetitions": "20",
    "apgar_min_time_seconds": "0.020000",
    "apgar_min_warmup_seconds": "0.010000",
    "apgar_google_benchmark_version": "1.9.5",
}
_V2_REQUIRED_CONTEXT = {
    "apgar_source_identity": "bazel_stable_workspace_status_v1",
    "apgar_source_identity_trust": "canonical_checked_in_invocation_v1",
    "apgar_source_tree_dirty": "false",
    "apgar_store_maximum_admission_items": "1024",
    "apgar_store_maximum_admission_input_bytes": "67108864",
    "apgar_store_maximum_admission_work_units": "100000000",
}
_CORRECTNESS_SUMMARY_KEYS = {
    "generator_stage_medians",
    "requested_queries",
    "reached_queries",
    "unreachable_queries",
    "failure_queries",
}
_REQUIRED_METADATA = (
    "apgar_backend",
    "apgar_benchmark_cpp_toolchain",
    "apgar_cpu_model",
    "apgar_cuda_driver",
    "apgar_cuda_runtime",
    "apgar_cuda_toolkit_manifest",
    "apgar_cudart_component",
    "apgar_default_cpu_toolchain",
    "apgar_device_name",
    "apgar_device_uuid",
    "apgar_global_memory_bytes",
    "apgar_host_architecture",
    "apgar_host_kernel",
    "apgar_host_os",
    "apgar_nvcc_component",
    "apgar_nvidia_kmd_driver",
    "apgar_policy_schedule",
    "apgar_supported_device_class",
)


class EvidenceError(ValueError):
    """A stable validation failure suitable for a Bazel test diagnostic."""


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be a JSON object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be a JSON array")
    return value


def _safe_relative_path(root: pathlib.Path, value: Any, field: str) -> pathlib.Path:
    if not isinstance(value, str) or not value:
        raise EvidenceError(f"manifest {field} must be a nonempty string")
    relative = pathlib.PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise EvidenceError(f"manifest {field} must be a safe repository-relative path")
    path = root.joinpath(*relative.parts)
    if not path.is_file():
        raise EvidenceError(f"manifest {field} does not identify a file: {value}")
    return path


def _read_json(path: pathlib.Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read {label} {path}: {error}") from error


def _required_sha256(manifest: Mapping[str, Any], field: str) -> str:
    value = manifest.get(field)
    if not isinstance(value, str) or _SHA256_PATTERN.fullmatch(value) is None:
        raise EvidenceError(f"manifest {field} must be 64 lowercase hexadecimal characters")
    return value


def _validate_file_sha256(path: pathlib.Path, expected: str, label: str) -> None:
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise EvidenceError(f"{label} checksum mismatch: expected {expected}, got {actual}")


def _validate_nonnegative_finite_numbers(value: Any, label: str) -> None:
    if isinstance(value, bool):
        return
    if isinstance(value, (int, float)):
        try:
            number = float(value)
        except OverflowError as error:
            raise EvidenceError(f"{label} must be finite and nonnegative") from error
        if not math.isfinite(number) or number < 0.0:
            raise EvidenceError(f"{label} must be finite and nonnegative")
        return
    if isinstance(value, dict):
        for key, child in value.items():
            _validate_nonnegative_finite_numbers(child, f"{label}.{key}")
        return
    if isinstance(value, list):
        for index, child in enumerate(value):
            _validate_nonnegative_finite_numbers(child, f"{label}[{index}]")


def _require_number(row: Mapping[str, Any], key: str, label: str) -> float:
    value = row.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise EvidenceError(f"{label} counter {key} must be numeric")
    number = float(value)
    if not math.isfinite(number) or number < 0.0:
        raise EvidenceError(f"{label} counter {key} must be finite and nonnegative")
    return number


def _require_counter_integer(row: Mapping[str, Any], key: str, label: str) -> int:
    number = _require_number(row, key, label)
    if not number.is_integer():
        raise EvidenceError(f"{label} counter {key} must be an integer")
    return int(number)


def _require_uint32_counter(row: Mapping[str, Any], key: str, label: str) -> int:
    value = _require_counter_integer(row, key, label)
    if value > _UINT32_MAX:
        raise EvidenceError(f"{label} counter {key} exceeds uint32")
    return value


def _require_exact_context_value(context: Mapping[str, Any], key: str, expected: Any) -> None:
    value = context.get(key)
    if type(value) is not type(expected) or value != expected:  # noqa: E721
        raise EvidenceError(f"context {key} mismatch: expected {expected!r}, got {value!r}")


def _validate_policy_identities(value: Any, label: str) -> None:
    if not isinstance(value, str):
        raise EvidenceError(f"context {label} must be a comma-separated string")
    identities = value.split(",")
    if len(identities) != max(_CANDIDATE_COUNTS):
        raise EvidenceError(
            f"context {label} must contain {max(_CANDIDATE_COUNTS)} ordered identities"
        )
    parsed: list[int] = []
    for identity in identities:
        if _UNSIGNED_DECIMAL_PATTERN.fullmatch(identity) is None:
            raise EvidenceError(f"context {label} contains a non-canonical identity")
        parsed_identity = int(identity)
        if parsed_identity > _UINT64_MAX:
            raise EvidenceError(f"context {label} contains an identity exceeding uint64")
        parsed.append(parsed_identity)
    if len(set(parsed)) != len(parsed):
        raise EvidenceError(f"context {label} contains duplicate policy identities")


def _validate_context(context: Mapping[str, Any], commit: str, result_schema: str) -> None:
    for key, expected in _REQUIRED_CONTEXT.items():
        _require_exact_context_value(context, key, expected)
    if context.get("apgar_result_schema") != result_schema:
        raise EvidenceError("context apgar_result_schema does not match the evidence manifest")
    if result_schema == "phase3_candidate_bakeoff_v2":
        for key, expected in _V2_REQUIRED_CONTEXT.items():
            _require_exact_context_value(context, key, expected)
    if context.get("apgar_commit") != commit:
        raise EvidenceError("context apgar_commit does not match the evidence manifest")
    for key in _REQUIRED_METADATA:
        value = context.get(key)
        if not isinstance(value, str) or not value:
            raise EvidenceError(f"context {key} must be a nonempty string")
    for case in _CASES:
        for suffix in ("", "_policy_identities"):
            key = f"apgar_case_{case}{suffix}"
            value = context.get(key)
            if not isinstance(value, str) or not value:
                raise EvidenceError(f"context {key} must be a nonempty string")
        _validate_policy_identities(
            context.get(f"apgar_case_{case}_policy_identities"),
            f"apgar_case_{case}_policy_identities",
        )


def _require_close(actual: float, expected: float, label: str) -> None:
    if not math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-12):
        raise EvidenceError(f"{label} mismatch: expected {expected}, got {actual}")


def _validate_query_accounting(row: Mapping[str, Any], label: str, candidate_count: int) -> None:
    requested = _require_counter_integer(row, "requested_candidate_count", label)
    if requested != candidate_count:
        raise EvidenceError(f"{label} requested_candidate_count mismatch")
    reached = _require_counter_integer(row, "reached_queries", label)
    failed = _require_counter_integer(row, "failed_queries", label)
    if reached + failed != candidate_count:
        raise EvidenceError(f"{label} reached and failed counts do not cover the request")
    classified_failures = sum(
        _require_counter_integer(row, counter, label) for counter in _FAILURE_COUNTERS
    )
    if classified_failures != failed:
        raise EvidenceError(f"{label} failure-class counts do not equal failed_queries")
    for counter in _ZERO_FAILURE_COUNTERS:
        if _require_counter_integer(row, counter, label) != 0:
            raise EvidenceError(f"{label} unexpectedly reports {counter}")

    base_cost, minimum_cost, cost_sum = (
        _require_counter_integer(row, counter, label) for counter in _SCALAR_COST_COUNTERS
    )
    if reached == 0:
        if (base_cost, minimum_cost, cost_sum) != (0, 0, 0):
            raise EvidenceError(f"{label} reports scalar cost for an entirely failed batch")
    elif minimum_cost > cost_sum:
        raise EvidenceError(f"{label} minimum scalar cost exceeds the reachable cost sum")

    for prefix in ("outcome_checksum", "semantic_outcome_checksum"):
        high_key = f"{prefix}_hi"
        low_key = f"{prefix}_lo"
        if high_key in row or low_key in row:
            _require_uint32_counter(row, high_key, label)
            _require_uint32_counter(row, low_key, label)

    if _require_number(row, "process_peak_rss_available", label) != 1.0:
        raise EvidenceError(f"{label} does not contain available process peak RSS evidence")
    if _require_counter_integer(row, "process_lifetime_peak_rss_bytes", label) == 0:
        raise EvidenceError(f"{label} reports an empty process peak RSS measurement")


def _validate_execution_telemetry(
    row: Mapping[str, Any], label: str, generator: str, candidate_count: int, upload_bytes: int
) -> None:
    persistent = _require_counter_integer(row, "persistent_owned_vram_bytes", label)
    batch = _require_counter_integer(row, "batch_owned_vram_bytes", label)
    peak = _require_counter_integer(row, "peak_owned_vram_bytes", label)
    if peak != persistent + batch:
        raise EvidenceError(f"{label} peak VRAM does not equal persistent plus batch ownership")

    if generator in ("sequential_cpu_astar", "parallel_cpu_astar"):
        for counter in _GPU_ONLY_COUNTERS:
            if _require_number(row, counter, label) != 0.0:
                raise EvidenceError(f"{label} CPU row unexpectedly reports GPU counter {counter}")
        workers = _require_counter_integer(row, "parallel_host_workers", label)
        if generator == "sequential_cpu_astar" and workers != 1:
            raise EvidenceError(f"{label} sequential CPU row must use one host worker")
        if generator == "parallel_cpu_astar" and not 1 <= workers <= candidate_count:
            raise EvidenceError(f"{label} parallel CPU worker count is outside the batch bounds")
        return

    if persistent != upload_bytes or persistent == 0 or batch == 0:
        raise EvidenceError(f"{label} GPU memory ownership disagrees with prepared upload evidence")
    if _require_counter_integer(row, "gpu_batch_owned_host_bytes", label) == 0:
        raise EvidenceError(f"{label} GPU batch host accounting must be nonzero")
    if _require_counter_integer(row, "parallel_host_workers", label) != 1:
        raise EvidenceError(f"{label} GPU batch must report one host dispatch worker")

    expected_chunk = 32 if generator == "batched_cuda_frontier" else 8
    chunk = _require_counter_integer(row, "chunk_rounds", label)
    if chunk != expected_chunk:
        raise EvidenceError(f"{label} GPU round chunk does not match the generator contract")
    dispatched = _require_counter_integer(row, "dispatched_rounds", label)
    readbacks = _require_counter_integer(row, "blocking_status_readback_count", label)
    expected_readbacks = 0 if dispatched == 0 else (dispatched + chunk - 1) // chunk
    if readbacks != expected_readbacks:
        raise EvidenceError(f"{label} GPU blocking-readback accounting is inconsistent")
    finalization = _require_counter_integer(row, "finalization_launch_count", label)
    if finalization > 1 or (dispatched == 0 and finalization != 1):
        raise EvidenceError(f"{label} GPU finalization-launch accounting is inconsistent")
    launches = _require_counter_integer(row, "kernel_launch_count", label)
    if generator == "batched_cuda_frontier":
        expected_launches = 2 + readbacks + finalization
        if launches != expected_launches:
            raise EvidenceError(f"{label} CUDA frontier launch accounting is inconsistent")
    else:
        possible_launches = {
            2 + dispatched * kernels_per_round + finalization for kernels_per_round in (3, 4)
        }
        if launches not in possible_launches:
            raise EvidenceError(f"{label} CUDA sweep launch accounting is inconsistent")
    rounds_maximum = _require_counter_integer(row, "rounds_maximum", label)
    rounds_total = _require_counter_integer(row, "rounds_total", label)
    if rounds_maximum > dispatched or rounds_total > candidate_count * dispatched:
        raise EvidenceError(f"{label} query rounds exceed the dispatched batch rounds")


def _validate_admission_accounting(
    row: Mapping[str, Any], label: str, result_schema: str, candidate_count: int
) -> None:
    reached = _require_counter_integer(row, "reached_queries", label)
    accepted = _require_counter_integer(row, "accepted_candidates", label)
    rejected = _require_counter_integer(row, "rejected_candidates", label)
    builder_rejections = _require_counter_integer(row, "builder_rejections", label)
    store_rejections = _require_counter_integer(row, "store_rejections", label)
    retained = _require_counter_integer(row, "retained_candidate_pool_size", label)
    if accepted + rejected != reached:
        raise EvidenceError(
            f"{label} accepted and rejected candidates do not partition reached queries"
        )
    if builder_rejections + store_rejections != rejected:
        raise EvidenceError(f"{label} rejection sources do not partition rejected candidates")
    if retained != accepted:
        raise EvidenceError(f"{label} retained pool size does not equal accepted candidates")
    if result_schema == "phase3_candidate_bakeoff_v2":
        rejection_records = _require_counter_integer(row, "retained_rejection_records", label)
        if rejection_records != rejected:
            raise EvidenceError(f"{label} retained rejection accounting does not equal rejections")

    for counter in ("unique_geometry_signatures", "unique_resource_signatures"):
        if _require_counter_integer(row, counter, label) != accepted:
            raise EvidenceError(f"{label} {counter} does not match the deduplicated pool")
    if _require_counter_integer(row, "nondominated_candidates", label) > accepted:
        raise EvidenceError(f"{label} nondominated count exceeds accepted candidates")

    accepted_bytes = _require_counter_integer(row, "accepted_logical_bytes", label)
    rejection_bytes = _require_counter_integer(row, "rejection_logical_bytes", label)
    peak_bytes = _require_counter_integer(row, "peak_deterministic_host_bytes", label)
    if (accepted == 0) != (accepted_bytes == 0):
        raise EvidenceError(f"{label} accepted logical-byte accounting is inconsistent")
    if (rejected == 0) != (rejection_bytes == 0):
        raise EvidenceError(f"{label} rejection logical-byte accounting is inconsistent")
    if peak_bytes < accepted_bytes + rejection_bytes:
        raise EvidenceError(f"{label} peak deterministic host bytes omit retained payloads")

    accepted_yield = _require_number(row, "accepted_candidate_yield", label)
    generated_acceptance = _require_number(row, "generated_candidate_acceptance", label)
    _require_close(accepted_yield, accepted / candidate_count, f"{label} accepted_candidate_yield")
    expected_acceptance = 0.0 if reached == 0 else accepted / reached
    _require_close(
        generated_acceptance,
        expected_acceptance,
        f"{label} generated_candidate_acceptance",
    )

    for overlap in (
        "mean_resource_jaccard",
        "minimum_resource_jaccard",
        "mean_geometric_overlap",
        "minimum_geometric_overlap",
        "resource_diversity",
        "geometric_diversity",
    ):
        if _require_number(row, overlap, label) > 1.0:
            raise EvidenceError(f"{label} counter {overlap} is outside [0, 1]")
    expected_resource_diversity = (
        0.0 if accepted < 2 else 1.0 - _require_number(row, "mean_resource_jaccard", label)
    )
    expected_geometric_diversity = (
        0.0 if accepted < 2 else 1.0 - _require_number(row, "mean_geometric_overlap", label)
    )
    _require_close(
        _require_number(row, "resource_diversity", label),
        expected_resource_diversity,
        f"{label} resource_diversity",
    )
    _require_close(
        _require_number(row, "geometric_diversity", label),
        expected_geometric_diversity,
        f"{label} geometric_diversity",
    )
    for prefix in ("candidate_order_checksum",):
        _require_uint32_counter(row, f"{prefix}_hi", label)
        _require_uint32_counter(row, f"{prefix}_lo", label)


def _validate_rows(
    rows: Sequence[Any],
    result_schema: str,
) -> dict[tuple[str, str, str, int], Mapping[str, Any]]:
    aggregate_counts: dict[str, set[str]] = {}
    medians: dict[tuple[str, str, str, int], Mapping[str, Any]] = {}
    upload_medians: dict[str, Mapping[str, Any]] = {}
    for index, raw_row in enumerate(rows):
        row = _object(raw_row, f"benchmark row {index}")
        name = row.get("name")
        aggregate = row.get("aggregate_name")
        run_name = row.get("run_name")
        if not isinstance(name, str) or not isinstance(run_name, str):
            raise EvidenceError(f"benchmark row {index} lacks stable names")
        if aggregate not in _AGGREGATES:
            raise EvidenceError(f"benchmark row {name} has an unexpected aggregate")
        aggregate_counts.setdefault(run_name, set()).add(str(aggregate))

        match = _ROW_PATTERN.match(name)
        if match is not None:
            generator, stage, case, count_text, name_aggregate = match.groups()
            count = int(count_text)
            if generator not in _GENERATORS or stage not in _STAGES:
                raise EvidenceError(f"unexpected generator or stage in {name}")
            if case not in _CASES or count not in _CANDIDATE_COUNTS:
                raise EvidenceError(f"unexpected case or candidate count in {name}")
            if name_aggregate != aggregate:
                raise EvidenceError(f"aggregate suffix mismatch in {name}")
            label = "/".join((generator, stage, case, str(count), str(aggregate)))
            if row.get("time_unit") != "us":
                raise EvidenceError(f"{label} does not report microseconds")
            _require_number(row, "real_time", label)
            required_counters = set(_COMMON_COUNTERS)
            if result_schema == "phase3_candidate_bakeoff_v2":
                required_counters.update(_V2_COMMON_COUNTERS)
            if stage != "execution_readback":
                required_counters.update(
                    _ADMISSION_COUNTERS_V2
                    if result_schema == "phase3_candidate_bakeoff_v2"
                    else _ADMISSION_COUNTERS_V1
                )
            for counter in required_counters:
                _require_number(row, counter, label)
            if aggregate != "median":
                continue
            key = (generator, stage, case, count)
            if key in medians:
                raise EvidenceError(f"duplicate median row for {key}")
            medians[key] = row
            continue

        upload_match = _UPLOAD_PATTERN.match(name)
        if upload_match is None:
            raise EvidenceError(f"unexpected benchmark row name {name}")
        case, name_aggregate = upload_match.groups()
        if case not in _CASES or name_aggregate != aggregate:
            raise EvidenceError(f"invalid prepared-upload row {name}")
        label = f"prepared-upload/{case}/{aggregate}"
        if row.get("time_unit") != "us":
            raise EvidenceError(f"{label} does not report microseconds")
        for counter in (
            "real_time",
            "prepared_uploads_per_second",
            "persistent_owned_vram_bytes",
        ):
            _require_number(row, counter, label)
        if aggregate == "median":
            if case in upload_medians:
                raise EvidenceError(f"duplicate prepared-upload median for {case}")
            upload_medians[case] = row

    for run_name, aggregates in aggregate_counts.items():
        if aggregates != _AGGREGATES:
            raise EvidenceError(f"run {run_name} does not contain all four aggregates")
    expected_medians = len(_GENERATORS) * len(_STAGES) * len(_CASES) * len(_CANDIDATE_COUNTS)
    if len(medians) != expected_medians:
        raise EvidenceError(
            f"expected {expected_medians} generator/stage medians, found {len(medians)}"
        )
    if set(upload_medians) != set(_CASES):
        raise EvidenceError("prepared-upload medians do not cover the complete corpus")
    if len(rows) != (expected_medians + len(_CASES)) * len(_AGGREGATES):
        raise EvidenceError("benchmark aggregate row count does not match the required matrix")

    for key, row in medians.items():
        generator, stage, case, count = key
        label = "/".join((generator, stage, case, str(count)))
        for counter in ("deterministic", "differential_match", "ordered_results"):
            if _require_number(row, counter, label) != 1.0:
                raise EvidenceError(f"{label} does not report {counter}=1")
        _validate_query_accounting(row, label, count)
        upload_bytes = _require_counter_integer(
            upload_medians[case], "persistent_owned_vram_bytes", f"prepared-upload/{case}"
        )
        _validate_execution_telemetry(row, label, generator, count, upload_bytes)
        if stage != "execution_readback":
            _validate_admission_accounting(row, label, result_schema, count)

    for case, row in upload_medians.items():
        label = f"prepared-upload/{case}"
        if _require_counter_integer(row, "persistent_owned_vram_bytes", label) == 0:
            raise EvidenceError(f"{label} persistent VRAM accounting must be nonzero")
    return medians


def _correctness_summary(
    medians: Mapping[tuple[str, str, str, int], Mapping[str, Any]],
    result_schema: str,
) -> dict[str, int]:
    requested_queries = 0
    reached_queries = 0
    unreachable_queries = 0
    failure_queries = 0
    for case in _CASES:
        for count in _CANDIDATE_COUNTS:
            baseline_label = f"correctness/sequential_cpu_astar/end_to_end/{case}/{count}"
            baseline = medians[("sequential_cpu_astar", "end_to_end", case, count)]
            expected_reached = _require_counter_integer(baseline, "reached_queries", baseline_label)
            expected_unreachable = _require_counter_integer(
                baseline, "unreachable_queries", baseline_label
            )
            expected_failures = tuple(
                _require_counter_integer(baseline, counter, baseline_label)
                for counter in _FAILURE_COUNTERS
            )
            expected_scalar_costs = tuple(
                _require_counter_integer(baseline, counter, baseline_label)
                for counter in _SCALAR_COST_COUNTERS
            )
            expected_semantic_checksum = (
                (
                    _require_uint32_counter(
                        baseline, "semantic_outcome_checksum_hi", baseline_label
                    ),
                    _require_uint32_counter(
                        baseline, "semantic_outcome_checksum_lo", baseline_label
                    ),
                )
                if result_schema == "phase3_candidate_bakeoff_v2"
                else None
            )
            requested_queries += count
            reached_queries += expected_reached
            unreachable_queries += expected_unreachable
            failure_queries += sum(
                _require_counter_integer(baseline, counter, baseline_label)
                for counter in _ZERO_FAILURE_COUNTERS
            )

            for generator in _GENERATORS:
                for stage in _STAGES:
                    label = f"correctness/{generator}/{stage}/{case}/{count}"
                    row = medians[(generator, stage, case, count)]
                    reached = _require_counter_integer(row, "reached_queries", label)
                    unreachable = _require_counter_integer(row, "unreachable_queries", label)
                    failures = tuple(
                        _require_counter_integer(row, counter, label)
                        for counter in _FAILURE_COUNTERS
                    )
                    if (reached, unreachable, failures) != (
                        expected_reached,
                        expected_unreachable,
                        expected_failures,
                    ):
                        raise EvidenceError(
                            f"{label} failure semantics differ from the canonical CPU result"
                        )
                    scalar_costs = tuple(
                        _require_counter_integer(row, counter, label)
                        for counter in _SCALAR_COST_COUNTERS
                    )
                    if scalar_costs != expected_scalar_costs:
                        raise EvidenceError(
                            f"{label} scalar costs differ from the canonical CPU result"
                        )
                    if expected_semantic_checksum is not None:
                        semantic_checksum = (
                            _require_uint32_counter(row, "semantic_outcome_checksum_hi", label),
                            _require_uint32_counter(row, "semantic_outcome_checksum_lo", label),
                        )
                        if semantic_checksum != expected_semantic_checksum:
                            raise EvidenceError(
                                f"{label} ordered policy outcomes differ from the canonical CPU result"
                            )

    return {
        "generator_stage_medians": len(medians),
        "requested_queries": requested_queries,
        "reached_queries": reached_queries,
        "unreachable_queries": unreachable_queries,
        "failure_queries": failure_queries,
    }


def _winner_counts(
    medians: Mapping[tuple[str, str, str, int], Mapping[str, Any]],
) -> tuple[dict[str, int], dict[str, int]]:
    end_to_end = {
        "sequential_cpu_astar": 0,
        "parallel_cpu_astar": 0,
        "cuda": 0,
    }
    execution = {"batched_cuda_frontier": 0, "batched_cuda_sweep": 0}
    for case in _CASES:
        for count in _CANDIDATE_COUNTS:
            end_times = {
                generator: _require_number(
                    medians[(generator, "end_to_end", case, count)],
                    "real_time",
                    f"end_to_end/{generator}/{case}/{count}",
                )
                for generator in _GENERATORS
            }
            winner = min(_GENERATORS, key=lambda generator: end_times[generator])
            if winner.startswith("batched_cuda_"):
                end_to_end["cuda"] += 1
            else:
                end_to_end[winner] += 1

            execution_times = {
                generator: _require_number(
                    medians[(generator, "execution_readback", case, count)],
                    "real_time",
                    f"execution_readback/{generator}/{case}/{count}",
                )
                for generator in _GENERATORS
            }
            fastest_cpu = min(
                execution_times["sequential_cpu_astar"],
                execution_times["parallel_cpu_astar"],
            )
            for generator in execution:
                if execution_times[generator] < fastest_cpu:
                    execution[generator] += 1
    return end_to_end, execution


def _require_mapping_of_ints(value: Any, label: str) -> dict[str, int]:
    mapping = _object(value, label)
    result: dict[str, int] = {}
    for key, raw_count in mapping.items():
        if isinstance(raw_count, bool) or not isinstance(raw_count, int) or raw_count < 0:
            raise EvidenceError(f"{label}.{key} must be a nonnegative integer")
        result[str(key)] = raw_count
    return result


def _require_correctness_summary(value: Any) -> dict[str, int]:
    summary = _require_mapping_of_ints(value, "expected_correctness_summary")
    if set(summary) != _CORRECTNESS_SUMMARY_KEYS:
        raise EvidenceError(
            "expected_correctness_summary must contain exactly "
            + ", ".join(sorted(_CORRECTNESS_SUMMARY_KEYS))
        )
    return summary


def validate(root: pathlib.Path, manifest_path: pathlib.Path) -> None:
    manifest = _object(_read_json(manifest_path, "manifest"), "manifest")
    if manifest.get("schema") != _MANIFEST_SCHEMA:
        raise EvidenceError("unsupported Phase 3 evidence manifest schema")
    result_schema = manifest.get("result_schema")
    if result_schema not in _RESULT_SCHEMAS:
        raise EvidenceError("unsupported Phase 3 benchmark result schema")
    commit = manifest.get("source_commit")
    if not isinstance(commit, str) or _COMMIT_PATTERN.fullmatch(commit) is None:
        raise EvidenceError("manifest source_commit must be 40 lowercase hexadecimal characters")
    result_checksum = _required_sha256(manifest, "result_sha256")
    report_checksum = _required_sha256(manifest, "report_sha256")
    decision_checksum = _required_sha256(manifest, "decision_sha256")

    result_path = _safe_relative_path(root, manifest.get("result_file"), "result_file")
    report_path = _safe_relative_path(root, manifest.get("report_file"), "report_file")
    decision_path = _safe_relative_path(root, manifest.get("decision_file"), "decision_file")
    _validate_file_sha256(result_path, result_checksum, "result")
    _validate_file_sha256(report_path, report_checksum, "report")
    _validate_file_sha256(decision_path, decision_checksum, "ADR")

    result = _object(_read_json(result_path, "benchmark result"), "benchmark result")
    _validate_nonnegative_finite_numbers(result, "benchmark result")
    _validate_context(_object(result.get("context"), "benchmark context"), commit, result_schema)
    medians = _validate_rows(_array(result.get("benchmarks"), "benchmark rows"), result_schema)
    correctness = _correctness_summary(medians, result_schema)
    expected_correctness = _require_correctness_summary(
        manifest.get("expected_correctness_summary")
    )
    if correctness != expected_correctness:
        raise EvidenceError(
            f"correctness summary mismatch: expected {expected_correctness}, got {correctness}"
        )
    end_to_end, execution = _winner_counts(medians)
    expected_end_to_end = _require_mapping_of_ints(
        manifest.get("expected_end_to_end_wins"), "expected_end_to_end_wins"
    )
    expected_execution = _require_mapping_of_ints(
        manifest.get("expected_execution_wins_against_fastest_cpu"),
        "expected_execution_wins_against_fastest_cpu",
    )
    if end_to_end != expected_end_to_end:
        raise EvidenceError(
            f"end-to-end winner summary mismatch: expected {expected_end_to_end}, got {end_to_end}"
        )
    if execution != expected_execution:
        raise EvidenceError(
            f"execution winner summary mismatch: expected {expected_execution}, got {execution}"
        )

    result_relative = str(manifest.get("result_file"))
    report = report_path.read_text(encoding="utf-8")
    decision = decision_path.read_text(encoding="utf-8")
    common_tokens = (result_relative, result_checksum, commit)
    for token in common_tokens:
        if token not in report or token not in decision:
            raise EvidenceError(f"report and ADR must both contain bound evidence token {token}")
    report_total = (
        f"| **Total** | **{end_to_end['sequential_cpu_astar']}** | "
        f"**{end_to_end['parallel_cpu_astar']}** | **{end_to_end['cuda']}** |"
    )
    if report_total not in report:
        raise EvidenceError("report does not contain the recomputed end-to-end total row")
    decision_total = (
        f"won {end_to_end['sequential_cpu_astar']} and parallel CPU A* won "
        f"{end_to_end['parallel_cpu_astar']}. CUDA won none."
    )
    if decision_total not in decision:
        raise EvidenceError("ADR does not contain the recomputed CPU dispatch conclusion")
    sweep_wins = execution["batched_cuda_sweep"]
    count_words = {0: "zero", 1: "one", 2: "two", 3: "three", 4: "four"}
    rendered_sweep_wins = count_words.get(sweep_wins, str(sweep_wins))
    if f"sweep won only {rendered_sweep_wins}" not in decision:
        raise EvidenceError("ADR does not contain the recomputed CUDA execution conclusion")

    median_count = correctness["generator_stage_medians"]
    requested_queries = correctness["requested_queries"]
    reached_queries = correctness["reached_queries"]
    unreachable_queries = correctness["unreachable_queries"]
    report_median_summary = f"All {median_count} generator/stage medians"
    decision_median_summary = f"All {median_count} non-upload median generator/stage rows"
    if report_median_summary not in report or decision_median_summary not in decision:
        raise EvidenceError("report and ADR do not contain the recomputed median-row summary")
    if f"{requested_queries:,} requested queries" not in report:
        raise EvidenceError("report does not contain the recomputed requested-query summary")
    if f"unique {requested_queries:,}-query matrix" not in decision:
        raise EvidenceError("ADR does not contain the recomputed requested-query summary")
    if f"{reached_queries:,} reachable" not in report:
        raise EvidenceError("report does not contain the recomputed reachable-query summary")
    if f"{unreachable_queries:,} intentionally unreachable" not in report:
        raise EvidenceError("report does not contain the recomputed unreachable-query summary")
    if f"{unreachable_queries:,} unreachable policy" not in decision:
        raise EvidenceError("ADR does not contain the recomputed unreachable-query summary")
    if correctness["failure_queries"] == 0:
        if (
            "No row reports backend failure" not in report
            or "There were no backend" not in decision
        ):
            raise EvidenceError("report and ADR do not contain the recomputed zero-failure summary")


def _default_paths() -> tuple[pathlib.Path, pathlib.Path]:
    test_srcdir = os.environ.get("TEST_SRCDIR")
    test_workspace = os.environ.get("TEST_WORKSPACE")
    if not test_srcdir or not test_workspace:
        raise EvidenceError("provide REPOSITORY_ROOT MANIFEST outside Bazel test runfiles")
    root = pathlib.Path(test_srcdir) / test_workspace
    return root, root / "benchmarks/results/phase3_candidate_bakeoff_manifest_v1.json"


def main(argv: list[str]) -> int:
    try:
        if len(argv) == 1:
            root, manifest = _default_paths()
        elif len(argv) == 3:
            root = pathlib.Path(argv[1]).resolve()
            manifest = pathlib.Path(argv[2]).resolve()
        else:
            raise EvidenceError("usage: validate_phase3_evidence.py [REPOSITORY_ROOT MANIFEST]")
        validate(root, manifest)
    except (EvidenceError, OSError) as error:
        print(f"Phase 3 evidence validation failed: {error}", file=sys.stderr)
        return 1
    print("Phase 3 evidence validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
