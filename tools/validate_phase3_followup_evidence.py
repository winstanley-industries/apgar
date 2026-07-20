"""Validate canonical Phase 3 persistent/compact CUDA follow-up evidence."""

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

MANIFEST_SCHEMA = "phase3_followup_evidence_manifest_v1"
RESULT_SCHEMA = "phase3_candidate_bakeoff_v3"
GENERATORS = ("sequential_cpu_astar", "parallel_cpu_astar", "batched_cuda_sweep")
CPU_GENERATORS = GENERATORS[:2]
CUDA_GENERATOR = GENERATORS[2]
STAGES = ("execution_readback", "prepared_end_to_end", "end_to_end")
CASES = (
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
CANDIDATE_COUNTS = (4, 8, 16, 32, 64, 128, 256, 512)
AGGREGATES = ("mean", "median", "stddev", "cv")
REPETITIONS = 20
MEAN_DIFFERENCE_Z = 1.96
SWEEP_CHUNK_ROUNDS = 8
SWEEP_FIXED_LAUNCHES = 4
SWEEP_KERNELS_PER_ROUND = (3, 4)
MAXIMUM_FINALIZATION_LAUNCHES = 1
MAXIMUM_VALIDATION_WORKERS = 8
VALIDATION_QUERIES_PER_WORKER = 64
RESULT_HEADER_BYTES = 120
COMPACT_PATH_HEADER_BYTES = 32
COMPACT_PATH_STATE_BYTES = 4
UINT32_MAX = (1 << 32) - 1
UINT64_MAX = (1 << 64) - 1
BENCHMARK_SUFFIX = r"min_time:0\.020/min_warmup_time:0\.010/repeats:20/real_time"
ROW_PATTERN = re.compile(rf"^phase3/([^/]+)/([^/]+)/([^/]+)/k_([0-9]+)/{BENCHMARK_SUFFIX}$")
UPLOAD_PATTERN = re.compile(rf"^phase3/shared_cuda/prepared_upload/([^/]+)/{BENCHMARK_SUFFIX}$")
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
UNSIGNED_PATTERN = re.compile(r"0|[1-9][0-9]*")

COMMON_COUNTERS = {
    "backend_failure_queries",
    "base_policy_scalar_cost",
    "batch_cuda_event_milliseconds",
    "batch_owned_vram_bytes",
    "blocking_status_readback_count",
    "cancelled_queries",
    "candidate_queries_per_second",
    "chunk_rounds",
    "deterministic",
    "device_to_host_readback_bytes",
    "differential_match",
    "dispatched_rounds",
    "examined_states",
    "examined_work_items",
    "failed_queries",
    "finalization_launch_count",
    "generated_routes_per_second",
    "gpu_batch_owned_host_bytes",
    "invalid_queries",
    "invariant_failure_queries",
    "kernel_launch_count",
    "minimum_reported_policy_scalar_cost",
    "ordered_results",
    "outcome_checksum_hi",
    "outcome_checksum_lo",
    "parallel_host_workers",
    "peak_owned_vram_bytes",
    "persistent_owned_vram_bytes",
    "prepared_node_lookup_host_bytes",
    "process_lifetime_peak_rss_bytes",
    "process_peak_rss_available",
    "reached_queries",
    "reachable_policy_scalar_cost_sum",
    "requested_candidate_count",
    "resource_exhausted_queries",
    "rounds_maximum",
    "rounds_total",
    "semantic_outcome_checksum_hi",
    "semantic_outcome_checksum_lo",
    "unsupported_queries",
    "unreachable_queries",
    "validation_failure_queries",
    "workspace_capacity_vram_bytes",
}
ADMISSION_COUNTERS = {
    "accepted_candidate_yield",
    "accepted_candidates",
    "accepted_candidates_per_second",
    "accepted_logical_bytes",
    "base_candidate_intrinsic_base_cost",
    "best_of_k_intrinsic_base_cost",
    "builder_rejections",
    "candidate_order_checksum_hi",
    "candidate_order_checksum_lo",
    "generated_candidate_acceptance",
    "geometric_diversity",
    "mean_geometric_overlap",
    "mean_resource_jaccard",
    "minimum_accepted_policy_scalar_cost",
    "minimum_geometric_overlap",
    "minimum_resource_jaccard",
    "nondominated_candidates",
    "peak_deterministic_host_bytes",
    "rejected_candidates",
    "rejection_logical_bytes",
    "resource_diversity",
    "retained_candidate_pool_size",
    "retained_rejection_records",
    "store_rejections",
    "unique_geometry_signatures",
    "unique_resource_signatures",
}
ADMISSION_SEMANTIC_COUNTERS = ADMISSION_COUNTERS - {"accepted_candidates_per_second"}
ZERO_FAILURE_COUNTERS = (
    "invalid_queries",
    "unsupported_queries",
    "resource_exhausted_queries",
    "cancelled_queries",
    "backend_failure_queries",
    "validation_failure_queries",
    "invariant_failure_queries",
)
FAILURE_COUNTERS = ("unreachable_queries",) + ZERO_FAILURE_COUNTERS
SEMANTIC_COUNTERS = (
    "reached_queries",
    "failed_queries",
    *FAILURE_COUNTERS,
    "base_policy_scalar_cost",
    "minimum_reported_policy_scalar_cost",
    "reachable_policy_scalar_cost_sum",
    "semantic_outcome_checksum_hi",
    "semantic_outcome_checksum_lo",
)
GPU_COUNTERS = (
    "batch_cuda_event_milliseconds",
    "batch_owned_vram_bytes",
    "blocking_status_readback_count",
    "chunk_rounds",
    "device_to_host_readback_bytes",
    "dispatched_rounds",
    "finalization_launch_count",
    "gpu_batch_owned_host_bytes",
    "kernel_launch_count",
    "peak_owned_vram_bytes",
    "persistent_owned_vram_bytes",
    "prepared_node_lookup_host_bytes",
    "workspace_capacity_vram_bytes",
)

REQUIRED_CONTEXT = {
    "json_schema_version": 1,
    "library_version": "1.9.5",
    "apgar_result_schema": RESULT_SCHEMA,
    "apgar_source_identity": "bazel_stable_workspace_status_v1",
    "apgar_source_identity_trust": "canonical_checked_in_invocation_v1",
    "apgar_source_tree_dirty": "false",
    "apgar_corpus_version": "1",
    "apgar_board_ir_schema_version": "1",
    "apgar_compiled_board_schema_version": "1",
    "apgar_candidate_schema_version": "1.0",
    "apgar_candidate_geometry_schema_version": "1",
    "apgar_candidate_resource_schema_version": "1",
    "apgar_candidate_policy_schema_version": "1",
    "apgar_device_candidate_batch_schema_version": "1",
    "apgar_device_candidate_compact_path_schema_version": "1",
    "apgar_candidate_counts": ",".join(str(count) for count in CANDIDATE_COUNTS),
    "apgar_store_maximum_rejection_items": str(max(CANDIDATE_COUNTS)),
    "apgar_store_maximum_admission_items": "1024",
    "apgar_store_maximum_admission_input_bytes": str(64 * 1024 * 1024),
    "apgar_store_maximum_admission_work_units": "100000000",
    "apgar_cuda_sweep_chunk_rounds": str(SWEEP_CHUNK_ROUNDS),
    "apgar_cuda_sweep_kernels_per_round_without_runs": "3",
    "apgar_cuda_sweep_kernels_per_round_with_runs": "4",
    "apgar_cuda_sweep_fixed_batch_launches": str(SWEEP_FIXED_LAUNCHES),
    "apgar_cuda_maximum_finalization_launches": str(MAXIMUM_FINALIZATION_LAUNCHES),
    "apgar_cuda_sweep_workspace_model": (
        "prepared_view_cached_bounded_capacity_with_exclusive_execution_lease"
    ),
    "apgar_cuda_sweep_readback_model": (
        "result_headers_plus_compact_path_headers_plus_used_packed_path_states"
    ),
    "apgar_repetitions": str(REPETITIONS),
    "apgar_min_time_seconds": "0.020000",
    "apgar_min_warmup_seconds": "0.010000",
    "apgar_google_benchmark_version": "1.9.5",
}
REQUIRED_METADATA = (
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
    """Stable validation failure suitable for a Bazel test diagnostic."""


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be a JSON object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be a JSON array")
    return value


def _number(value: Any, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise EvidenceError(f"{label} must be numeric")
    number = float(value)
    if not math.isfinite(number) or number < 0.0:
        raise EvidenceError(f"{label} must be finite and nonnegative")
    return number


def _counter(row: Mapping[str, Any], key: str, label: str) -> float:
    return _number(row.get(key), f"{label} counter {key}")


def _integer(row: Mapping[str, Any], key: str, label: str) -> int:
    number = _counter(row, key, label)
    if not number.is_integer():
        raise EvidenceError(f"{label} counter {key} must be an integer")
    return int(number)


def _uint32(row: Mapping[str, Any], key: str, label: str) -> int:
    value = _integer(row, key, label)
    if value > UINT32_MAX:
        raise EvidenceError(f"{label} counter {key} exceeds uint32")
    return value


def _close(actual: float, expected: float, label: str) -> None:
    if not math.isclose(actual, expected, rel_tol=1e-9, abs_tol=1e-12):
        raise EvidenceError(f"{label} mismatch: expected {expected}, got {actual}")


def _validate_numbers(value: Any, label: str) -> None:
    if isinstance(value, bool):
        return
    if isinstance(value, (int, float)):
        _number(value, label)
    elif isinstance(value, dict):
        for key, child in value.items():
            _validate_numbers(child, f"{label}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _validate_numbers(child, f"{label}[{index}]")


def _case_field(context: Mapping[str, Any], case: str, field: str) -> int:
    value = context.get(f"apgar_case_{case}")
    if not isinstance(value, str):
        raise EvidenceError(f"context apgar_case_{case} must be a nonempty string")
    match = re.search(rf"(?:^|;){re.escape(field)}=([0-9]+)(?:;|$)", value)
    if match is None:
        raise EvidenceError(f"context apgar_case_{case} lacks {field}")
    return int(match.group(1))


def _validate_context(context: Mapping[str, Any], commit: str) -> None:
    for key, expected in REQUIRED_CONTEXT.items():
        value = context.get(key)
        if type(value) is not type(expected) or value != expected:  # noqa: E721
            raise EvidenceError(f"context {key} mismatch: expected {expected!r}, got {value!r}")
    if context.get("apgar_commit") != commit:
        raise EvidenceError("context apgar_commit does not match the evidence manifest")
    for key in REQUIRED_METADATA:
        value = context.get(key)
        if not isinstance(value, str) or not value:
            raise EvidenceError(f"context {key} must be a nonempty string")
    workers = context.get("apgar_parallel_host_workers")
    if not isinstance(workers, str) or UNSIGNED_PATTERN.fullmatch(workers) is None:
        raise EvidenceError("context apgar_parallel_host_workers must be canonical decimal")
    if not 1 <= int(workers) <= 1024:
        raise EvidenceError("context apgar_parallel_host_workers is outside supported bounds")
    for case in CASES:
        if _case_field(context, case, "nodes") == 0:
            raise EvidenceError(f"context apgar_case_{case} reports zero represented nodes")
        if _case_field(context, case, "persistent_device_bytes") == 0:
            raise EvidenceError(f"context apgar_case_{case} reports zero persistent bytes")
        if _case_field(context, case, "prepared_node_lookup_host_bytes") == 0:
            raise EvidenceError(f"context apgar_case_{case} reports zero lookup bytes")
        identities = context.get(f"apgar_case_{case}_policy_identities")
        if not isinstance(identities, str):
            raise EvidenceError(f"context {case} policy identities must be a string")
        parts = identities.split(",")
        if len(parts) != max(CANDIDATE_COUNTS):
            raise EvidenceError(f"context {case} must contain 512 policy identities")
        parsed: list[int] = []
        for part in parts:
            if UNSIGNED_PATTERN.fullmatch(part) is None or int(part) > UINT64_MAX:
                raise EvidenceError(f"context {case} contains an invalid policy identity")
            parsed.append(int(part))
        if len(set(parsed)) != len(parsed):
            raise EvidenceError(f"context {case} contains duplicate policy identities")


def _expected_worker_count(query_count: int) -> int:
    return min(
        MAXIMUM_VALIDATION_WORKERS,
        (query_count + VALIDATION_QUERIES_PER_WORKER - 1) // VALIDATION_QUERIES_PER_WORKER,
    )


def _validate_query_accounting(row: Mapping[str, Any], label: str, count: int) -> None:
    if _integer(row, "requested_candidate_count", label) != count:
        raise EvidenceError(f"{label} requested_candidate_count mismatch")
    reached = _integer(row, "reached_queries", label)
    failed = _integer(row, "failed_queries", label)
    if reached + failed != count:
        raise EvidenceError(f"{label} reached and failed counts do not cover the request")
    if sum(_integer(row, key, label) for key in FAILURE_COUNTERS) != failed:
        raise EvidenceError(f"{label} failure classes do not partition failed_queries")
    for key in ZERO_FAILURE_COUNTERS:
        if _integer(row, key, label) != 0:
            raise EvidenceError(f"{label} unexpectedly reports {key}")
    base = _integer(row, "base_policy_scalar_cost", label)
    minimum = _integer(row, "minimum_reported_policy_scalar_cost", label)
    total = _integer(row, "reachable_policy_scalar_cost_sum", label)
    if reached == 0 and (base, minimum, total) != (0, 0, 0):
        raise EvidenceError(f"{label} reports scalar costs for an entirely failed batch")
    if reached != 0 and minimum > total:
        raise EvidenceError(f"{label} minimum scalar cost exceeds its reachable cost sum")
    for prefix in ("outcome_checksum", "semantic_outcome_checksum"):
        _uint32(row, f"{prefix}_hi", label)
        _uint32(row, f"{prefix}_lo", label)
    if _counter(row, "process_peak_rss_available", label) != 1.0:
        raise EvidenceError(f"{label} lacks process peak RSS evidence")
    if _integer(row, "process_lifetime_peak_rss_bytes", label) == 0:
        raise EvidenceError(f"{label} reports an empty process peak RSS measurement")


def _validate_execution(
    row: Mapping[str, Any],
    label: str,
    generator: str,
    case: str,
    count: int,
    context: Mapping[str, Any],
    upload: Mapping[str, Any],
) -> None:
    if generator in CPU_GENERATORS:
        for key in GPU_COUNTERS:
            if _counter(row, key, label) != 0.0:
                raise EvidenceError(f"{label} CPU row unexpectedly reports GPU counter {key}")
        workers = _integer(row, "parallel_host_workers", label)
        expected = 1
        if generator == "parallel_cpu_astar":
            expected = min(count, int(str(context["apgar_parallel_host_workers"])))
        if workers != expected:
            raise EvidenceError(f"{label} CPU worker count mismatch")
        return

    persistent = _integer(row, "persistent_owned_vram_bytes", label)
    batch = _integer(row, "batch_owned_vram_bytes", label)
    capacity = _integer(row, "workspace_capacity_vram_bytes", label)
    peak = _integer(row, "peak_owned_vram_bytes", label)
    upload_persistent = _integer(upload, "persistent_owned_vram_bytes", f"upload/{case}")
    lookup = _integer(row, "prepared_node_lookup_host_bytes", label)
    upload_lookup = _integer(upload, "prepared_node_lookup_host_bytes", f"upload/{case}")
    if persistent == 0 or persistent != upload_persistent:
        raise EvidenceError(f"{label} persistent VRAM disagrees with prepared upload")
    if lookup == 0 or lookup != upload_lookup:
        raise EvidenceError(f"{label} prepared lookup disagrees with prepared upload")
    if batch == 0 or capacity < batch or peak != persistent + capacity:
        raise EvidenceError(f"{label} workspace capacity or simultaneous peak is inconsistent")
    if _integer(row, "chunk_rounds", label) != SWEEP_CHUNK_ROUNDS:
        raise EvidenceError(f"{label} CUDA sweep chunk size mismatch")
    dispatched = _integer(row, "dispatched_rounds", label)
    readbacks = _integer(row, "blocking_status_readback_count", label)
    expected_readbacks = 0 if dispatched == 0 else math.ceil(dispatched / SWEEP_CHUNK_ROUNDS)
    if readbacks != expected_readbacks:
        raise EvidenceError(f"{label} blocking readback accounting is inconsistent")
    finalization = _integer(row, "finalization_launch_count", label)
    if finalization > MAXIMUM_FINALIZATION_LAUNCHES or (dispatched == 0 and finalization != 1):
        raise EvidenceError(f"{label} finalization launch accounting is inconsistent")
    launches = _integer(row, "kernel_launch_count", label)
    possible = {
        SWEEP_FIXED_LAUNCHES + dispatched * kernels + finalization
        for kernels in SWEEP_KERNELS_PER_ROUND
    }
    if launches not in possible:
        raise EvidenceError(f"{label} CUDA sweep launch accounting is inconsistent")
    if _integer(row, "rounds_maximum", label) > dispatched:
        raise EvidenceError(f"{label} maximum query rounds exceed dispatched rounds")
    if _integer(row, "rounds_total", label) > count * dispatched:
        raise EvidenceError(f"{label} total query rounds exceed dispatched batch work")
    if _integer(row, "parallel_host_workers", label) != _expected_worker_count(count):
        raise EvidenceError(f"{label} compact validation worker count mismatch")
    if _integer(row, "gpu_batch_owned_host_bytes", label) == 0:
        raise EvidenceError(f"{label} GPU host workspace accounting must be nonzero")

    nodes = _case_field(context, case, "nodes")
    states = nodes * 9
    readback = _integer(row, "device_to_host_readback_bytes", label)
    fixed_readback = count * (RESULT_HEADER_BYTES + COMPACT_PATH_HEADER_BYTES)
    if readback < fixed_readback or (readback - fixed_readback) % COMPACT_PATH_STATE_BYTES != 0:
        raise EvidenceError(f"{label} compact readback bytes do not encode whole path states")
    packed_states = (readback - fixed_readback) // COMPACT_PATH_STATE_BYTES
    reached = _integer(row, "reached_queries", label)
    if packed_states > count * states or packed_states < reached:
        raise EvidenceError(f"{label} compact readback path-state count is outside bounds")


def _validate_admission(row: Mapping[str, Any], label: str, count: int, generator: str) -> None:
    reached = _integer(row, "reached_queries", label)
    accepted = _integer(row, "accepted_candidates", label)
    rejected = _integer(row, "rejected_candidates", label)
    builder = _integer(row, "builder_rejections", label)
    store = _integer(row, "store_rejections", label)
    if accepted + rejected != reached or builder + store != rejected:
        raise EvidenceError(f"{label} admission partitions are inconsistent")
    if _integer(row, "retained_candidate_pool_size", label) != accepted:
        raise EvidenceError(f"{label} retained pool does not equal accepted candidates")
    if _integer(row, "retained_rejection_records", label) != rejected:
        raise EvidenceError(f"{label} retained rejection records do not equal rejections")
    for key in ("unique_geometry_signatures", "unique_resource_signatures"):
        if _integer(row, key, label) != accepted:
            raise EvidenceError(f"{label} {key} does not match the deduplicated pool")
    if _integer(row, "nondominated_candidates", label) > accepted:
        raise EvidenceError(f"{label} nondominated count exceeds accepted candidates")
    accepted_bytes = _integer(row, "accepted_logical_bytes", label)
    rejection_bytes = _integer(row, "rejection_logical_bytes", label)
    peak_bytes = _integer(row, "peak_deterministic_host_bytes", label)
    if (accepted == 0) != (accepted_bytes == 0):
        raise EvidenceError(f"{label} accepted logical bytes are inconsistent")
    if (rejected == 0) != (rejection_bytes == 0):
        raise EvidenceError(f"{label} rejection logical bytes are inconsistent")
    minimum_peak = accepted_bytes + rejection_bytes
    if generator == CUDA_GENERATOR:
        minimum_peak = max(minimum_peak, 8 * count)
    if peak_bytes < minimum_peak:
        raise EvidenceError(f"{label} peak deterministic host bytes omit bounded payloads")
    _close(_counter(row, "accepted_candidate_yield", label), accepted / count, label)
    expected_acceptance = 0.0 if reached == 0 else accepted / reached
    _close(_counter(row, "generated_candidate_acceptance", label), expected_acceptance, label)
    for prefix in ("resource", "geometric"):
        mean = _counter(
            row,
            f"mean_{prefix}_jaccard" if prefix == "resource" else f"mean_{prefix}_overlap",
            label,
        )
        minimum = _counter(
            row,
            f"minimum_{prefix}_jaccard" if prefix == "resource" else f"minimum_{prefix}_overlap",
            label,
        )
        diversity = _counter(row, f"{prefix}_diversity", label)
        if mean > 1.0 or minimum > 1.0:
            raise EvidenceError(f"{label} {prefix} overlap is outside [0, 1]")
        _close(diversity, 0.0 if accepted < 2 else 1.0 - mean, label)
    _uint32(row, "candidate_order_checksum_hi", label)
    _uint32(row, "candidate_order_checksum_lo", label)


RowKey = tuple[str, str, str, int]
AggregateRows = dict[str, Mapping[str, Any]]


def _validate_rows(
    rows: Sequence[Any], context: Mapping[str, Any]
) -> tuple[dict[RowKey, AggregateRows], dict[str, AggregateRows]]:
    expected_runs = len(GENERATORS) * len(STAGES) * len(CASES) * len(CANDIDATE_COUNTS)
    if len(rows) != (expected_runs + len(CASES)) * len(AGGREGATES):
        raise EvidenceError("benchmark row count does not match the exact v3 canonical matrix")
    runs: dict[RowKey, AggregateRows] = {}
    uploads: dict[str, AggregateRows] = {}
    seen: set[tuple[str, str]] = set()
    for index, raw in enumerate(rows):
        row = _object(raw, f"benchmark row {index}")
        name = row.get("name")
        run_name = row.get("run_name")
        aggregate = row.get("aggregate_name")
        if (
            not isinstance(name, str)
            or not isinstance(run_name, str)
            or aggregate not in AGGREGATES
        ):
            raise EvidenceError(f"benchmark row {index} lacks canonical aggregate identity")
        if row.get("run_type") != "aggregate" or row.get("repetitions") != REPETITIONS:
            raise EvidenceError(f"benchmark row {name} is not a 20-repetition aggregate")
        if name != f"{run_name}_{aggregate}":
            raise EvidenceError(f"benchmark row {name} has a noncanonical aggregate suffix")
        identity = (run_name, str(aggregate))
        if identity in seen:
            raise EvidenceError(f"duplicate aggregate row {identity}")
        seen.add(identity)
        if row.get("time_unit") != "us":
            raise EvidenceError(f"benchmark row {name} does not report microseconds")
        _counter(row, "real_time", name)

        match = ROW_PATTERN.match(run_name)
        if match is not None:
            generator, stage, case, count_text = match.groups()
            count = int(count_text)
            if (
                generator not in GENERATORS
                or stage not in STAGES
                or case not in CASES
                or count not in CANDIDATE_COUNTS
            ):
                raise EvidenceError(f"unexpected v3 benchmark run {run_name}")
            key = (generator, stage, case, count)
            if aggregate in runs.setdefault(key, {}):
                raise EvidenceError(f"duplicate aggregate {aggregate} for {key}")
            runs[key][str(aggregate)] = row
            continue

        match = UPLOAD_PATTERN.match(run_name)
        if match is None or match.group(1) not in CASES:
            raise EvidenceError(f"unexpected benchmark row {name}")
        case = match.group(1)
        if aggregate in uploads.setdefault(case, {}):
            raise EvidenceError(f"duplicate upload aggregate {aggregate} for {case}")
        uploads[case][str(aggregate)] = row

    expected_keys = {
        (generator, stage, case, count)
        for generator in GENERATORS
        for stage in STAGES
        for case in CASES
        for count in CANDIDATE_COUNTS
    }
    if set(runs) != expected_keys or set(uploads) != set(CASES):
        raise EvidenceError("benchmark rows do not cover the exact v3 canonical matrix")
    for label, aggregates in [
        *((str(key), value) for key, value in runs.items()),
        *((f"upload/{case}", value) for case, value in uploads.items()),
    ]:
        if set(aggregates) != set(AGGREGATES):
            raise EvidenceError(f"{label} does not contain all four aggregates")
        mean = _counter(aggregates["mean"], "real_time", label)
        stddev = _counter(aggregates["stddev"], "real_time", label)
        cv = _counter(aggregates["cv"], "real_time", label)
        _close(cv, 0.0 if mean == 0.0 else stddev / mean, f"{label} real-time cv")

    for case, aggregates in uploads.items():
        median = aggregates["median"]
        label = f"upload/{case}"
        for key in (
            "persistent_owned_vram_bytes",
            "prepared_node_lookup_host_bytes",
            "prepared_uploads_per_second",
        ):
            _counter(median, key, label)
        persistent = _integer(median, "persistent_owned_vram_bytes", label)
        lookup = _integer(median, "prepared_node_lookup_host_bytes", label)
        if persistent != _case_field(context, case, "persistent_device_bytes"):
            raise EvidenceError(f"{label} persistent bytes disagree with case context")
        if lookup != _case_field(context, case, "prepared_node_lookup_host_bytes"):
            raise EvidenceError(f"{label} lookup bytes disagree with case context")

    for key, aggregates in runs.items():
        generator, stage, case, count = key
        median = aggregates["median"]
        label = "/".join((generator, stage, case, str(count)))
        required = COMMON_COUNTERS | (
            ADMISSION_COUNTERS if stage != "execution_readback" else set()
        )
        for counter_name in required:
            _counter(median, counter_name, label)
        for flag in ("deterministic", "differential_match", "ordered_results"):
            if _counter(median, flag, label) != 1.0:
                raise EvidenceError(f"{label} does not report {flag}=1")
        _validate_query_accounting(median, label, count)
        _validate_execution(median, label, generator, case, count, context, uploads[case]["median"])
        if stage != "execution_readback":
            _validate_admission(median, label, count, generator)
    return runs, uploads


def _semantic_summary(runs: Mapping[RowKey, AggregateRows]) -> dict[str, int]:
    requested = 0
    reached = 0
    unreachable = 0
    for case in CASES:
        for count in CANDIDATE_COUNTS:
            baseline = runs[("sequential_cpu_astar", "end_to_end", case, count)]["median"]
            label = f"semantic-baseline/{case}/{count}"
            expected = tuple(_integer(baseline, key, label) for key in SEMANTIC_COUNTERS)
            requested += count
            reached += _integer(baseline, "reached_queries", label)
            unreachable += _integer(baseline, "unreachable_queries", label)
            for generator in GENERATORS:
                for stage in STAGES:
                    row = runs[(generator, stage, case, count)]["median"]
                    actual = tuple(
                        _integer(row, key, f"semantic/{generator}/{stage}/{case}/{count}")
                        for key in SEMANTIC_COUNTERS
                    )
                    if actual != expected:
                        raise EvidenceError(
                            f"semantic/{generator}/{stage}/{case}/{count} differs from CPU oracle"
                        )
            for generator in GENERATORS:
                prepared = runs[(generator, "prepared_end_to_end", case, count)]["median"]
                cold = runs[(generator, "end_to_end", case, count)]["median"]
                prepared_admission = tuple(
                    _counter(prepared, key, f"prepared admission/{generator}/{case}/{count}")
                    for key in sorted(ADMISSION_SEMANTIC_COUNTERS)
                )
                cold_admission = tuple(
                    _counter(cold, key, f"cold admission/{generator}/{case}/{count}")
                    for key in sorted(ADMISSION_SEMANTIC_COUNTERS)
                )
                if prepared_admission != cold_admission:
                    raise EvidenceError(
                        f"{generator}/{case}/{count} prepared and cold admission differ"
                    )
    return {
        "generator_stage_medians": len(runs),
        "unique_requested_queries": requested,
        "reached_queries": reached,
        "unreachable_queries": unreachable,
        "unexpected_failure_queries": 0,
    }


def _comparisons(
    runs: Mapping[RowKey, AggregateRows],
) -> dict[str, dict[str, int | float]]:
    result: dict[str, dict[str, int | float]] = {}
    for stage in STAGES:
        summary = {
            "comparisons": 0,
            "nominal_cuda_median_wins": 0,
            "nominal_cpu_median_wins": 0,
            "nominal_median_ties": 0,
            "clear_cuda_mean_wins": 0,
            "clear_cpu_mean_wins": 0,
            "unclear_mean_differences": 0,
            "equal_row_geomean_cuda_speedup": 0.0,
            "pooled_cuda_throughput_speedup": 0.0,
        }
        cpu_median_sum = 0.0
        cuda_median_sum = 0.0
        log_speedup_sum = 0.0
        for case in CASES:
            for count in CANDIDATE_COUNTS:
                summary["comparisons"] += 1
                cuda = runs[(CUDA_GENERATOR, stage, case, count)]
                cpus = [runs[(generator, stage, case, count)] for generator in CPU_GENERATORS]
                cuda_median = _counter(cuda["median"], "real_time", "CUDA median")
                cpu_median = min(_counter(cpu["median"], "real_time", "CPU median") for cpu in cpus)
                if cuda_median == 0.0 or cpu_median == 0.0:
                    raise EvidenceError("comparison medians must be positive")
                cpu_median_sum += cpu_median
                cuda_median_sum += cuda_median
                log_speedup_sum += math.log(cpu_median / cuda_median)
                if cuda_median < cpu_median:
                    summary["nominal_cuda_median_wins"] += 1
                elif cuda_median > cpu_median:
                    summary["nominal_cpu_median_wins"] += 1
                else:
                    summary["nominal_median_ties"] += 1

                cuda_mean = _counter(cuda["mean"], "real_time", "CUDA mean")
                cuda_sd = _counter(cuda["stddev"], "real_time", "CUDA stddev")
                cuda_clear = True
                cpu_clear = False
                for cpu in cpus:
                    cpu_mean = _counter(cpu["mean"], "real_time", "CPU mean")
                    cpu_sd = _counter(cpu["stddev"], "real_time", "CPU stddev")
                    margin = MEAN_DIFFERENCE_Z * math.sqrt(
                        (cuda_sd * cuda_sd + cpu_sd * cpu_sd) / REPETITIONS
                    )
                    cuda_clear = cuda_clear and cuda_mean + margin < cpu_mean
                    cpu_clear = cpu_clear or cpu_mean + margin < cuda_mean
                if cuda_clear:
                    summary["clear_cuda_mean_wins"] += 1
                elif cpu_clear:
                    summary["clear_cpu_mean_wins"] += 1
                else:
                    summary["unclear_mean_differences"] += 1
        comparison_count = int(summary["comparisons"])
        summary["equal_row_geomean_cuda_speedup"] = math.exp(log_speedup_sum / comparison_count)
        summary["pooled_cuda_throughput_speedup"] = cpu_median_sum / cuda_median_sum
        result[stage] = summary
    return result


def _safe_path(root: pathlib.Path, value: Any, field: str) -> pathlib.Path:
    if not isinstance(value, str) or not value:
        raise EvidenceError(f"manifest {field} must be a nonempty string")
    relative = pathlib.PurePosixPath(value)
    if relative.is_absolute() or ".." in relative.parts:
        raise EvidenceError(f"manifest {field} must be a safe repository-relative path")
    path = root.joinpath(*relative.parts)
    if not path.is_file():
        raise EvidenceError(f"manifest {field} does not identify a file")
    return path


def _digest(manifest: Mapping[str, Any], field: str) -> str:
    value = manifest.get(field)
    if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
        raise EvidenceError(f"manifest {field} must be a lowercase SHA-256 digest")
    return value


def _read_json(path: pathlib.Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise EvidenceError(f"cannot read {label} {path}: {error}") from error


def _validate_digest(path: pathlib.Path, expected: str, label: str) -> None:
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != expected:
        raise EvidenceError(f"{label} checksum mismatch: expected {expected}, got {actual}")


def validate(root: pathlib.Path, manifest_path: pathlib.Path) -> None:
    manifest = _object(_read_json(manifest_path, "manifest"), "manifest")
    if manifest.get("schema") != MANIFEST_SCHEMA or manifest.get("result_schema") != RESULT_SCHEMA:
        raise EvidenceError("unsupported Phase 3 follow-up evidence manifest or result schema")
    commit = manifest.get("source_commit")
    if not isinstance(commit, str) or COMMIT_PATTERN.fullmatch(commit) is None:
        raise EvidenceError("manifest source_commit must be a 40-character lowercase commit")
    paths = {
        kind: _safe_path(root, manifest.get(f"{kind}_file"), f"{kind}_file")
        for kind in ("result", "report", "decision")
    }
    digests = {kind: _digest(manifest, f"{kind}_sha256") for kind in paths}
    for kind, path in paths.items():
        _validate_digest(path, digests[kind], kind)

    result = _object(_read_json(paths["result"], "result"), "benchmark result")
    _validate_numbers(result, "benchmark result")
    context = _object(result.get("context"), "benchmark context")
    _validate_context(context, commit)
    runs, _ = _validate_rows(_array(result.get("benchmarks"), "benchmark rows"), context)
    correctness = _semantic_summary(runs)
    comparisons = _comparisons(runs)
    if manifest.get("expected_correctness_summary") != correctness:
        raise EvidenceError("manifest correctness summary does not match the raw evidence")
    if manifest.get("expected_stage_comparisons") != comparisons:
        raise EvidenceError("manifest stage comparisons do not match the raw evidence")

    result_file = str(manifest.get("result_file"))
    correctness_token = json.dumps(correctness, sort_keys=True, separators=(",", ":"))
    comparison_token = json.dumps(comparisons, sort_keys=True, separators=(",", ":"))
    for kind in ("report", "decision"):
        document = paths[kind].read_text(encoding="utf-8")
        for token in (result_file, digests["result"], commit, correctness_token, comparison_token):
            if token not in document:
                raise EvidenceError(f"{kind} does not contain bound v3 evidence token {token}")


def _default_paths() -> tuple[pathlib.Path, pathlib.Path]:
    test_srcdir = os.environ.get("TEST_SRCDIR")
    test_workspace = os.environ.get("TEST_WORKSPACE")
    if not test_srcdir or not test_workspace:
        raise EvidenceError("provide REPOSITORY_ROOT MANIFEST outside Bazel test runfiles")
    root = pathlib.Path(test_srcdir) / test_workspace
    manifest = root / "benchmarks/results/phase3_persistent_compact_sweep_manifest_v1.json"
    return root, manifest


def main(argv: list[str]) -> int:
    try:
        if len(argv) == 1:
            root, manifest = _default_paths()
        elif len(argv) == 3:
            root = pathlib.Path(argv[1]).resolve()
            manifest = pathlib.Path(argv[2]).resolve()
        else:
            raise EvidenceError(
                "usage: validate_phase3_followup_evidence.py [REPOSITORY_ROOT MANIFEST]"
            )
        validate(root, manifest)
    except (EvidenceError, OSError) as error:
        print(f"Phase 3 follow-up evidence validation failed: {error}", file=sys.stderr)
        return 1
    print("Phase 3 follow-up evidence validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
