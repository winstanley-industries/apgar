"""Strict Phase 4 exact-small publication join and fixed-pool oracle."""

from __future__ import annotations

import argparse
import itertools
import json
import os
import pathlib
import re
import struct
import subprocess
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from python.runfiles import runfiles as bazel_runfiles

from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

EvidenceError = raw_validator.EvidenceError
StableHashBuilder = raw_validator.StableHashBuilder

_U16_MAX = (1 << 16) - 1
_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_I64_MIN = -(1 << 63)
_I64_MAX = (1 << 63) - 1
_COMMIT = re.compile(r"[0-9a-f]{40}")
_MAXIMUM_SNAPSHOT_BYTES = 64 * 1024 * 1024
_MAXIMUM_OUTPUT_BYTES = 1024 * 1024
_MAXIMUM_JSON_NESTING_DEPTH = 64
_POOL_COUNT = 6
_MAXIMUM_CANDIDATES_PER_POOL = 6
_MAXIMUM_CANDIDATES = 36
_MAXIMUM_COMPONENT_ROWS = 100_000
_MAXIMUM_EXPANDED_EDGES = 100_000
_MAXIMUM_CAPACITY_OVERRIDES = 100_000
_MAXIMUM_LOGICAL_BYTES = 32 * 1024 * 1024
_MAXIMUM_CARTESIAN_PRODUCT = 4096
_MAXIMUM_SERIALIZED_BYTES = 64 * 1024 * 1024
_ADMISSION_MAGIC = b"APGARP4E"

_TOP_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "decision_eligible",
    "config",
    "execution_order",
    "root_seed",
    "corpus_checksum",
    "descriptor_fingerprint",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "workload_roster",
    "workload_net_roster_checksum",
    "budget_checksum",
    "raw_cell_plan_checksum",
    "raw_cell_artifact_checksum",
    "raw_source_envelope_checksum",
    "raw_reference",
    "per_net_report_artifact_checksum",
    "per_net_report_source_envelope_checksum",
    "per_net_candidate_telemetry_checksum",
    "candidate_semantics",
    "candidate_semantic_checksum",
    "candidate_session_checksum",
    "final_pool_manifest_checksum",
    "final_rejection_manifest_checksum",
    "production_outcome_source",
    "capacity",
    "capacity_model_checksum",
    "cartesian_product",
    "pools",
    "production_selections",
    "production_outcome",
    "maximum_serialized_bytes",
    "artifact_checksum",
)
_ENTITY_FIELDS = ("id", "generation")
_ID_FIELDS = ("high", "low")
_RESOURCE_FIELDS = ("layer", "lattice_x", "lattice_y", "direction")
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
_CAPACITY_FIELDS = ("schema_version", "associations", "default_capacity_units", "overrides")
_CAPACITY_ASSOCIATION_FIELDS = (
    "board_content_hash",
    "compiler_profile_fingerprint",
    "geometry_compiler_version",
)
_OVERRIDE_FIELDS = ("resource", "capacity_units")
_POOL_FIELDS = ("net", "candidates")
_CANDIDATE_FIELDS = (
    "schema_major",
    "schema_minor",
    "id",
    "net",
    "intended_terminals",
    "associations",
    "geometry_schema_version",
    "resource_schema_version",
    "policy",
    "policy_identity",
    "provenance",
    "geometry",
    "metrics",
    "constraints",
    "geometry_signature",
    "resource_signature",
    "payload_checksum",
    "logical_bytes",
    "intrinsic_cost",
    "resource_spans",
)
_CANDIDATE_ASSOCIATION_FIELDS = (
    "board_content_hash",
    "compiler_profile_fingerprint",
    "geometry_compiler_version",
    "routing_profile_fingerprint",
    "rule_bucket_identity",
)
_POLICY_FIELDS = (
    "schema_version",
    "objective",
    "deterministic_seed",
    "candidate_ordinal",
    "orthogonal_step_surcharge",
    "diagonal_step_surcharge",
    "bend_surcharge",
    "banned_resources",
    "resource_penalties",
)
_PENALTY_FIELDS = ("resource", "additional_cost")
_PROVENANCE_FIELDS = (
    "generator",
    "generator_version",
    "backend",
    "supported_device_class",
    "deterministic_seed",
    "batch_identity",
    "query_identity",
    "candidate_ordinal",
)
_LINE_FIELDS = ("kind", "layer", "start", "end")
_VIA_FIELDS = ("kind", "template_id", "position", "start_layer", "end_layer")
_POINT_FIELDS = ("x", "y")
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
_CONSTRAINT_FIELDS = (
    "supported_hard_constraints_satisfied",
    "unsupported_rules_remain",
    "connected_intended_terminal_count",
    "exact_validation_code",
)
_SPAN_FIELDS = (
    "layer",
    "lattice_x",
    "lattice_y",
    "direction",
    "edge_count",
    "usage_units",
)
_SELECTION_FIELDS = (
    "net",
    "status",
    "candidate_id",
    "candidate_payload_checksum",
    "intrinsic_cost",
)
_OUTCOME_FIELDS = (
    "selected_net_count",
    "no_candidate_net_count",
    "overused_resource_count",
    "total_overuse_units",
    "total_intrinsic_cost",
    "world_checksum",
)
_OBJECTIVE_FIELDS = (
    "selected_net_count",
    "total_overuse_units",
    "total_intrinsic_base_cost",
)
_WITNESS_FIELDS = ("net", "candidate_id")
_ORACLE_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "decision_eligible",
    "case_id",
    "raw_artifact_checksum",
    "per_net_report_artifact_checksum",
    "snapshot_artifact_checksum",
    "candidate_semantic_checksum",
    "cartesian_product",
    "production_objective",
    "optimum_objective",
    "production_overused_resource_count",
    "canonical_witness_overused_resource_count",
    "production_is_optimal",
    "optimum_count",
    "canonical_witness",
    "artifact_checksum",
)


class _AdmissionEncoder:
    def __init__(self) -> None:
        self.data = bytearray(_ADMISSION_MAGIC)

    def u8(self, value: int) -> None:
        self.data.extend(struct.pack("<B", value))

    def u16(self, value: int) -> None:
        self.data.extend(struct.pack("<H", value))

    def u32(self, value: int) -> None:
        self.data.extend(struct.pack("<I", value))

    def u64(self, value: int) -> None:
        self.data.extend(struct.pack("<Q", value))

    def i64(self, value: int) -> None:
        self.data.extend(struct.pack("<q", value))

    def string(self, value: str) -> None:
        encoded = value.encode("utf-8")
        self.u32(len(encoded))
        self.data.extend(encoded)


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


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise EvidenceError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u16(value: Any, label: str) -> int:
    return _uint(value, _U16_MAX, label)


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _i64(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not _I64_MIN <= value <= _I64_MAX:
        raise EvidenceError(f"{label} must be a signed 64-bit integer")
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


def _check_nesting(value: Any, label: str) -> None:
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


def read_snapshot_document(path: pathlib.Path) -> Mapping[str, Any]:
    """Read once under the frozen cap and require canonical JSON bytes."""
    try:
        with path.open("rb") as stream:
            encoded = stream.read(_MAXIMUM_SNAPSHOT_BYTES + 1)
        if len(encoded) > _MAXIMUM_SNAPSHOT_BYTES:
            raise EvidenceError("snapshot exceeds the 64 MiB input bound")
        text = encoded.decode("utf-8")
        document = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_nesting(document, "snapshot")
        canonical = (
            json.dumps(document, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
        )
        if text != canonical:
            raise EvidenceError("snapshot must be canonical one-line JSON followed by one LF")
        return _object(document, "snapshot")
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RecursionError) as error:
        if isinstance(error, EvidenceError):
            raise
        raise EvidenceError(f"cannot read exact-small snapshot {path}: {error}") from error


class _Encoder:
    def __init__(self, domain: str) -> None:
        self.hash = StableHashBuilder()
        self.hash.string(domain)
        self.bytes = 0

    def byte(self, value: int) -> None:
        self.hash.byte(value)
        self.bytes += 1

    def boolean(self, value: bool) -> None:
        self.hash.boolean(value)
        self.bytes += 1

    def u16(self, value: int) -> None:
        self.hash.byte(value)
        self.hash.byte(value >> 8)
        self.bytes += 2

    def u32(self, value: int) -> None:
        self.hash.u32(value)
        self.bytes += 4

    def u64(self, value: int) -> None:
        self.hash.u64(value)
        self.bytes += 8

    def i64(self, value: int) -> None:
        self.hash.u64(value & _U64_MAX)
        self.bytes += 8

    def string(self, value: str) -> None:
        encoded = value.encode("utf-8")
        self.hash.string(value)
        self.bytes += 8 + len(encoded)

    def finish(self) -> int:
        return self.hash.finish()


def _hash_i64(hashed: StableHashBuilder, value: int) -> None:
    hashed.u64(value & _U64_MAX)


def _entity(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _ENTITY_FIELDS, label)
    if _u64(result["id"], f"{label}.id") == 0:
        raise EvidenceError(f"{label}.id must be nonzero")
    _u32(result["generation"], f"{label}.generation")
    return result


def _hash128(value: Any, label: str, *, nonzero: bool = True) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _ID_FIELDS, label)
    high = _u64(result["high"], f"{label}.high")
    low = _u64(result["low"], f"{label}.low")
    if nonzero and high == 0 and low == 0:
        raise EvidenceError(f"{label} must be nonzero")
    return result


def _entity_key(value: Mapping[str, Any]) -> tuple[int, int]:
    return value["id"], value["generation"]


def _id_key(value: Mapping[str, Any]) -> tuple[int, int]:
    return value["high"], value["low"]


def _resource(value: Any, label: str) -> Mapping[str, Any]:
    result = _object(value, label)
    _fields(result, _RESOURCE_FIELDS, label)
    _u32(result["layer"], f"{label}.layer")
    _i64(result["lattice_x"], f"{label}.lattice_x")
    _i64(result["lattice_y"], f"{label}.lattice_y")
    _enum(result["direction"], {0, 1, 2, 3}, f"{label}.direction")
    return result


def _resource_key(value: Mapping[str, Any]) -> tuple[int, int, int, int]:
    return value["layer"], value["lattice_x"], value["lattice_y"], value["direction"]


def _encode_entity(encoder: _Encoder, value: Mapping[str, Any]) -> None:
    encoder.u64(value["id"])
    encoder.u32(value["generation"])


def _encode_id(encoder: _Encoder, value: Mapping[str, Any]) -> None:
    encoder.u64(value["high"])
    encoder.u64(value["low"])


def _encode_resource(encoder: _Encoder, value: Mapping[str, Any]) -> None:
    encoder.u32(value["layer"])
    encoder.i64(value["lattice_x"])
    encoder.i64(value["lattice_y"])
    encoder.byte(value["direction"])


