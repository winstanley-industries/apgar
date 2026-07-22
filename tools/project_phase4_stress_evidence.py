"""Build and rebuild-validate the Phase 4 stress-ladder evidence artifact."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import project_phase4_operational_evidence as operational_projector
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_MAXIMUM_PROBE_BYTES = 64 * 1024
_MAXIMUM_ARTIFACT_BYTES = 4 * 1024 * 1024
_MAXIMUM_NESTING_DEPTH = 64
_COMMIT = re.compile(r"[0-9a-f]{40}")
_PROBE_FIELDS = (
    "schema_version",
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "corpus_version",
    "corpus_checksum",
    "limits",
    "cases",
    "artifact_checksum",
    "source_envelope_checksum",
)
_LIMIT_FIELDS = (
    "maximum_nets",
    "maximum_compiled_nodes",
    "maximum_compiled_host_bytes",
    "maximum_active_regions",
    "maximum_board_entities",
)
_PROBE_CASE_FIELDS = (
    "case_id",
    "descriptor_fingerprint",
    "requested_net_count",
    "requested_pool_size",
    "declared_stress_target_nets",
    "build_status",
    "materialization_scope",
    "full_workload_materialized",
    "capacity_model_reached",
    "allocator_reached",
    "error_code",
    "invariant_id",
    "limiting_work_bound",
    "maximum_preparable_net_count",
    "first_unpreparable_net",
    "required_compiled_nodes",
    "configured_compiled_node_limit",
    "required_compiled_host_bytes",
    "configured_compiled_host_byte_limit",
    "per_net_compiled_nodes",
    "per_net_compiled_host_bytes",
)
_EXPECTED_WORK_BOUNDS = {
    3001: {
        "requested_net_count": 2048,
        "descriptor_fingerprint": 5505549972392664092,
        "maximum_preparable_net_count": 739,
        "first_unpreparable_net": {"id": 1739, "generation": 0},
        "required_compiled_nodes": 276879360,
        "required_compiled_host_bytes": 6941540352,
        "per_net_compiled_nodes": 135195,
        "per_net_compiled_host_bytes": 3389424,
    },
    3002: {
        "requested_net_count": 4096,
        "descriptor_fingerprint": 6801270323093200014,
        "maximum_preparable_net_count": 369,
        "first_unpreparable_net": {"id": 1369, "generation": 0},
        "required_compiled_nodes": 1107087360,
        "required_compiled_host_bytes": 27747614720,
        "per_net_compiled_nodes": 270285,
        "per_net_compiled_host_bytes": 6774320,
    },
}


class StressError(ValueError):
    """A stress authority or publication artifact is invalid."""


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise StressError(f"{label} must be an object")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value.keys()) != tuple(expected):
        raise StressError(f"{label} fields or field order are noncanonical")


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise StressError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _boolean(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise StressError(f"{label} must be a boolean")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise StressError(f"{label} must be a string")
    return value


def _reject_duplicate_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise StressError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_non_json_constant(value: str) -> Any:
    raise StressError(f"non-JSON numeric constant: {value}")


def _check_nesting(value: Any, label: str) -> None:
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > _MAXIMUM_NESTING_DEPTH:
            raise StressError(f"{label} exceeds the nesting-depth bound")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def _read_canonical(path: pathlib.Path, label: str, maximum: int) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(maximum + 1)
        if len(encoded) > maximum:
            raise StressError(f"{label} exceeds the {maximum}-byte input bound")
        text = encoded.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_nesting(value, label)
        canonical = (
            json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
        )
        if text != canonical:
            raise StressError(f"{label} must be canonical one-line JSON plus LF")
        if not isinstance(value, dict):
            raise StressError(f"{label} must be an object")
        return value
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        if isinstance(error, StressError):
            raise
        raise StressError(f"cannot read {label} {path}: {error}") from error


def compute_probe_artifact_checksum(probe: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-ARTIFACT-V1")
    hashed.u32(probe["schema_version"])
    hashed.string(probe["source_commit"])
    hashed.boolean(probe["source_stamped"])
    hashed.boolean(probe["source_tree_dirty"])
    hashed.u32(probe["corpus_version"])
    hashed.u64(probe["corpus_checksum"])
    for field in _LIMIT_FIELDS:
        hashed.u64(probe["limits"][field])
    hashed.u64(len(probe["cases"]))
    for row in probe["cases"]:
        hashed.u32(row["case_id"])
        hashed.u64(row["descriptor_fingerprint"])
        hashed.u32(row["requested_net_count"])
        hashed.u32(row["requested_pool_size"])
        hashed.u32(row["declared_stress_target_nets"])
        hashed.string(row["build_status"])
        hashed.string(row["materialization_scope"])
        hashed.boolean(row["full_workload_materialized"])
        hashed.boolean(row["capacity_model_reached"])
        hashed.boolean(row["allocator_reached"])
        hashed.byte(row["error_code"])
        hashed.string(row["invariant_id"])
        hashed.byte(row["limiting_work_bound"])
        hashed.u64(row["maximum_preparable_net_count"])
        hashed.u64(row["first_unpreparable_net"]["id"])
        hashed.u32(row["first_unpreparable_net"]["generation"])
        for field in (
            "required_compiled_nodes",
            "configured_compiled_node_limit",
            "required_compiled_host_bytes",
            "configured_compiled_host_byte_limit",
            "per_net_compiled_nodes",
            "per_net_compiled_host_bytes",
        ):
            hashed.u64(row[field])
    return hashed.finish()


def compute_probe_source_envelope_checksum(probe: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-SOURCE-ENVELOPE-V1")
    hashed.u32(probe["schema_version"])
    hashed.string(probe["source_commit"])
    hashed.boolean(probe["source_stamped"])
    hashed.boolean(probe["source_tree_dirty"])
    hashed.u64(probe["artifact_checksum"])
    return hashed.finish()


def validate_probe(value: Any, *, expected_commit: str) -> Mapping[str, Any]:
    probe = _object(value, "stress probe")
    _fields(probe, _PROBE_FIELDS, "stress probe")
    if (
        _u32(probe["schema_version"], "probe.schema_version") != 1
        or _string(probe["source_commit"], "probe.source_commit") != expected_commit
        or not _boolean(probe["source_stamped"], "probe.source_stamped")
        or _boolean(probe["source_tree_dirty"], "probe.source_tree_dirty")
        or _u32(probe["corpus_version"], "probe.corpus_version") != 1
    ):
        raise StressError("stress probe is not from the expected clean v1 source")
    manifest_checksum, manifest_cases, _ = raw_validator._representative_manifest()
    if _u64(probe["corpus_checksum"], "probe.corpus_checksum") != manifest_checksum:
        raise StressError("stress probe corpus checksum differs from the frozen manifest")
    limits = _object(probe["limits"], "probe.limits")
    _fields(limits, _LIMIT_FIELDS, "probe.limits")
    expected_limits = {
        "maximum_nets": 4096,
        "maximum_compiled_nodes": 100_000_000,
        "maximum_compiled_host_bytes": 8 * 1024 * 1024 * 1024,
        "maximum_active_regions": 250_000,
        "maximum_board_entities": 100_000,
    }
    for field, expected in expected_limits.items():
        if _u64(limits[field], f"probe.limits.{field}") != expected:
            raise StressError("stress probe does not use default representative-corpus limits")
    cases = probe["cases"]
    if not isinstance(cases, list) or len(cases) != 2:
        raise StressError("stress probe must contain exactly cases 3001 and 3002")
    for index, row_value in enumerate(cases):
        label = f"probe.cases[{index}]"
        row = _object(row_value, label)
        _fields(row, _PROBE_CASE_FIELDS, label)
        case_id = _u32(row["case_id"], f"{label}.case_id")
        if case_id != 3001 + index:
            raise StressError("stress probe cases must be ordered 3001 then 3002")
        expected = _EXPECTED_WORK_BOUNDS[case_id]
        manifest = manifest_cases[case_id]
        first = _object(row["first_unpreparable_net"], f"{label}.first_unpreparable_net")
        _fields(first, ("id", "generation"), f"{label}.first_unpreparable_net")
        for field in ("id",):
            _u64(first[field], f"{label}.first_unpreparable_net.{field}")
        _u32(first["generation"], f"{label}.first_unpreparable_net.generation")
        for field in (
            "descriptor_fingerprint",
            "maximum_preparable_net_count",
            "required_compiled_nodes",
            "configured_compiled_node_limit",
            "required_compiled_host_bytes",
            "configured_compiled_host_byte_limit",
            "per_net_compiled_nodes",
            "per_net_compiled_host_bytes",
        ):
            _u64(row[field], f"{label}.{field}")
        for field in (
            "requested_net_count",
            "requested_pool_size",
            "declared_stress_target_nets",
            "error_code",
            "limiting_work_bound",
        ):
            _u32(row[field], f"{label}.{field}")
        for field in (
            "full_workload_materialized",
            "capacity_model_reached",
            "allocator_reached",
        ):
            _boolean(row[field], f"{label}.{field}")
        _string(row["build_status"], f"{label}.build_status")
        _string(row["materialization_scope"], f"{label}.materialization_scope")
        _string(row["invariant_id"], f"{label}.invariant_id")
        if (
            row["descriptor_fingerprint"] != manifest["descriptor_fingerprint"]
            or row["descriptor_fingerprint"] != expected["descriptor_fingerprint"]
            or row["requested_net_count"] != manifest["workload_net_count"]
            or row["requested_net_count"] != expected["requested_net_count"]
            or row["requested_pool_size"] != 4
            or row["declared_stress_target_nets"] != 4096
            or row["build_status"] != "compiled_work_bound"
            or row["materialization_scope"]
            != "descriptor_board_and_one_representative_compiled_net_only"
            or row["full_workload_materialized"]
            or row["capacity_model_reached"]
            or row["allocator_reached"]
            or row["error_code"] != 10
            or row["invariant_id"] != "benchmark.phase4_representative.compiled_work_bound.v1"
            or row["limiting_work_bound"] != 1
            or row["configured_compiled_node_limit"] != limits["maximum_compiled_nodes"]
            or row["configured_compiled_host_byte_limit"] != limits["maximum_compiled_host_bytes"]
        ):
            raise StressError(f"{label} does not reproduce the frozen compiled-work witness")
        for field in (
            "maximum_preparable_net_count",
            "first_unpreparable_net",
            "required_compiled_nodes",
            "required_compiled_host_bytes",
            "per_net_compiled_nodes",
            "per_net_compiled_host_bytes",
        ):
            if row[field] != expected[field]:
                raise StressError(f"{label}.{field} differs from frozen stress arithmetic")
        if (
            row["required_compiled_nodes"]
            != row["per_net_compiled_nodes"] * row["requested_net_count"]
            or row["required_compiled_host_bytes"]
            != row["per_net_compiled_host_bytes"] * row["requested_net_count"]
            or row["maximum_preparable_net_count"]
            != row["configured_compiled_node_limit"] // row["per_net_compiled_nodes"]
            or row["first_unpreparable_net"]["id"] != 1000 + row["maximum_preparable_net_count"]
        ):
            raise StressError(f"{label} work-bound quotient or first-net arithmetic is invalid")
    if (
        _u64(probe["artifact_checksum"], "probe.artifact_checksum") == 0
        or probe["artifact_checksum"] != compute_probe_artifact_checksum(probe)
        or _u64(probe["source_envelope_checksum"], "probe.source_envelope_checksum") == 0
        or probe["source_envelope_checksum"] != compute_probe_source_envelope_checksum(probe)
    ):
        raise StressError("stress probe checksum does not authenticate its contents or source")
    return probe


def read_probe(path: pathlib.Path, *, expected_commit: str) -> Mapping[str, Any]:
    return validate_probe(
        _read_canonical(path, "stress work-bound probe", _MAXIMUM_PROBE_BYTES),
        expected_commit=expected_commit,
    )


def _availability(status: str, reason: str) -> dict[str, str]:
    return {"status": status, "reason": reason}


def _canonical_payload(document: Mapping[str, Any]) -> str:
    payload = {
        key: value
        for key, value in document.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    return json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def compute_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-STRESS-EVIDENCE-ARTIFACT-V1")
    hashed.string(_canonical_payload(document))
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-STRESS-EVIDENCE-SOURCE-ENVELOPE-V1")
    hashed.u32(document["schema_version"])
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def _successful_case(
    raw: Mapping[str, Any], report: Mapping[str, Any], operational: Mapping[str, Any]
) -> dict[str, Any]:
    config = raw["config"]
    manifest_checksum, manifest_cases, _ = raw_validator._representative_manifest()
    manifest = manifest_cases[3000]
    if (
        config["case_id"] != 3000
        or config["requested_pool_size"] != 4
        or manifest["workload_net_count"] != 1024
        or raw["corpus_checksum"] != manifest_checksum
    ):
        raise StressError("successful stress authority is not frozen case 3000 at pool 4")
    timing_rows = []
    for row in operational["repetitions"]:
        timing_rows.append(
            {
                "repetition_index": row["repetition_index"],
                "execution_order": row["execution_order"],
                "baseline_timing_nanoseconds": copy.deepcopy(row["baseline"]["timing_nanoseconds"]),
                "candidate_timing_nanoseconds": copy.deepcopy(
                    row["candidate"]["timing_nanoseconds"]
                ),
            }
        )
    return {
        "case_id": 3000,
        "descriptor_fingerprint": manifest["descriptor_fingerprint"],
        "requested_net_count": 1024,
        "requested_pool_size": 4,
        "declared_stress_target_nets": 4096,
        "evidence_status": "raw_report_operational_success",
        "materialization_scope": "complete_frozen_case_and_twenty_paired_repetitions",
        "full_workload_materialized": True,
        "candidate_preparation_reached": True,
        "allocator_reached": True,
        "logical_compiled_requirements": {
            "compiled_nodes": manifest["required_compiled_nodes"],
            "compiled_host_bytes": manifest["required_compiled_host_bytes"],
            "host_bytes_kind": "logical_compiled_estimate_not_peak_rss",
            "active_regions": manifest["required_active_regions"],
            "board_entities": manifest["required_board_entities"],
        },
        "execution_measurement": {
            "status": "available",
            "timing_scope": "raw_nested_case_build_prepared_cold_outer_intervals",
            "cpu_utilization": _availability(
                "unavailable", "Raw v1 does not contain CPU utilization samples"
            ),
            "gpu_utilization": _availability(
                "not_applicable", "both measured contenders are CPU-only"
            ),
            "process_lifetime_peaks": [
                {
                    "arm": process["arm"],
                    "peak_host_bytes": process["process_lifetime_peak_host_bytes"],
                    "scope": process["peak_measurement_scope"],
                }
                for process in operational["processes"]
            ],
            "repetitions": timing_rows,
        },
        "source_binding": {
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
            "report_artifact_checksum": report["artifact_checksum"],
            "report_source_envelope_checksum": report["source_envelope_checksum"],
            "operational_artifact_checksum": operational["artifact_checksum"],
            "operational_source_envelope_checksum": operational["source_envelope_checksum"],
            "environment_checksum": raw["environment"]["environment_checksum"],
            "authority_run_identity": raw["authority_run_identity"],
            "controller_identity": raw["controller_identity"],
        },
    }


def _bounded_case(row: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "case_id": row["case_id"],
        "descriptor_fingerprint": row["descriptor_fingerprint"],
        "requested_net_count": row["requested_net_count"],
        "requested_pool_size": row["requested_pool_size"],
        "declared_stress_target_nets": row["declared_stress_target_nets"],
        "evidence_status": "compiled_work_bound",
        "materialization_scope": row["materialization_scope"],
        "full_workload_materialized": False,
        "candidate_preparation_reached": False,
        "allocator_reached": False,
        "work_bound": {
            "limiting_work_bound": "compiled_nodes",
            "maximum_preparable_net_count": row["maximum_preparable_net_count"],
            "prefix_interpretation": "capacity_derived_not_materialized_or_achieved",
            "first_unpreparable_net": copy.deepcopy(row["first_unpreparable_net"]),
            "required_compiled_nodes": row["required_compiled_nodes"],
            "configured_compiled_node_limit": row["configured_compiled_node_limit"],
            "required_compiled_host_bytes": row["required_compiled_host_bytes"],
            "configured_compiled_host_byte_limit": row["configured_compiled_host_byte_limit"],
            "per_net_compiled_nodes": row["per_net_compiled_nodes"],
            "per_net_compiled_host_bytes": row["per_net_compiled_host_bytes"],
            "host_bytes_kind": "logical_compiled_estimate_not_peak_rss",
        },
        "execution_measurement": {
            "elapsed_time": _availability(
                "unavailable", "the builder returns during deterministic compiled-work preflight"
            ),
            "peak_host_memory": _availability(
                "unavailable", "the bounded probe does not collect wait4 peak RSS"
            ),
            "candidate_preparation": _availability(
                "not_reached", "compiled-work bound stops before candidate preparation"
            ),
            "allocator": _availability(
                "not_reached", "compiled-work bound stops before allocator execution"
            ),
        },
    }


def project_document(
    raw: Mapping[str, Any],
    report: Mapping[str, Any],
    operational: Mapping[str, Any],
    probe: Mapping[str, Any],
) -> dict[str, Any]:
    if (
        raw["source_commit"] != probe["source_commit"]
        or raw["source_stamped"] != probe["source_stamped"]
        or raw["source_tree_dirty"] != probe["source_tree_dirty"]
        or raw["corpus_checksum"] != probe["corpus_checksum"]
        or raw["config"]["corpus_limits"] != probe["limits"]
    ):
        raise StressError("successful Raw cell and work-bound probe differ in source or limits")
    cases = [_successful_case(raw, report, operational)]
    cases.extend(_bounded_case(row) for row in probe["cases"])
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "standalone_decision_eligible": False,
        "phase4_complete": False,
        "stress_ladder_coverage_complete": True,
        "declared_target_net_count": 4096,
        "maximum_full_raw_success_net_count": 1024,
        "declared_target_fully_supported": False,
        "scalability_result": "1024_full_raw_success_2048_and_4096_compiled_work_bound",
        "corpus_checksum": raw["corpus_checksum"],
        "representative_corpus_limits": copy.deepcopy(probe["limits"]),
        "work_bound_probe_binding": {
            "artifact_checksum": probe["artifact_checksum"],
            "source_envelope_checksum": probe["source_envelope_checksum"],
        },
        "cases": cases,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = compute_artifact_checksum(result)
    result["source_envelope_checksum"] = compute_source_envelope_checksum(result)
    return result


def validate_authorities(
    raw: Mapping[str, Any],
    report: Mapping[str, Any],
    operational: Mapping[str, Any],
    probe: Mapping[str, Any],
    *,
    expected_commit: str,
) -> None:
    if _COMMIT.fullmatch(expected_commit) is None:
        raise StressError("publication requires an independently supplied expected commit")
    raw_validator.validate_document(raw, expected_commit=expected_commit)
    report_validator.validate_join(raw, report, expected_commit=expected_commit)
    operational_projector.validate_projection(raw, operational)
    validate_probe(probe, expected_commit=expected_commit)


def _assert_exact(expected: Any, actual: Any, label: str = "stress artifact") -> None:
    if type(actual) is not type(expected):
        raise StressError(f"{label} has a noncanonical JSON type")
    if isinstance(expected, dict):
        if tuple(actual.keys()) != tuple(expected.keys()):
            raise StressError(f"{label} fields or field order differ from the rebuilt artifact")
        for key in expected:
            _assert_exact(expected[key], actual[key], f"{label}.{key}")
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            raise StressError(f"{label} length differs from the rebuilt artifact")
        for index, (expected_child, actual_child) in enumerate(zip(expected, actual, strict=True)):
            _assert_exact(expected_child, actual_child, f"{label}[{index}]")
    elif actual != expected:
        raise StressError(f"{label} differs from the rebuilt stress evidence")


def validate_artifact(
    raw: Mapping[str, Any],
    report: Mapping[str, Any],
    operational: Mapping[str, Any],
    probe: Mapping[str, Any],
    value: Mapping[str, Any],
    *,
    expected_commit: str,
) -> None:
    validate_authorities(raw, report, operational, probe, expected_commit=expected_commit)
    expected = project_document(raw, report, operational, probe)
    _assert_exact(expected, value)
    if value["artifact_checksum"] != compute_artifact_checksum(value):
        raise StressError("artifact_checksum does not authenticate the stress artifact")
    if value["source_envelope_checksum"] != compute_source_envelope_checksum(value):
        raise StressError("source_envelope_checksum does not authenticate stress source")


def read_artifact(path: pathlib.Path) -> Mapping[str, Any]:
    return _read_canonical(path, "stress evidence", _MAXIMUM_ARTIFACT_BYTES)


def encode_artifact(value: Mapping[str, Any]) -> str:
    encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
    if len(encoded.encode("utf-8")) > _MAXIMUM_ARTIFACT_BYTES:
        raise StressError(
            f"stress evidence exceeds the {_MAXIMUM_ARTIFACT_BYTES}-byte output bound"
        )
    return encoded


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw-3000", required=True, type=pathlib.Path)
    parser.add_argument("--report-3000", required=True, type=pathlib.Path)
    parser.add_argument("--operational-3000", required=True, type=pathlib.Path)
    parser.add_argument("--work-bound-probe", required=True, type=pathlib.Path)
    parser.add_argument("--validate", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw = raw_validator.read_validated_publication_document(
            options.raw_3000, expected_commit=options.expected_commit
        )
        report = report_validator.read_report_document(options.report_3000)
        report_validator.validate_join(raw, report, expected_commit=options.expected_commit)
        operational = operational_projector.read_projection(options.operational_3000)
        operational_projector.validate_projection(raw, operational)
        probe = read_probe(options.work_bound_probe, expected_commit=options.expected_commit)
        if options.validate is None:
            sys.stdout.write(encode_artifact(project_document(raw, report, operational, probe)))
        else:
            validate_artifact(
                raw,
                report,
                operational,
                probe,
                read_artifact(options.validate),
                expected_commit=options.expected_commit,
            )
    except (
        StressError,
        operational_projector.ProjectionError,
        raw_validator.EvidenceError,
        report_validator.EvidenceError,
    ) as error:
        print(f"Phase 4 stress evidence failed: {error}", file=sys.stderr)
        return 1
    if options.validate is not None:
        print("validated one Phase 4 Stress Evidence v1 artifact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