def _parse_policy(value: Any, label: str) -> Mapping[str, Any]:
    policy = _object(value, label)
    _fields(policy, _POLICY_FIELDS, label)
    if _u32(policy["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")
    _enum(policy["objective"], {0, 1, 2, 3}, f"{label}.objective")
    _u64(policy["deterministic_seed"], f"{label}.deterministic_seed")
    _u32(policy["candidate_ordinal"], f"{label}.candidate_ordinal")
    for field in (
        "orthogonal_step_surcharge",
        "diagonal_step_surcharge",
        "bend_surcharge",
    ):
        _u64(policy[field], f"{label}.{field}")
    banned = _array(policy["banned_resources"], f"{label}.banned_resources")
    penalties = _array(policy["resource_penalties"], f"{label}.resource_penalties")
    if len(banned) + len(penalties) > _MAXIMUM_COMPONENT_ROWS:
        raise EvidenceError(f"{label} resource actions exceed the frozen bound")
    parsed_banned = [
        _resource(item, f"{label}.banned_resources[{index}]") for index, item in enumerate(banned)
    ]
    banned_keys = [_resource_key(item) for item in parsed_banned]
    if banned_keys != sorted(set(banned_keys)):
        raise EvidenceError(f"{label}.banned_resources must be strictly canonical")
    penalty_keys: list[tuple[int, int, int, int]] = []
    for index, item in enumerate(penalties):
        penalty_label = f"{label}.resource_penalties[{index}]"
        penalty = _object(item, penalty_label)
        _fields(penalty, _PENALTY_FIELDS, penalty_label)
        resource = _resource(penalty["resource"], f"{penalty_label}.resource")
        if _u64(penalty["additional_cost"], f"{penalty_label}.additional_cost") == 0:
            raise EvidenceError(f"{penalty_label}.additional_cost must be nonzero")
        penalty_keys.append(_resource_key(resource))
    if penalty_keys != sorted(set(penalty_keys)) or set(penalty_keys) & set(banned_keys):
        raise EvidenceError(f"{label}.resource_penalties are not canonical and disjoint")
    return policy


def _encode_policy(encoder: _Encoder, policy: Mapping[str, Any]) -> None:
    encoder.u32(policy["schema_version"])
    encoder.byte(policy["objective"])
    encoder.u64(policy["deterministic_seed"])
    encoder.u32(policy["candidate_ordinal"])
    encoder.u64(policy["orthogonal_step_surcharge"])
    encoder.u64(policy["diagonal_step_surcharge"])
    encoder.u64(policy["bend_surcharge"])
    encoder.u64(len(policy["banned_resources"]))
    for resource in policy["banned_resources"]:
        _encode_resource(encoder, resource)
    encoder.u64(len(policy["resource_penalties"]))
    for penalty in policy["resource_penalties"]:
        _encode_resource(encoder, penalty["resource"])
        encoder.u64(penalty["additional_cost"])


def _policy_identity(policy: Mapping[str, Any]) -> int:
    encoder = _Encoder("APGAR-CANDIDATE-POLICY-V1")
    _encode_policy(encoder, policy)
    return encoder.finish()


def _parse_provenance(value: Any, label: str) -> Mapping[str, Any]:
    provenance = _object(value, label)
    _fields(provenance, _PROVENANCE_FIELDS, label)
    generator = _enum(provenance["generator"], {0, 1, 2}, f"{label}.generator")
    if _u32(provenance["generator_version"], f"{label}.generator_version") == 0:
        raise EvidenceError(f"{label}.generator_version must be nonzero")
    backend = _enum(provenance["backend"], {0, 1}, f"{label}.backend")
    device = _string(provenance["supported_device_class"], f"{label}.supported_device_class")
    if not device or len(device.encode("utf-8")) > 1024:
        raise EvidenceError(f"{label}.supported_device_class is outside the v1 bound")
    for field in ("deterministic_seed", "batch_identity", "query_identity"):
        if _u64(provenance[field], f"{label}.{field}") == 0:
            raise EvidenceError(f"{label}.{field} must be nonzero")
    _u32(provenance["candidate_ordinal"], f"{label}.candidate_ordinal")
    if (generator == 0 and (backend != 0 or device != "cpu-reference-v1")) or (
        generator != 0 and (backend != 1 or not device.startswith("cuda-cc-"))
    ):
        raise EvidenceError(f"{label} generator/backend/device combination is unsupported")
    return provenance


def _encode_provenance(encoder: _Encoder, provenance: Mapping[str, Any]) -> None:
    encoder.byte(provenance["generator"])
    encoder.u32(provenance["generator_version"])
    encoder.byte(provenance["backend"])
    encoder.string(provenance["supported_device_class"])
    encoder.u64(provenance["deterministic_seed"])
    encoder.u64(provenance["batch_identity"])
    encoder.u64(provenance["query_identity"])
    encoder.u32(provenance["candidate_ordinal"])


def _parse_geometry(value: Any, label: str) -> Sequence[Mapping[str, Any]]:
    geometry = _array(value, label)
    if not geometry or len(geometry) > _MAXIMUM_COMPONENT_ROWS:
        raise EvidenceError(f"{label} count is outside the frozen bound")
    previous_end: tuple[int, int] | None = None
    previous_direction: tuple[int, int] | None = None
    previous_layer: int | None = None
    parsed: list[Mapping[str, Any]] = []
    for index, item in enumerate(geometry):
        primitive_label = f"{label}[{index}]"
        primitive = _object(item, primitive_label)
        kind = _string(primitive.get("kind"), f"{primitive_label}.kind")
        if kind != "line":
            if kind == "through_via":
                _fields(primitive, _VIA_FIELDS, primitive_label)
            raise EvidenceError(f"{primitive_label} uses unsupported geometry kind {kind!r}")
        _fields(primitive, _LINE_FIELDS, primitive_label)
        layer = _u32(primitive["layer"], f"{primitive_label}.layer")
        points: list[tuple[int, int]] = []
        for field in ("start", "end"):
            point_label = f"{primitive_label}.{field}"
            point = _object(primitive[field], point_label)
            _fields(point, _POINT_FIELDS, point_label)
            points.append(
                (
                    _i64(point["x"], f"{point_label}.x"),
                    _i64(point["y"], f"{point_label}.y"),
                )
            )
        start, end = points
        dx = end[0] - start[0]
        dy = end[1] - start[1]
        if start == end or (dx != 0 and dy != 0 and abs(dx) != abs(dy)):
            raise EvidenceError(f"{primitive_label} is not a nondegenerate H/V/45 line")
        direction = ((dx > 0) - (dx < 0), (dy > 0) - (dy < 0))
        if previous_end is not None and previous_end != start:
            raise EvidenceError(f"{primitive_label} is not contiguous with its predecessor")
        if previous_layer == layer and previous_direction == direction:
            raise EvidenceError(f"{primitive_label} is not maximally normalized")
        previous_end = end
        previous_direction = direction
        previous_layer = layer
        parsed.append(primitive)
    return parsed


def _encode_primitive(encoder: _Encoder, primitive: Mapping[str, Any]) -> None:
    if primitive["kind"] == "line":
        encoder.byte(1)
        encoder.u32(primitive["layer"])
        encoder.i64(primitive["start"]["x"])
        encoder.i64(primitive["start"]["y"])
        encoder.i64(primitive["end"]["x"])
        encoder.i64(primitive["end"]["y"])
        return
    encoder.byte(2)
    encoder.u64(primitive["template_id"])
    encoder.i64(primitive["position"]["x"])
    encoder.i64(primitive["position"]["y"])
    encoder.u32(primitive["start_layer"])
    encoder.u32(primitive["end_layer"])


def _parse_metrics(value: Any, label: str) -> Mapping[str, Any]:
    metrics = _object(value, label)
    _fields(metrics, _METRICS_FIELDS, label)
    for field in _METRICS_FIELDS:
        _u64(metrics[field], f"{label}.{field}")
    return metrics


def _encode_metrics(encoder: _Encoder, metrics: Mapping[str, Any]) -> None:
    for field in _METRICS_FIELDS:
        encoder.u64(metrics[field])


def _parse_constraints(value: Any, label: str) -> Mapping[str, Any]:
    constraints = _object(value, label)
    _fields(constraints, _CONSTRAINT_FIELDS, label)
    supported = _boolean(
        constraints["supported_hard_constraints_satisfied"],
        f"{label}.supported_hard_constraints_satisfied",
    )
    unsupported = _boolean(
        constraints["unsupported_rules_remain"], f"{label}.unsupported_rules_remain"
    )
    connected = _u32(
        constraints["connected_intended_terminal_count"],
        f"{label}.connected_intended_terminal_count",
    )
    code = _enum(
        constraints["exact_validation_code"], {0, 1, 2, 3}, f"{label}.exact_validation_code"
    )
    if not supported or unsupported or connected != 2 or code != 0:
        raise EvidenceError(f"{label} is not the canonical passed exact assessment")
    return constraints


def _encode_constraints(encoder: _Encoder, constraints: Mapping[str, Any]) -> None:
    encoder.boolean(constraints["supported_hard_constraints_satisfied"])
    encoder.boolean(constraints["unsupported_rules_remain"])
    encoder.u32(constraints["connected_intended_terminal_count"])
    encoder.byte(constraints["exact_validation_code"])


def _span_key(span: Mapping[str, Any]) -> tuple[int, int, int, int, int, int]:
    return (
        span["layer"],
        span["lattice_x"],
        span["lattice_y"],
        span["direction"],
        span["edge_count"],
        span["usage_units"],
    )


def _parse_spans(value: Any, label: str) -> Sequence[Mapping[str, Any]]:
    spans = _array(value, label)
    if not spans or len(spans) > _MAXIMUM_COMPONENT_ROWS:
        raise EvidenceError(f"{label} count is outside the frozen bound")
    parsed: list[Mapping[str, Any]] = []
    expanded: set[tuple[int, int, int, int]] = set()
    previous_key: tuple[int, int, int, int, int, int] | None = None
    previous_last: tuple[int, int] | None = None
    previous_span: Mapping[str, Any] | None = None
    for index, item in enumerate(spans):
        span_label = f"{label}[{index}]"
        span = _object(item, span_label)
        _fields(span, _SPAN_FIELDS, span_label)
        _u32(span["layer"], f"{span_label}.layer")
        _i64(span["lattice_x"], f"{span_label}.lattice_x")
        _i64(span["lattice_y"], f"{span_label}.lattice_y")
        direction = _enum(span["direction"], {0, 1, 2, 3}, f"{span_label}.direction")
        count = _u32(span["edge_count"], f"{span_label}.edge_count")
        usage = _u32(span["usage_units"], f"{span_label}.usage_units")
        if count == 0 or usage != 1:
            raise EvidenceError(f"{span_label} must have nonzero count and usage one")
        key = _span_key(span)
        if previous_key is not None and previous_key >= key:
            raise EvidenceError(f"{label} must be in strict canonical span order")
        dx, dy = ((1, 0), (1, 1), (0, 1), (1, -1))[direction]
        last_x = span["lattice_x"] + dx * (count - 1)
        last_y = span["lattice_y"] + dy * (count - 1)
        move_dx, move_dy = ((1, 0), (1, 1), (0, 1), (-1, 1))[direction]
        for coordinate in (
            last_x,
            last_y,
            span["lattice_x"] + move_dx,
            span["lattice_y"] + move_dy,
            last_x + move_dx,
            last_y + move_dy,
        ):
            if not _I64_MIN <= coordinate <= _I64_MAX:
                raise EvidenceError(f"{span_label} source or endpoint overflows int64")
        if (
            previous_span is not None
            and previous_span["layer"] == span["layer"]
            and previous_span["direction"] == direction
            and previous_last is not None
            and (previous_last[0] + dx, previous_last[1] + dy)
            == (span["lattice_x"], span["lattice_y"])
        ):
            raise EvidenceError(f"{span_label} is not maximally coalesced")
        for offset in range(count):
            resource = (
                span["layer"],
                span["lattice_x"] + dx * offset,
                span["lattice_y"] + dy * offset,
                direction,
            )
            if resource in expanded:
                raise EvidenceError(f"{span_label} overlaps another atomic resource")
            expanded.add(resource)
        previous_key = key
        previous_last = (last_x, last_y)
        previous_span = span
        parsed.append(span)
    return parsed


def _encode_span(encoder: _Encoder, span: Mapping[str, Any]) -> None:
    encoder.u32(span["layer"])
    encoder.i64(span["lattice_x"])
    encoder.i64(span["lattice_y"])
    encoder.byte(span["direction"])
    encoder.u32(span["edge_count"])
    encoder.u32(span["usage_units"])


def _derive_candidate_fields(
    geometry: Sequence[Mapping[str, Any]],
    policy: Mapping[str, Any],
    remaining: dict[str, int],
) -> tuple[list[dict[str, int]], dict[str, int]]:
    direction_for_delta = {
        (1, 0): 0,
        (1, 1): 1,
        (0, 1): 2,
        (-1, 1): 3,
        (-1, 0): 4,
        (-1, -1): 5,
        (0, -1): 6,
        (1, -1): 7,
    }
    movement = ((1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1))
    banned = {_resource_key(resource) for resource in policy["banned_resources"]}
    penalties = {
        _resource_key(penalty["resource"]): penalty["additional_cost"]
        for penalty in policy["resource_penalties"]
    }
    atomic: list[tuple[int, int, int, int]] = []
    scalar_cost = intrinsic_cost = orthogonal = diagonal = bends = 0
    incoming: int | None = None
    for primitive in geometry:
        start = primitive["start"]
        end = primitive["end"]
        delta_x = end["x"] - start["x"]
        delta_y = end["y"] - start["y"]
        step_count = max(abs(delta_x), abs(delta_y))
        _consume_budget(remaining, "geometry_steps", step_count)
        direction = direction_for_delta[
            ((delta_x > 0) - (delta_x < 0), (delta_y > 0) - (delta_y < 0))
        ]
        step_x, step_y = movement[direction]
        source_x, source_y = start["x"], start["y"]
        for _ in range(step_count):
            canonical_direction = direction
            canonical_x, canonical_y = source_x, source_y
            if direction == 4:
                canonical_x -= 1
                canonical_direction = 0
            elif direction == 5:
                canonical_x -= 1
                canonical_y -= 1
                canonical_direction = 1
            elif direction == 6:
                canonical_y -= 1
                canonical_direction = 2
            elif direction == 7:
                canonical_x += 1
                canonical_y -= 1
                canonical_direction = 3
            resource = (primitive["layer"], canonical_x, canonical_y, canonical_direction)
            if resource in banned:
                raise EvidenceError("candidate geometry traverses a policy-banned resource")
            is_diagonal = direction in {1, 3, 5, 7}
            bend = incoming is not None and incoming != direction
            base = 1414 if is_diagonal else 1000
            if bend:
                base += 100
                bends += 1
            policy_cost = base
            policy_cost += (
                policy["diagonal_step_surcharge"]
                if is_diagonal
                else policy["orthogonal_step_surcharge"]
            )
            if bend:
                policy_cost += policy["bend_surcharge"]
            policy_cost += penalties.get(resource, 0)
            scalar_cost += policy_cost
            intrinsic_cost += base
            if scalar_cost > _U64_MAX or intrinsic_cost > _U64_MAX:
                raise EvidenceError("candidate reconstructed cost overflows uint64")
            if is_diagonal:
                diagonal += 1
            else:
                orthogonal += 1
            atomic.append(resource)
            incoming = direction
            source_x += step_x
            source_y += step_y
    atomic.sort()
    if len(atomic) != len(set(atomic)):
        raise EvidenceError("candidate geometry reuses one physical edge")
    spans: list[dict[str, int]] = []
    for layer, lattice_x, lattice_y, direction in atomic:
        dx, dy = ((1, 0), (1, 1), (0, 1), (1, -1))[direction]
        if spans:
            previous = spans[-1]
            expected_x = previous["lattice_x"] + dx * previous["edge_count"]
            expected_y = previous["lattice_y"] + dy * previous["edge_count"]
            if (
                previous["layer"] == layer
                and previous["direction"] == direction
                and expected_x == lattice_x
                and expected_y == lattice_y
            ):
                previous["edge_count"] += 1
                continue
        spans.append(
            {
                "layer": layer,
                "lattice_x": lattice_x,
                "lattice_y": lattice_y,
                "direction": direction,
                "edge_count": 1,
                "usage_units": 1,
            }
        )
    metrics = {
        "scalar_policy_cost": scalar_cost,
        "intrinsic_base_cost": intrinsic_cost,
        "orthogonal_step_count": orthogonal,
        "diagonal_step_count": diagonal,
        "bend_count": bends,
        "line_primitive_count": len(geometry),
        "via_count": 0,
        "axis_aligned_length_dbu": orthogonal,
        "diagonal_projection_dbu": diagonal,
    }
    return spans, metrics


def _geometry_signature(geometry: Sequence[Mapping[str, Any]]) -> tuple[int, int]:
    halves = []
    for domain in ("APGAR-CANDIDATE-GEOMETRY-V1-A", "APGAR-CANDIDATE-GEOMETRY-V1-B"):
        encoder = _Encoder(domain)
        encoder.u64(len(geometry))
        for primitive in geometry:
            _encode_primitive(encoder, primitive)
        halves.append(encoder.finish())
    return halves[0], halves[1]


def _resource_signature(spans: Sequence[Mapping[str, Any]]) -> tuple[int, int]:
    halves = []
    for domain in ("APGAR-CANDIDATE-RESOURCES-V1-A", "APGAR-CANDIDATE-RESOURCES-V1-B"):
        encoder = _Encoder(domain)
        encoder.u64(len(spans))
        for span in spans:
            _encode_span(encoder, span)
        halves.append(encoder.finish())
    return halves[0], halves[1]


def _candidate_id(candidate: Mapping[str, Any]) -> tuple[int, int]:
    halves = []
    for domain in ("APGAR-CANDIDATE-ID-V1-A", "APGAR-CANDIDATE-ID-V1-B"):
        encoder = _Encoder(domain)
        _encode_entity(encoder, candidate["net"])
        associations = candidate["associations"]
        encoder.u64(associations["board_content_hash"])
        encoder.u64(associations["compiler_profile_fingerprint"])
        encoder.u32(associations["geometry_compiler_version"])
        encoder.u64(associations["routing_profile_fingerprint"])
        encoder.u64(associations["rule_bucket_identity"])
        encoder.u64(candidate["policy_identity"])
        _encode_provenance(encoder, candidate["provenance"])
        halves.append(encoder.finish())
    if halves == [0, 0]:
        halves[1] = 1
    return halves[0], halves[1]


def _candidate_payload(candidate: Mapping[str, Any]) -> tuple[int, int]:
    encoder = _Encoder("APGAR-ROUTE-CANDIDATE-V1")
    encoder.u16(candidate["schema_major"])
    encoder.u16(candidate["schema_minor"])
    _encode_id(encoder, candidate["id"])
    _encode_entity(encoder, candidate["net"])
    for terminal in candidate["intended_terminals"]:
        _encode_entity(encoder, terminal)
    associations = candidate["associations"]
    encoder.u64(associations["board_content_hash"])
    encoder.u64(associations["compiler_profile_fingerprint"])
    encoder.u32(associations["geometry_compiler_version"])
    encoder.u64(associations["routing_profile_fingerprint"])
    encoder.u64(associations["rule_bucket_identity"])
    encoder.u32(candidate["geometry_schema_version"])
    encoder.u32(candidate["resource_schema_version"])
    _encode_policy(encoder, candidate["policy"])
    encoder.u64(candidate["policy_identity"])
    _encode_provenance(encoder, candidate["provenance"])
    encoder.u64(len(candidate["geometry"]))
    for primitive in candidate["geometry"]:
        _encode_primitive(encoder, primitive)
    encoder.u64(len(candidate["resource_spans"]))
    for span in candidate["resource_spans"]:
        _encode_span(encoder, span)
    _encode_metrics(encoder, candidate["metrics"])
    _encode_constraints(encoder, candidate["constraints"])
    _encode_id(encoder, candidate["geometry_signature"])
    _encode_id(encoder, candidate["resource_signature"])
    return encoder.finish(), encoder.bytes + 16


def _parse_candidate(
    value: Any,
    label: str,
    expected_net: Mapping[str, Any],
    snapshot: Mapping[str, Any],
    remaining: dict[str, int],
) -> Mapping[str, Any]:
    _consume_budget(remaining, "candidates", 1)
    candidate = _object(value, label)
    _fields(candidate, _CANDIDATE_FIELDS, label)
    if (
        _u16(candidate["schema_major"], f"{label}.schema_major") != 1
        or _u16(candidate["schema_minor"], f"{label}.schema_minor") != 0
    ):
        raise EvidenceError(f"{label} schema must be 1.0")
    identity = _hash128(candidate["id"], f"{label}.id")
    net = _entity(candidate["net"], f"{label}.net")
    if net != expected_net:
        raise EvidenceError(f"{label}.net differs from its pool")
    terminals = _array(candidate["intended_terminals"], f"{label}.intended_terminals")
    if len(terminals) != 2:
        raise EvidenceError(f"{label}.intended_terminals must contain exactly two rows")
    parsed_terminals = [
        _entity(item, f"{label}.intended_terminals[{index}]")
        for index, item in enumerate(terminals)
    ]
    if parsed_terminals[0] == parsed_terminals[1]:
        raise EvidenceError(f"{label}.intended_terminals must be distinct")
    associations = _object(candidate["associations"], f"{label}.associations")
    _fields(associations, _CANDIDATE_ASSOCIATION_FIELDS, f"{label}.associations")
    for field in _CANDIDATE_ASSOCIATION_FIELDS:
        parser = _u32 if field == "geometry_compiler_version" else _u64
        if parser(associations[field], f"{label}.associations.{field}") == 0:
            raise EvidenceError(f"{label}.associations.{field} must be nonzero")
    if associations["board_content_hash"] != snapshot["board_content_hash"]:
        raise EvidenceError(f"{label} is associated with another Board")
    if (
        _u32(candidate["geometry_schema_version"], f"{label}.geometry_schema_version") != 1
        or _u32(candidate["resource_schema_version"], f"{label}.resource_schema_version") != 1
    ):
        raise EvidenceError(f"{label} geometry/resource schema must be 1")
    raw_policy = candidate["policy"]
    _consume_budget(
        remaining,
        "policy_rows",
        len(raw_policy["banned_resources"]) + len(raw_policy["resource_penalties"]),
    )
    policy = _parse_policy(raw_policy, f"{label}.policy")
    if _u64(candidate["policy_identity"], f"{label}.policy_identity") != _policy_identity(policy):
        raise EvidenceError(f"{label}.policy_identity does not authenticate complete policy")
    provenance = _parse_provenance(candidate["provenance"], f"{label}.provenance")
    if (
        provenance["candidate_ordinal"] != policy["candidate_ordinal"]
        or provenance["deterministic_seed"] != policy["deterministic_seed"]
    ):
        raise EvidenceError(f"{label} policy/provenance seed or ordinal differs")
    _consume_budget(remaining, "geometry_rows", len(candidate["geometry"]))
    geometry = _parse_geometry(candidate["geometry"], f"{label}.geometry")
    metrics = _parse_metrics(candidate["metrics"], f"{label}.metrics")
    if metrics["line_primitive_count"] != len(geometry) or metrics["via_count"] != 0:
        raise EvidenceError(f"{label}.metrics primitive counts do not match geometry")
    _parse_constraints(candidate["constraints"], f"{label}.constraints")
    geometry_signature = _hash128(candidate["geometry_signature"], f"{label}.geometry_signature")
    resource_signature = _hash128(candidate["resource_signature"], f"{label}.resource_signature")
    _consume_budget(remaining, "span_rows", len(candidate["resource_spans"]))
    _consume_budget(
        remaining,
        "expanded_edges",
        sum(span["edge_count"] for span in candidate["resource_spans"]),
    )
    spans = _parse_spans(candidate["resource_spans"], f"{label}.resource_spans")
    derived_spans, derived_metrics = _derive_candidate_fields(geometry, policy, remaining)
    if list(spans) != derived_spans:
        raise EvidenceError(f"{label}.resource_spans differ from exact geometry reconstruction")
    if metrics != derived_metrics:
        raise EvidenceError(f"{label}.metrics differ from exact geometry/resource reconstruction")
    if _id_key(geometry_signature) != _geometry_signature(geometry):
        raise EvidenceError(f"{label}.geometry_signature is invalid")
    if _id_key(resource_signature) != _resource_signature(spans):
        raise EvidenceError(f"{label}.resource_signature is invalid")
    if _id_key(identity) != _candidate_id(candidate):
        raise EvidenceError(f"{label}.id is not derivable from net/policy/provenance")
    payload, logical_bytes = _candidate_payload(candidate)
    if _u64(candidate["payload_checksum"], f"{label}.payload_checksum") != payload:
        raise EvidenceError(f"{label}.payload_checksum is invalid")
    if _u64(candidate["logical_bytes"], f"{label}.logical_bytes") != logical_bytes:
        raise EvidenceError(f"{label}.logical_bytes is invalid")
    _consume_budget(remaining, "logical_bytes", candidate["logical_bytes"])
    intrinsic = _u64(candidate["intrinsic_cost"], f"{label}.intrinsic_cost")
    if intrinsic != metrics["intrinsic_base_cost"]:
        raise EvidenceError(f"{label}.intrinsic_cost must be unweighted intrinsic_base_cost")
    return candidate


def _hash_entity(hashed: StableHashBuilder, entity: Mapping[str, Any]) -> None:
    hashed.u64(entity["id"])
    hashed.u32(entity["generation"])


def _hash_id(hashed: StableHashBuilder, identity: Mapping[str, Any]) -> None:
    hashed.u64(identity["high"])
    hashed.u64(identity["low"])


def _hash_resource(hashed: StableHashBuilder, resource: Mapping[str, Any]) -> None:
    hashed.u32(resource["layer"])
    _hash_i64(hashed, resource["lattice_x"])
    _hash_i64(hashed, resource["lattice_y"])
    hashed.byte(resource["direction"])


def _pool_manifest(pool: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-ONE-WORLD-POOL-MANIFEST-V1")
    _hash_entity(hashed, pool["net"])
    hashed.u64(len(pool["candidates"]))
    for candidate in pool["candidates"]:
        _hash_id(hashed, candidate["id"])
        hashed.u64(candidate["payload_checksum"])
    return hashed.finish()


def _pools_manifest(pools: Sequence[Mapping[str, Any]]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-ONE-WORLD-POOLS-MANIFEST-V1")
    hashed.u64(len(pools))
    for pool in pools:
        _hash_entity(hashed, pool["net"])
        hashed.u64(_pool_manifest(pool))
    return hashed.finish()


def _capacity_checksum(snapshot: Mapping[str, Any]) -> int:
    capacity = snapshot["capacity"]
    hashed = StableHashBuilder()
    hashed.string("APGAR-RESOURCE-CAPACITY-MODEL-V1")
    hashed.u32(capacity["schema_version"])
    associations = capacity["associations"]
    hashed.u64(associations["board_content_hash"])
    hashed.u64(associations["compiler_profile_fingerprint"])
    hashed.u32(associations["geometry_compiler_version"])
    hashed.u32(capacity["default_capacity_units"])
    hashed.u64(len(capacity["overrides"]))
    for override in capacity["overrides"]:
        _hash_resource(hashed, override["resource"])
        hashed.u32(override["capacity_units"])
    return hashed.finish()


def _hash_outcome(hashed: StableHashBuilder, outcome: Mapping[str, Any]) -> None:
    for field in _OUTCOME_FIELDS:
        hashed.u64(outcome[field])


def _hash_semantics(hashed: StableHashBuilder, semantics: Mapping[str, Any]) -> None:
    hashed.u32(semantics["schema_version"])
    hashed.byte(semantics["arm"])
    hashed.byte(semantics["execution_order"])
    hashed.u32(semantics["corpus_version"])
    hashed.u64(semantics["corpus_checksum"])
    hashed.u32(semantics["case_id"])
    for field in (
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "capacity_model_checksum",
        "budget_checksum",
    ):
        hashed.u64(semantics[field])
    for field in ("workload_net_count", "requested_pool_size", "repetition_index"):
        hashed.u32(semantics[field])
    hashed.u64(semantics["root_seed"])
    for field in (
        "preparation_worker_count",
        "baseline_sweeps",
        "candidate_regeneration_epochs",
    ):
        hashed.u32(semantics[field])
    hashed.u64(semantics["candidate_columns_per_epoch"])
    hashed.u32(semantics["candidate_terminal_selection_rounds"])
    for field in _BUDGET_FIELDS:
        hashed.u64(semantics["external_budget"][field])
    for part in ("opportunity", "actual"):
        hashed.u64(semantics[part]["route_queries"])
        hashed.u64(semantics[part]["route_work_units"])
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
        hashed.u64(semantics[field])
    hashed.byte(semantics["terminal_reason"])
    hashed.byte(semantics["candidate_outcome_source"])
    _hash_outcome(hashed, semantics["outcome"])
    hashed.u64(semantics["semantic_checksum"])


def _hash_candidate_artifact(hashed: StableHashBuilder, candidate: Mapping[str, Any]) -> None:
    hashed.u32(candidate["schema_major"])
    hashed.u32(candidate["schema_minor"])
    _hash_id(hashed, candidate["id"])
    _hash_entity(hashed, candidate["net"])
    for terminal in candidate["intended_terminals"]:
        _hash_entity(hashed, terminal)
    associations = candidate["associations"]
    hashed.u64(associations["board_content_hash"])
    hashed.u64(associations["compiler_profile_fingerprint"])
    hashed.u32(associations["geometry_compiler_version"])
    hashed.u64(associations["routing_profile_fingerprint"])
    hashed.u64(associations["rule_bucket_identity"])
    hashed.u32(candidate["geometry_schema_version"])
    hashed.u32(candidate["resource_schema_version"])
    policy = candidate["policy"]
    hashed.u32(policy["schema_version"])
    hashed.byte(policy["objective"])
    hashed.u64(policy["deterministic_seed"])
    hashed.u32(policy["candidate_ordinal"])
    hashed.u64(policy["orthogonal_step_surcharge"])
    hashed.u64(policy["diagonal_step_surcharge"])
    hashed.u64(policy["bend_surcharge"])
    hashed.u64(len(policy["banned_resources"]))
    for resource in policy["banned_resources"]:
        _hash_resource(hashed, resource)
    hashed.u64(len(policy["resource_penalties"]))
    for penalty in policy["resource_penalties"]:
        _hash_resource(hashed, penalty["resource"])
        hashed.u64(penalty["additional_cost"])
    hashed.u64(candidate["policy_identity"])
    provenance = candidate["provenance"]
    hashed.byte(provenance["generator"])
    hashed.u32(provenance["generator_version"])
    hashed.byte(provenance["backend"])
    hashed.string(provenance["supported_device_class"])
    hashed.u64(provenance["deterministic_seed"])
    hashed.u64(provenance["batch_identity"])
    hashed.u64(provenance["query_identity"])
    hashed.u32(provenance["candidate_ordinal"])
    hashed.u64(len(candidate["geometry"]))
    for primitive in candidate["geometry"]:
        hashed.byte(0 if primitive["kind"] == "line" else 1)
        if primitive["kind"] == "line":
            hashed.u32(primitive["layer"])
            for point in (primitive["start"], primitive["end"]):
                _hash_i64(hashed, point["x"])
                _hash_i64(hashed, point["y"])
        else:
            hashed.u64(primitive["template_id"])
            _hash_i64(hashed, primitive["position"]["x"])
            _hash_i64(hashed, primitive["position"]["y"])
            hashed.u32(primitive["start_layer"])
            hashed.u32(primitive["end_layer"])
    for field in _METRICS_FIELDS:
        hashed.u64(candidate["metrics"][field])
    constraints = candidate["constraints"]
    hashed.boolean(constraints["supported_hard_constraints_satisfied"])
    hashed.boolean(constraints["unsupported_rules_remain"])
    hashed.u32(constraints["connected_intended_terminal_count"])
    hashed.byte(constraints["exact_validation_code"])
    _hash_id(hashed, candidate["geometry_signature"])
    _hash_id(hashed, candidate["resource_signature"])
    hashed.u64(candidate["payload_checksum"])
    hashed.u64(candidate["logical_bytes"])
    hashed.u64(candidate["intrinsic_cost"])
    hashed.u64(len(candidate["resource_spans"]))
    for span in candidate["resource_spans"]:
        hashed.u32(span["layer"])
        _hash_i64(hashed, span["lattice_x"])
        _hash_i64(hashed, span["lattice_y"])
        hashed.byte(span["direction"])
        hashed.u32(span["edge_count"])
        hashed.u32(span["usage_units"])


def compute_snapshot_artifact_checksum(snapshot: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("apgar.phase4.exact_small_snapshot.artifact.v1")
    hashed.u32(snapshot["schema_version"])
    hashed.boolean(snapshot["decision_eligible"])
    config = snapshot["config"]
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
    hashed.byte(snapshot["execution_order"])
    for field in (
        "root_seed",
        "corpus_checksum",
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
    ):
        hashed.u64(snapshot[field])
    hashed.u64(len(snapshot["workload_roster"]))
    for net in snapshot["workload_roster"]:
        _hash_entity(hashed, net)
    for field in (
        "workload_net_roster_checksum",
        "budget_checksum",
        "raw_cell_plan_checksum",
        "raw_cell_artifact_checksum",
        "raw_source_envelope_checksum",
    ):
        hashed.u64(snapshot[field])
    reference = snapshot["raw_reference"]
    hashed.u32(reference["repetition_index"])
    hashed.byte(reference["execution_order"])
    for field in _RAW_REFERENCE_FIELDS[2:]:
        hashed.u64(reference[field])
    hashed.u64(snapshot["per_net_report_artifact_checksum"])
    hashed.u64(snapshot["per_net_report_source_envelope_checksum"])
    hashed.u64(snapshot["per_net_candidate_telemetry_checksum"])
    _hash_semantics(hashed, snapshot["candidate_semantics"])
    for field in (
        "candidate_semantic_checksum",
        "candidate_session_checksum",
        "final_pool_manifest_checksum",
        "final_rejection_manifest_checksum",
    ):
        hashed.u64(snapshot[field])
    hashed.byte(snapshot["production_outcome_source"])
    capacity = snapshot["capacity"]
    hashed.u32(capacity["schema_version"])
    associations = capacity["associations"]
    hashed.u64(associations["board_content_hash"])
    hashed.u64(associations["compiler_profile_fingerprint"])
    hashed.u32(associations["geometry_compiler_version"])
    hashed.u32(capacity["default_capacity_units"])
    hashed.u64(len(capacity["overrides"]))
    for override in capacity["overrides"]:
        _hash_resource(hashed, override["resource"])
        hashed.u32(override["capacity_units"])
    hashed.u64(snapshot["capacity_model_checksum"])
    hashed.u64(snapshot["cartesian_product"])
    hashed.u64(len(snapshot["pools"]))
    for pool in snapshot["pools"]:
        _hash_entity(hashed, pool["net"])
        hashed.u64(len(pool["candidates"]))
        for candidate in pool["candidates"]:
            _hash_candidate_artifact(hashed, candidate)
    hashed.u64(len(snapshot["production_selections"]))
    for selection in snapshot["production_selections"]:
        _hash_entity(hashed, selection["net"])
        hashed.byte(selection["status"])
        hashed.boolean(selection["candidate_id"] is not None)
        if selection["candidate_id"] is not None:
            _hash_id(hashed, selection["candidate_id"])
        hashed.boolean(selection["candidate_payload_checksum"] is not None)
        if selection["candidate_payload_checksum"] is not None:
            hashed.u64(selection["candidate_payload_checksum"])
        hashed.u64(selection["intrinsic_cost"])
    _hash_outcome(hashed, snapshot["production_outcome"])
    hashed.u64(snapshot["maximum_serialized_bytes"])
    return hashed.finish()


def compute_snapshot_source_envelope(snapshot: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("apgar.phase4.exact_small_snapshot.source_envelope.v1")
    hashed.string(snapshot["source_commit"])
    hashed.boolean(snapshot["source_stamped"])
    hashed.boolean(snapshot["source_tree_dirty"])
    hashed.u64(snapshot["artifact_checksum"])
    return hashed.finish()


def _bounded_add(total: int, value: int, maximum: int, label: str) -> int:
    if value > maximum or total > maximum - value:
        raise EvidenceError(f"snapshot aggregate {label} exceeds its frozen bound")
    return total + value


def _consume_budget(remaining: dict[str, int], name: str, value: int) -> None:
    if value < 0 or value > remaining[name]:
        raise EvidenceError(f"snapshot {name} traversal exceeds shape-only preflight")
    remaining[name] -= value


def _preflight_candidate_shapes(
    candidate_lists: Sequence[Sequence[Any]],
) -> dict[str, int]:
    totals = {
        "candidates": 0,
        "geometry_rows": 0,
        "span_rows": 0,
        "policy_rows": 0,
        "logical_bytes": 0,
        "expanded_edges": 0,
        "geometry_steps": 0,
    }
    for pool_index, candidates in enumerate(candidate_lists):
        for candidate_index, candidate_value in enumerate(candidates):
            label = f"snapshot.pools[{pool_index}].candidates[{candidate_index}]"
            candidate = _object(candidate_value, label)
            _fields(candidate, _CANDIDATE_FIELDS, label)
            totals["candidates"] = _bounded_add(
                totals["candidates"], 1, _MAXIMUM_CANDIDATES, "candidate rows"
            )
            policy = _object(candidate["policy"], f"{label}.policy")
            _fields(policy, _POLICY_FIELDS, f"{label}.policy")
            banned = _array(policy["banned_resources"], f"{label}.policy.banned_resources")
            penalties = _array(policy["resource_penalties"], f"{label}.policy.resource_penalties")
            totals["policy_rows"] = _bounded_add(
                totals["policy_rows"],
                len(banned) + len(penalties),
                _MAXIMUM_COMPONENT_ROWS,
                "policy rows",
            )
            geometry = _array(candidate["geometry"], f"{label}.geometry")
            spans = _array(candidate["resource_spans"], f"{label}.resource_spans")
            totals["geometry_rows"] = _bounded_add(
                totals["geometry_rows"],
                len(geometry),
                _MAXIMUM_COMPONENT_ROWS,
                "geometry rows",
            )
            totals["span_rows"] = _bounded_add(
                totals["span_rows"], len(spans), _MAXIMUM_COMPONENT_ROWS, "resource-span rows"
            )
            logical_bytes = _u64(candidate["logical_bytes"], f"{label}.logical_bytes")
            totals["logical_bytes"] = _bounded_add(
                totals["logical_bytes"],
                logical_bytes,
                _MAXIMUM_LOGICAL_BYTES,
                "declared logical bytes",
            )
            for primitive_index, primitive_value in enumerate(geometry):
                primitive_label = f"{label}.geometry[{primitive_index}]"
                primitive = _object(primitive_value, primitive_label)
                _fields(primitive, _LINE_FIELDS, primitive_label)
                if _string(primitive["kind"], f"{primitive_label}.kind") != "line":
                    raise EvidenceError(f"{primitive_label} is not a supported line")
                points = []
                for field in ("start", "end"):
                    point_label = f"{primitive_label}.{field}"
                    point = _object(primitive[field], point_label)
                    _fields(point, _POINT_FIELDS, point_label)
                    points.append(
                        (
                            _i64(point["x"], f"{point_label}.x"),
                            _i64(point["y"], f"{point_label}.y"),
                        )
                    )
                delta_x = points[1][0] - points[0][0]
                delta_y = points[1][1] - points[0][1]
                step_count = max(abs(delta_x), abs(delta_y))
                totals["geometry_steps"] = _bounded_add(
                    totals["geometry_steps"],
                    step_count,
                    _MAXIMUM_EXPANDED_EDGES,
                    "geometry steps",
                )
            for span_index, span_value in enumerate(spans):
                span_label = f"{label}.resource_spans[{span_index}]"
                span = _object(span_value, span_label)
                _fields(span, _SPAN_FIELDS, span_label)
                edge_count = _u32(span["edge_count"], f"{span_label}.edge_count")
                totals["expanded_edges"] = _bounded_add(
                    totals["expanded_edges"],
                    edge_count,
                    _MAXIMUM_EXPANDED_EDGES,
                    "expanded resource edges",
                )
    return totals


def preflight_cartesian_product(pool_sizes: Sequence[int]) -> int:
    """Validate the exact roster and stop at the first product excess."""
    if len(pool_sizes) != _POOL_COUNT:
        raise EvidenceError("exact-small Cartesian preflight requires exactly six pools")
    product = 1
    for size in pool_sizes:
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise EvidenceError("exact-small pool size must be an unsigned integer")
        product *= max(1, size)
        if product > _MAXIMUM_CARTESIAN_PRODUCT:
            raise EvidenceError("snapshot Cartesian product exceeds 4096")
    return product


def _parse_snapshot(snapshot_value: Any) -> Mapping[str, Any]:
    snapshot = _object(snapshot_value, "snapshot")
    _fields(snapshot, _TOP_FIELDS, "snapshot")
    if _u32(snapshot["schema_version"], "snapshot.schema_version") != 1:
        raise EvidenceError("snapshot.schema_version must be 1")
    if _boolean(snapshot["decision_eligible"], "snapshot.decision_eligible"):
        raise EvidenceError("snapshot must remain diagnostic-only")
    if _enum(snapshot["execution_order"], {0, 1}, "snapshot.execution_order") != 0:
        raise EvidenceError("snapshot must be baseline-first")
    config = report_validator.validate_config(snapshot["config"])
    if config["case_id"] not in {100, 101, 102} or config["requested_pool_size"] != 4:
        raise EvidenceError("snapshot must be an exact case 100/101/102 at pool four")
    for field in (
        "root_seed",
        "corpus_checksum",
        "descriptor_fingerprint",
        "case_checksum",
        "board_content_hash",
        "workload_checksum",
        "workload_net_roster_checksum",
        "budget_checksum",
        "raw_cell_plan_checksum",
        "raw_cell_artifact_checksum",
        "raw_source_envelope_checksum",
        "per_net_report_artifact_checksum",
        "per_net_report_source_envelope_checksum",
        "per_net_candidate_telemetry_checksum",
        "candidate_semantic_checksum",
        "candidate_session_checksum",
        "final_pool_manifest_checksum",
        "final_rejection_manifest_checksum",
        "capacity_model_checksum",
        "artifact_checksum",
    ):
        if _u64(snapshot[field], f"snapshot.{field}") == 0:
            raise EvidenceError(f"snapshot.{field} must be nonzero")
    commit = _string(snapshot["source_commit"], "snapshot.source_commit")
    if _COMMIT.fullmatch(commit) is None:
        raise EvidenceError("snapshot.source_commit is not a full lowercase commit")
    if not _boolean(snapshot["source_stamped"], "snapshot.source_stamped") or _boolean(
        snapshot["source_tree_dirty"], "snapshot.source_tree_dirty"
    ):
        raise EvidenceError("snapshot source must be clean and stamped")
    _u64(snapshot["source_envelope_checksum"], "snapshot.source_envelope_checksum")
    roster = _array(snapshot["workload_roster"], "snapshot.workload_roster")
    if len(roster) != _POOL_COUNT:
        raise EvidenceError("snapshot.workload_roster must contain exactly six nets")
    parsed_roster = [
        _entity(item, f"snapshot.workload_roster[{index}]") for index, item in enumerate(roster)
    ]
    if [_entity_key(net) for net in parsed_roster] != sorted(
        set(_entity_key(net) for net in parsed_roster)
    ):
        raise EvidenceError("snapshot.workload_roster must be strictly canonical")
    reference = _object(snapshot["raw_reference"], "snapshot.raw_reference")
    _fields(reference, _RAW_REFERENCE_FIELDS, "snapshot.raw_reference")
    if (
        _u32(reference["repetition_index"], "snapshot.raw_reference.repetition_index") != 0
        or _enum(reference["execution_order"], {0, 1}, "snapshot.raw_reference.execution_order")
        != 0
    ):
        raise EvidenceError("snapshot raw reference must be repetition-zero baseline-first")
    for field in _RAW_REFERENCE_FIELDS[2:]:
        if _u64(reference[field], f"snapshot.raw_reference.{field}") == 0:
            raise EvidenceError(f"snapshot.raw_reference.{field} must be nonzero")
    semantics = report_validator.validate_semantics(
        snapshot["candidate_semantics"], "snapshot.candidate_semantics"
    )
    _enum(snapshot["production_outcome_source"], {1, 2}, "snapshot.production_outcome_source")
    capacity = _object(snapshot["capacity"], "snapshot.capacity")
    _fields(capacity, _CAPACITY_FIELDS, "snapshot.capacity")
    if _u32(capacity["schema_version"], "snapshot.capacity.schema_version") != 1:
        raise EvidenceError("snapshot.capacity.schema_version must be 1")
    capacity_associations = _object(capacity["associations"], "snapshot.capacity.associations")
    _fields(capacity_associations, _CAPACITY_ASSOCIATION_FIELDS, "snapshot.capacity.associations")
    for field in _CAPACITY_ASSOCIATION_FIELDS:
        parser = _u32 if field == "geometry_compiler_version" else _u64
        if parser(capacity_associations[field], f"snapshot.capacity.associations.{field}") == 0:
            raise EvidenceError(f"snapshot.capacity.associations.{field} must be nonzero")
    default_capacity = _u32(
        capacity["default_capacity_units"], "snapshot.capacity.default_capacity_units"
    )
    if default_capacity > 1:
        raise EvidenceError("snapshot exact capacity must be binary")
    overrides = _array(capacity["overrides"], "snapshot.capacity.overrides")
    if len(overrides) > _MAXIMUM_CAPACITY_OVERRIDES:
        raise EvidenceError("snapshot capacity override count exceeds the frozen bound")
    override_keys: list[tuple[int, int, int, int]] = []
    for index, item in enumerate(overrides):
        label = f"snapshot.capacity.overrides[{index}]"
        override = _object(item, label)
        _fields(override, _OVERRIDE_FIELDS, label)
        resource = _resource(override["resource"], f"{label}.resource")
        if _u32(override["capacity_units"], f"{label}.capacity_units") > 1:
            raise EvidenceError(f"{label}.capacity_units must be binary")
        override_keys.append(_resource_key(resource))
    if override_keys != sorted(set(override_keys)):
        raise EvidenceError("snapshot capacity overrides must be strictly canonical")
    pools = _array(snapshot["pools"], "snapshot.pools")
    if len(pools) != _POOL_COUNT:
        raise EvidenceError("snapshot must contain exactly six explicit pools")
    parsed_pool_shapes: list[tuple[Mapping[str, Any], Mapping[str, Any], Sequence[Any]]] = []
    pool_sizes: list[int] = []
    for pool_index, item in enumerate(pools):
        label = f"snapshot.pools[{pool_index}]"
        pool = _object(item, label)
        _fields(pool, _POOL_FIELDS, label)
        net = _entity(pool["net"], f"{label}.net")
        if net != parsed_roster[pool_index]:
            raise EvidenceError(f"{label}.net differs from the workload roster")
        candidates = _array(pool["candidates"], f"{label}.candidates")
        if len(candidates) > _MAXIMUM_CANDIDATES_PER_POOL:
            raise EvidenceError(f"{label} exceeds the candidate-per-pool bound")
        parsed_pool_shapes.append((pool, net, candidates))
        pool_sizes.append(len(candidates))
    product = preflight_cartesian_product(pool_sizes)
    remaining = _preflight_candidate_shapes([candidates for _, _, candidates in parsed_pool_shapes])
    candidate_count = geometry_count = span_count = policy_count = expanded_count = logical = 0
    candidate_ids: set[tuple[int, int]] = set()
    for pool_index, (pool, net, candidates) in enumerate(parsed_pool_shapes):
        label = f"snapshot.pools[{pool_index}]"
        previous_id: tuple[int, int] | None = None
        for candidate_index, candidate_value in enumerate(candidates):
            candidate = _parse_candidate(
                candidate_value,
                f"{label}.candidates[{candidate_index}]",
                net,
                snapshot,
                remaining,
            )
            identity = _id_key(candidate["id"])
            if previous_id is not None and previous_id >= identity:
                raise EvidenceError(f"{label}.candidates are not in strict ID order")
            if identity in candidate_ids:
                raise EvidenceError("snapshot candidate IDs must be globally unique")
            candidate_ids.add(identity)
            previous_id = identity
            candidate_count += 1
            geometry_count += len(candidate["geometry"])
            span_count += len(candidate["resource_spans"])
            policy_count += len(candidate["policy"]["banned_resources"]) + len(
                candidate["policy"]["resource_penalties"]
            )
            expanded_count += sum(span["edge_count"] for span in candidate["resource_spans"])
            logical += candidate["logical_bytes"]
            if (
                candidate_count > _MAXIMUM_CANDIDATES
                or geometry_count > _MAXIMUM_COMPONENT_ROWS
                or span_count > _MAXIMUM_COMPONENT_ROWS
                or policy_count > _MAXIMUM_COMPONENT_ROWS
                or expanded_count > _MAXIMUM_EXPANDED_EDGES
                or logical > _MAXIMUM_LOGICAL_BYTES
            ):
                raise EvidenceError("snapshot aggregate candidate shape exceeds a frozen bound")
    if any(remaining.values()):
        raise EvidenceError("snapshot candidate traversal did not consume shape-only preflight")
    if _u64(snapshot["cartesian_product"], "snapshot.cartesian_product") != product:
        raise EvidenceError("snapshot.cartesian_product does not match complete pools")
    if _pools_manifest(pools) != snapshot["final_pool_manifest_checksum"]:
        raise EvidenceError("snapshot final-pool manifest is invalid")
    selections = _array(snapshot["production_selections"], "snapshot.production_selections")
    if len(selections) != _POOL_COUNT:
        raise EvidenceError("snapshot must contain exactly six production selections")
    for index, item in enumerate(selections):
        label = f"snapshot.production_selections[{index}]"
        selection = _object(item, label)
        _fields(selection, _SELECTION_FIELDS, label)
        if _entity(selection["net"], f"{label}.net") != parsed_roster[index]:
            raise EvidenceError(f"{label}.net differs from workload roster")
        status = _enum(selection["status"], {0, 1}, f"{label}.status")
        intrinsic = _u64(selection["intrinsic_cost"], f"{label}.intrinsic_cost")
        if status == 0:
            identity = _hash128(selection["candidate_id"], f"{label}.candidate_id")
            checksum = _u64(
                selection["candidate_payload_checksum"], f"{label}.candidate_payload_checksum"
            )
            found = next(
                (
                    candidate
                    for candidate in pools[index]["candidates"]
                    if candidate["id"] == identity
                ),
                None,
            )
            if (
                found is None
                or found["payload_checksum"] != checksum
                or found["metrics"]["intrinsic_base_cost"] != intrinsic
            ):
                raise EvidenceError(f"{label} is not an exact member of its pool")
        elif (
            selection["candidate_id"] is not None
            or selection["candidate_payload_checksum"] is not None
            or intrinsic != 0
            or pools[index]["candidates"]
        ):
            raise EvidenceError(f"{label} no-candidate encoding is noncanonical")
    outcome = _object(snapshot["production_outcome"], "snapshot.production_outcome")
    _fields(outcome, _OUTCOME_FIELDS, "snapshot.production_outcome")
    for field in _OUTCOME_FIELDS:
        _u64(outcome[field], f"snapshot.production_outcome.{field}")
    if outcome["world_checksum"] == 0:
        raise EvidenceError("snapshot production world checksum must be nonzero")
    if (
        _u64(snapshot["maximum_serialized_bytes"], "snapshot.maximum_serialized_bytes")
        != _MAXIMUM_SERIALIZED_BYTES
    ):
        raise EvidenceError("snapshot.maximum_serialized_bytes differs from the v1 bound")
    if _capacity_checksum(snapshot) != snapshot["capacity_model_checksum"]:
        raise EvidenceError("snapshot capacity-model checksum is invalid")
    if semantics["final_candidate_count"] != candidate_count:
        raise EvidenceError("snapshot candidate count differs from candidate semantics")
    if snapshot["artifact_checksum"] != compute_snapshot_artifact_checksum(snapshot):
        raise EvidenceError("snapshot artifact checksum is invalid")
    if snapshot["source_envelope_checksum"] != compute_snapshot_source_envelope(snapshot):
        raise EvidenceError("snapshot source envelope is invalid")
    return snapshot


def _admission_resource(encoder: _AdmissionEncoder, resource: Mapping[str, Any]) -> None:
    encoder.u32(resource["layer"])
    encoder.i64(resource["lattice_x"])
    encoder.i64(resource["lattice_y"])
    encoder.u8(resource["direction"])


def _admission_entity(encoder: _AdmissionEncoder, entity: Mapping[str, Any]) -> None:
    encoder.u64(entity["id"])
    encoder.u32(entity["generation"])


def _admission_id(encoder: _AdmissionEncoder, identity: Mapping[str, Any]) -> None:
    encoder.u64(identity["high"])
    encoder.u64(identity["low"])


def _candidate_admission_payload(snapshot: Mapping[str, Any]) -> bytes:
    encoder = _AdmissionEncoder()
    encoder.u32(1)
    encoder.u32(snapshot["config"]["case_id"])
    for field in _LIMIT_FIELDS:
        encoder.u64(snapshot["config"]["corpus_limits"][field])
    encoder.u32(len(snapshot["pools"]))
    for pool in snapshot["pools"]:
        _admission_entity(encoder, pool["net"])
        encoder.u32(len(pool["candidates"]))
        for candidate in pool["candidates"]:
            encoder.u16(candidate["schema_major"])
            encoder.u16(candidate["schema_minor"])
            _admission_id(encoder, candidate["id"])
            _admission_entity(encoder, candidate["net"])
            for terminal in candidate["intended_terminals"]:
                _admission_entity(encoder, terminal)
            associations = candidate["associations"]
            encoder.u64(associations["board_content_hash"])
            encoder.u64(associations["compiler_profile_fingerprint"])
            encoder.u32(associations["geometry_compiler_version"])
            encoder.u64(associations["routing_profile_fingerprint"])
            encoder.u64(associations["rule_bucket_identity"])
            encoder.u32(candidate["geometry_schema_version"])
            encoder.u32(candidate["resource_schema_version"])
            policy = candidate["policy"]
            encoder.u32(policy["schema_version"])
            encoder.u8(policy["objective"])
            encoder.u64(policy["deterministic_seed"])
            encoder.u32(policy["candidate_ordinal"])
            encoder.u64(policy["orthogonal_step_surcharge"])
            encoder.u64(policy["diagonal_step_surcharge"])
            encoder.u64(policy["bend_surcharge"])
            encoder.u32(len(policy["banned_resources"]))
            for resource in policy["banned_resources"]:
                _admission_resource(encoder, resource)
            encoder.u32(len(policy["resource_penalties"]))
            for penalty in policy["resource_penalties"]:
                _admission_resource(encoder, penalty["resource"])
                encoder.u64(penalty["additional_cost"])
            encoder.u64(candidate["policy_identity"])
            provenance = candidate["provenance"]
            encoder.u8(provenance["generator"])
            encoder.u32(provenance["generator_version"])
            encoder.u8(provenance["backend"])
            encoder.string(provenance["supported_device_class"])
            encoder.u64(provenance["deterministic_seed"])
            encoder.u64(provenance["batch_identity"])
            encoder.u64(provenance["query_identity"])
            encoder.u32(provenance["candidate_ordinal"])
            encoder.u32(len(candidate["geometry"]))
            for primitive in candidate["geometry"]:
                encoder.u8(0)
                encoder.u32(primitive["layer"])
                encoder.i64(primitive["start"]["x"])
                encoder.i64(primitive["start"]["y"])
                encoder.i64(primitive["end"]["x"])
                encoder.i64(primitive["end"]["y"])
            encoder.u32(len(candidate["resource_spans"]))
            for span in candidate["resource_spans"]:
                _admission_resource(encoder, span)
                encoder.u32(span["edge_count"])
                encoder.u32(span["usage_units"])
            for field in _METRICS_FIELDS:
                encoder.u64(candidate["metrics"][field])
            constraints = candidate["constraints"]
            encoder.u8(int(constraints["supported_hard_constraints_satisfied"]))
            encoder.u8(int(constraints["unsupported_rules_remain"]))
            encoder.u32(constraints["connected_intended_terminal_count"])
            encoder.u8(constraints["exact_validation_code"])
            _admission_id(encoder, candidate["geometry_signature"])
            _admission_id(encoder, candidate["resource_signature"])
            encoder.u64(candidate["payload_checksum"])
            encoder.u64(candidate["logical_bytes"])
            encoder.u64(candidate["intrinsic_cost"])
    if len(encoder.data) > _MAXIMUM_SNAPSHOT_BYTES:
        raise EvidenceError("candidate admission replay payload exceeds the 64 MiB bound")
    return bytes(encoder.data)


def _admission_replay_path() -> pathlib.Path:
    try:
        resolver = bazel_runfiles.Create()
    except (OSError, UnicodeError, ValueError) as error:
        raise EvidenceError("cannot resolve exact candidate admission replay runfiles") from error
    if resolver is not None:
        resolved = resolver.Rlocation("_main/phase4_exact_small_candidate_admission_replay")
        if resolved is not None:
            candidate = pathlib.Path(resolved)
            if candidate.is_file() and os.access(candidate, os.X_OK):
                return candidate
    try:
        launcher = pathlib.Path(sys.argv[0]).resolve(strict=True)
    except OSError:
        launcher = None
    if launcher is not None:
        candidate = (
            pathlib.Path(f"{launcher}.runfiles")
            / "_main"
            / "phase4_exact_small_candidate_admission_replay"
        )
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    runfiles = os.environ.get("RUNFILES_DIR") or os.environ.get("TEST_SRCDIR")
    workspace = os.environ.get("TEST_WORKSPACE") or "_main"
    if runfiles:
        candidate = (
            pathlib.Path(runfiles) / workspace / "phase4_exact_small_candidate_admission_replay"
        )
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    raise EvidenceError(
        "exact candidate admission replay is unavailable; build the validator with Bazel"
    )


def _replay_exact_candidate_admission(snapshot: Mapping[str, Any]) -> None:
    try:
        completed = subprocess.run(
            [_admission_replay_path()],
            input=_candidate_admission_payload(snapshot),
            capture_output=True,
            check=False,
            timeout=30,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        raise EvidenceError(f"exact candidate admission replay could not run: {error}") from error
    if completed.returncode != 0:
        diagnostic = completed.stderr.decode("utf-8", errors="replace").strip()
        if len(diagnostic) > 1024:
            diagnostic = diagnostic[:1024]
        raise EvidenceError(f"exact candidate admission replay failed: {diagnostic}")
    if completed.stdout:
        raise EvidenceError("exact candidate admission replay emitted unexpected stdout")


def _expand_candidate(candidate: Mapping[str, Any]) -> set[tuple[int, int, int, int]]:
    resources: set[tuple[int, int, int, int]] = set()
    for span in candidate["resource_spans"]:
        dx, dy = ((1, 0), (1, 1), (0, 1), (1, -1))[span["direction"]]
        for offset in range(span["edge_count"]):
            resources.add(
                (
                    span["layer"],
                    span["lattice_x"] + dx * offset,
                    span["lattice_y"] + dy * offset,
                    span["direction"],
                )
            )
    return resources


def _score(
    choices: Sequence[Mapping[str, Any] | None],
    default_capacity: int,
    overrides: Mapping[tuple[int, int, int, int], int],
) -> tuple[tuple[int, int, int], int]:
    usage: dict[tuple[int, int, int, int], int] = {}
    selected = 0
    cost = 0
    for candidate in choices:
        if candidate is None:
            continue
        selected += 1
        cost += candidate["metrics"]["intrinsic_base_cost"]
        for resource in _expand_candidate(candidate):
            usage[resource] = usage.get(resource, 0) + 1
    overuses = [
        units - overrides.get(resource, default_capacity)
        for resource, units in usage.items()
        if units > overrides.get(resource, default_capacity)
    ]
    return (selected, sum(overuses), cost), len(overuses)


def _objective_key(objective: tuple[int, int, int]) -> tuple[int, int, int]:
    return -objective[0], objective[1], objective[2]


def _objective_document(objective: tuple[int, int, int]) -> dict[str, int]:
    return dict(zip(_OBJECTIVE_FIELDS, objective, strict=True))


def _witness_key(choices: Sequence[Mapping[str, Any] | None]) -> tuple[tuple[int, int], ...]:
    return tuple(
        (-1, -1) if candidate is None else _id_key(candidate["id"]) for candidate in choices
    )


def enumerate_exact_oracle(
    snapshot: Mapping[str, Any],
) -> tuple[tuple[int, int, int], int, tuple[Mapping[str, Any] | None, ...]]:
    """Exhaustively solve one already validated frozen-pool snapshot."""
    capacity = snapshot["capacity"]
    overrides = {
        _resource_key(override["resource"]): override["capacity_units"]
        for override in capacity["overrides"]
    }
    alternatives: list[Sequence[Mapping[str, Any] | None]] = [
        pool["candidates"] if pool["candidates"] else [None] for pool in snapshot["pools"]
    ]
    optimum: tuple[int, int, int] | None = None
    count = 0
    witness: tuple[Mapping[str, Any] | None, ...] | None = None
    for raw_choices in itertools.product(*alternatives):
        choices = tuple(raw_choices)
        objective, _ = _score(choices, capacity["default_capacity_units"], overrides)
        if optimum is None or _objective_key(objective) < _objective_key(optimum):
            optimum = objective
            count = 1
            witness = choices
        elif objective == optimum:
            count += 1
            if witness is None or _witness_key(choices) < _witness_key(witness):
                witness = choices
    if optimum is None or witness is None or count == 0:
        raise EvidenceError("exact oracle unexpectedly enumerated no worlds")
    return optimum, count, witness


def _production_choices(snapshot: Mapping[str, Any]) -> tuple[Mapping[str, Any] | None, ...]:
    choices: list[Mapping[str, Any] | None] = []
    for pool, selection in zip(snapshot["pools"], snapshot["production_selections"], strict=True):
        if selection["status"] == 1:
            choices.append(None)
            continue
        choices.append(
            next(
                candidate
                for candidate in pool["candidates"]
                if candidate["id"] == selection["candidate_id"]
            )
        )
    return tuple(choices)


def _oracle_artifact_checksum(artifact: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-EXACT-SMALL-ORACLE-ARTIFACT-V1")
    hashed.u32(artifact["schema_version"])
    hashed.boolean(artifact["decision_eligible"])
    hashed.u32(artifact["case_id"])
    for field in (
        "raw_artifact_checksum",
        "per_net_report_artifact_checksum",
        "snapshot_artifact_checksum",
        "candidate_semantic_checksum",
        "cartesian_product",
    ):
        hashed.u64(artifact[field])
    for name in ("production_objective", "optimum_objective"):
        for field in _OBJECTIVE_FIELDS:
            hashed.u64(artifact[name][field])
    hashed.u64(artifact["production_overused_resource_count"])
    hashed.u64(artifact["canonical_witness_overused_resource_count"])
    hashed.boolean(artifact["production_is_optimal"])
    hashed.u64(artifact["optimum_count"])
    hashed.u64(len(artifact["canonical_witness"]))
    for row in artifact["canonical_witness"]:
        _hash_entity(hashed, row["net"])
        hashed.boolean(row["candidate_id"] is not None)
        if row["candidate_id"] is not None:
            _hash_id(hashed, row["candidate_id"])
    return hashed.finish()


def _oracle_source_envelope(artifact: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-EXACT-SMALL-ORACLE-SOURCE-ENVELOPE-V1")
    hashed.string(artifact["source_commit"])
    hashed.boolean(artifact["source_stamped"])
    hashed.boolean(artifact["source_tree_dirty"])
    hashed.u64(artifact["artifact_checksum"])
    return hashed.finish()


def validate_publication(
    raw_value: Any, report_value: Any, snapshot_value: Any, *, expected_commit: str
) -> Mapping[str, Any]:
    """Validate all three inputs and return one canonical oracle DTO."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise EvidenceError("publication requires an independently supplied expected commit")
    report_validator.validate_join(raw_value, report_value, expected_commit=expected_commit)
    return validate_publication_against_validated_authorities(
        raw_value,
        report_value,
        snapshot_value,
        expected_commit=expected_commit,
    )


def validate_publication_against_validated_authorities(
    raw_value: Any, report_value: Any, snapshot_value: Any, *, expected_commit: str
) -> Mapping[str, Any]:
    """Build the oracle after the caller validates its versioned Raw/report authority."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise EvidenceError("publication requires an independently supplied expected commit")
    raw = _object(raw_value, "raw cell")
    report = _object(report_value, "per-net report")
    snapshot = _parse_snapshot(snapshot_value)
    if (
        snapshot["source_commit"] != expected_commit
        or snapshot["source_commit"] != raw["source_commit"]
        or snapshot["source_commit"] != report["source_commit"]
        or snapshot["source_stamped"] != raw["source_stamped"]
        or snapshot["source_stamped"] != report["source_stamped"]
        or snapshot["source_tree_dirty"] != raw["source_tree_dirty"]
        or snapshot["source_tree_dirty"] != report["source_tree_dirty"]
    ):
        raise EvidenceError("snapshot source does not join the validated Raw/report source")
    if snapshot["config"] != raw["config"] or snapshot["config"] != report["config"]:
        raise EvidenceError("snapshot config does not join Raw and per-net report")
    for snapshot_field, raw_field in (
        ("corpus_checksum", "corpus_checksum"),
        ("raw_cell_plan_checksum", "cell_plan_checksum"),
        ("raw_cell_artifact_checksum", "artifact_checksum"),
        ("raw_source_envelope_checksum", "source_envelope_checksum"),
    ):
        if snapshot[snapshot_field] != raw[raw_field]:
            raise EvidenceError(f"snapshot.{snapshot_field} does not join the Raw cell")
    if (
        snapshot["per_net_report_artifact_checksum"] != report["artifact_checksum"]
        or snapshot["per_net_report_source_envelope_checksum"] != report["source_envelope_checksum"]
        or snapshot["workload_net_roster_checksum"] != report["workload_net_roster_checksum"]
    ):
        raise EvidenceError("snapshot does not join the complete per-net report authority")
    raw_attempt = raw["attempts"][0]
    expected_reference = {
        "repetition_index": 0,
        "execution_order": 0,
        "pair_attempt_checksum": raw_attempt["attempt_checksum"],
        "paired_semantic_checksum": raw_attempt["result"]["semantic_checksum"],
        "paired_artifact_checksum": raw_attempt["result"]["artifact_checksum"],
        "baseline_semantic_checksum": raw_attempt["baseline"]["record"]["semantics"][
            "semantic_checksum"
        ],
        "baseline_arm_artifact_checksum": raw_attempt["baseline"]["record"]["artifact_checksum"],
        "candidate_semantic_checksum": raw_attempt["candidate"]["record"]["semantics"][
            "semantic_checksum"
        ],
        "candidate_arm_artifact_checksum": raw_attempt["candidate"]["record"]["artifact_checksum"],
    }
    if (
        snapshot["raw_reference"] != expected_reference
        or snapshot["raw_reference"] != report["raw_reference"]
    ):
        raise EvidenceError("snapshot raw reference does not exactly join repetition zero")
    raw_candidate_semantics = raw_attempt["candidate"]["record"]["semantics"]
    report_candidate = report["arms"][1]["diagnostic"]
    if (
        snapshot["candidate_semantics"] != raw_candidate_semantics
        or snapshot["candidate_semantics"] != report_candidate["semantics"]
        or snapshot["candidate_semantic_checksum"] != raw_candidate_semantics["semantic_checksum"]
        or snapshot["per_net_candidate_telemetry_checksum"]
        != report_candidate["telemetry"]["telemetry_checksum"]
    ):
        raise EvidenceError("snapshot candidate semantics/telemetry do not join Raw/report")
    semantics = snapshot["candidate_semantics"]
    projection_pairs = (
        ("root_seed", "root_seed"),
        ("corpus_checksum", "corpus_checksum"),
        ("descriptor_fingerprint", "descriptor_fingerprint"),
        ("case_checksum", "case_checksum"),
        ("board_content_hash", "board_content_hash"),
        ("workload_checksum", "workload_checksum"),
        ("capacity_model_checksum", "capacity_model_checksum"),
        ("budget_checksum", "budget_checksum"),
        ("algorithm_session_checksum", "candidate_session_checksum"),
        ("final_pool_manifest_checksum", "final_pool_manifest_checksum"),
        ("final_rejection_manifest_checksum", "final_rejection_manifest_checksum"),
        ("candidate_outcome_source", "production_outcome_source"),
    )
    for semantics_field, snapshot_field in projection_pairs:
        if semantics[semantics_field] != snapshot[snapshot_field]:
            raise EvidenceError(f"snapshot.{snapshot_field} differs from complete semantics")
    if semantics["outcome"] != snapshot["production_outcome"]:
        raise EvidenceError("snapshot production outcome differs from complete semantics")
    report_nets = [row["net"] for row in report_candidate["telemetry"]["per_net"]]
    if snapshot["workload_roster"] != report_nets:
        raise EvidenceError("snapshot workload roster differs from per-net telemetry")
    for index, telemetry in enumerate(report_candidate["telemetry"]["per_net"]):
        pool = snapshot["pools"][index]
        selection = snapshot["production_selections"][index]
        if telemetry["final_pool_size"] != len(pool["candidates"]):
            raise EvidenceError("snapshot pool size differs from per-net candidate telemetry")
        if (
            telemetry["selected_status"] != selection["status"]
            or telemetry["selected_candidate_id"] != selection["candidate_id"]
            or telemetry["selected_candidate_payload_checksum"]
            != selection["candidate_payload_checksum"]
        ):
            raise EvidenceError("snapshot selection differs from per-net candidate telemetry")
        if selection["status"] == 0:
            selected = next(
                candidate
                for candidate in pool["candidates"]
                if candidate["id"] == selection["candidate_id"]
            )
            if (
                telemetry["selected_candidate_metrics"] != selected["metrics"]
                or selection["intrinsic_cost"] != selected["metrics"]["intrinsic_base_cost"]
            ):
                raise EvidenceError(
                    "snapshot selected metrics differ from complete per-net candidate telemetry"
                )
        elif (
            telemetry["selected_candidate_metrics"] is not None or selection["intrinsic_cost"] != 0
        ):
            raise EvidenceError("snapshot no-candidate telemetry uses non-null selected fields")
    _replay_exact_candidate_admission(snapshot)
    optimum, optimum_count, witness = enumerate_exact_oracle(snapshot)
    capacity = snapshot["capacity"]
    override_map = {
        _resource_key(override["resource"]): override["capacity_units"]
        for override in capacity["overrides"]
    }
    production, production_overused_resource_count = _score(
        _production_choices(snapshot), capacity["default_capacity_units"], override_map
    )
    expected_outcome = snapshot["production_outcome"]
    if (
        production
        != (
            expected_outcome["selected_net_count"],
            expected_outcome["total_overuse_units"],
            expected_outcome["total_intrinsic_cost"],
        )
        or production_overused_resource_count != expected_outcome["overused_resource_count"]
    ):
        raise EvidenceError("production selections do not reproduce the serialized outcome")
    if production != optimum:
        raise EvidenceError("production frozen-pool objective is not exact-optimal")
    _, witness_overused_resource_count = _score(
        witness, capacity["default_capacity_units"], override_map
    )
    witness_rows = [
        {
            "net": snapshot["pools"][index]["net"],
            "candidate_id": None if candidate is None else candidate["id"],
        }
        for index, candidate in enumerate(witness)
    ]
    artifact: dict[str, Any] = {
        "source_commit": expected_commit,
        "source_stamped": True,
        "source_tree_dirty": False,
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "decision_eligible": False,
        "case_id": snapshot["config"]["case_id"],
        "raw_artifact_checksum": raw["artifact_checksum"],
        "per_net_report_artifact_checksum": report["artifact_checksum"],
        "snapshot_artifact_checksum": snapshot["artifact_checksum"],
        "candidate_semantic_checksum": snapshot["candidate_semantic_checksum"],
        "cartesian_product": snapshot["cartesian_product"],
        "production_objective": _objective_document(production),
        "optimum_objective": _objective_document(optimum),
        "production_overused_resource_count": production_overused_resource_count,
        "canonical_witness_overused_resource_count": witness_overused_resource_count,
        "production_is_optimal": True,
        "optimum_count": optimum_count,
        "canonical_witness": witness_rows,
        "artifact_checksum": 0,
    }
    artifact["artifact_checksum"] = _oracle_artifact_checksum(artifact)
    artifact["source_envelope_checksum"] = _oracle_source_envelope(artifact)
    return artifact


def serialize_oracle_artifact(artifact_value: Any) -> str:
    artifact = _object(artifact_value, "oracle artifact")
    _fields(artifact, _ORACLE_FIELDS, "oracle artifact")
    for name in ("production_objective", "optimum_objective"):
        objective = _object(artifact[name], f"oracle artifact.{name}")
        _fields(objective, _OBJECTIVE_FIELDS, f"oracle artifact.{name}")
    witness = _array(artifact["canonical_witness"], "oracle artifact.canonical_witness")
    for index, item in enumerate(witness):
        row = _object(item, f"oracle artifact.canonical_witness[{index}]")
        _fields(row, _WITNESS_FIELDS, f"oracle artifact.canonical_witness[{index}]")
    if artifact["artifact_checksum"] != _oracle_artifact_checksum(artifact):
        raise EvidenceError("oracle artifact checksum is invalid")
    if artifact["source_envelope_checksum"] != _oracle_source_envelope(artifact):
        raise EvidenceError("oracle source envelope is invalid")
    output = json.dumps(artifact, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
    if len(output.encode("utf-8")) > _MAXIMUM_OUTPUT_BYTES:
        raise EvidenceError("oracle output exceeds the 1 MiB bound")
    return output


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw = raw_validator.read_validated_publication_document(
            options.raw, expected_commit=options.expected_commit
        )
        report = report_validator.read_report_document(options.report)
        snapshot = read_snapshot_document(options.snapshot)
        artifact = validate_publication(
            raw, report, snapshot, expected_commit=options.expected_commit
        )
        output = serialize_oracle_artifact(artifact)
    except EvidenceError as error:
        print(f"Phase 4 exact-small oracle publication failed: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
