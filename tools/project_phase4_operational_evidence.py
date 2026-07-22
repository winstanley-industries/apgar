"""Project and rebuild-validate Phase 4 operational evidence from canonical Raw v1."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator

_MAXIMUM_PROJECTION_BYTES = 16 * 1024 * 1024
_MAXIMUM_NESTING_DEPTH = 64


class ProjectionError(ValueError):
    """A projection is malformed or does not rebuild from its Raw v1 cell."""


def _availability(status: str, reason: str) -> dict[str, str]:
    return {"status": status, "reason": reason}


def _arm_projection(attempt: Mapping[str, Any]) -> dict[str, Any]:
    record = attempt["record"]
    semantics = record["semantics"]
    observation = record["external_observation"]
    case_build = record["case_build_elapsed_nanoseconds"]
    prepared = record["prepared_elapsed_nanoseconds"]
    cold = record["cold_elapsed_nanoseconds"]
    outer = observation["outer_elapsed_nanoseconds"]
    if case_build + prepared > cold or cold > outer:
        raise ProjectionError("validated Raw v1 contains non-nested timing intervals")
    return {
        "arm": semantics["arm"],
        "dispatch_ordinal": attempt["dispatch_ordinal"],
        "process_instance_identity": attempt["process_instance_identity"],
        "attempt_checksum": attempt["attempt_checksum"],
        "semantic_checksum": semantics["semantic_checksum"],
        "record_artifact_checksum": record["artifact_checksum"],
        "authority_checksum": observation["authority_checksum"],
        "timing_nanoseconds": {
            "case_build": case_build,
            "prepared": prepared,
            "cold": cold,
            "outer": outer,
            "cold_residual": cold - case_build - prepared,
            "outer_residual": outer - cold,
        },
        "opportunity": copy.deepcopy(semantics["opportunity"]),
        "actual": copy.deepcopy(semantics["actual"]),
        "preparation": {
            "route_queries": semantics["preparation_route_queries"],
            "route_work_units": semantics["preparation_route_work_units"],
        },
        "regeneration": {
            "route_queries": semantics["regeneration_route_queries"],
            "route_work_units": semantics["regeneration_route_work_units"],
        },
        "candidate_accounting": {
            "requested": semantics["requested_columns"],
            "admitted": semantics["admitted_candidates"],
            "rejected": semantics["rejected_columns"],
            "final": semantics["final_candidate_count"],
        },
        "preparer_lifecycle": copy.deepcopy(record["preparer_lifecycle"]),
    }


def _process_projection(
    attempts: Sequence[Mapping[str, Any]], rows: Sequence[Mapping[str, Any]], arm: int
) -> dict[str, Any]:
    name = "baseline" if arm == 0 else "candidate"
    arms = [row[name] for row in rows]
    observations = [attempt[name]["record"]["external_observation"] for attempt in attempts]
    process_ids = {row["process_instance_identity"] for row in arms}
    peaks = {attempt[name]["process_lifetime_peak_host_bytes"] for attempt in attempts}
    if len(process_ids) != 1 or len(peaks) != 1:
        raise ProjectionError(
            "validated Raw v1 does not have one process identity and peak per arm"
        )
    return {
        "arm": arm,
        "process_instance_identity": next(iter(process_ids)),
        "process_lifetime_peak_host_bytes": next(iter(peaks)),
        "peak_measurement_scope": "long_lived_worker_including_warmup_and_all_repetitions",
        "configured_external_caps": {
            "wall_limit_nanoseconds": observations[0]["configured_wall_limit_nanoseconds"],
            "address_space_limit_bytes": observations[0]["configured_address_space_limit_bytes"],
            "peak_host_limit_bytes": observations[0]["configured_peak_host_limit_bytes"],
        },
        "successful_attempt_count": len(arms),
        "first_dispatch_ordinal": min(row["dispatch_ordinal"] for row in arms),
        "last_dispatch_ordinal": max(row["dispatch_ordinal"] for row in arms),
        "initial_preparer_lifecycle": copy.deepcopy(arms[0]["preparer_lifecycle"]),
        "final_preparer_lifecycle": copy.deepcopy(arms[-1]["preparer_lifecycle"]),
    }


def _canonical_payload(document: Mapping[str, Any]) -> str:
    payload = {
        key: value
        for key, value in document.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    return json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def compute_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-PROJECTION-ARTIFACT-V1")
    hashed.string(_canonical_payload(document))
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-PROJECTION-SOURCE-ENVELOPE-V1")
    hashed.u32(document["schema_version"])
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def project_document(raw: Mapping[str, Any]) -> dict[str, Any]:
    """Derive Operational Projection v1 from an already fully validated Raw v1 cell."""
    attempts = raw["attempts"]
    if len(attempts) != 20 or raw["config"]["repetitions"] != 20:
        raise ProjectionError("Operational Projection v1 requires exactly 20 Raw repetitions")
    rows: list[dict[str, Any]] = []
    for repetition, pair in enumerate(attempts):
        rows.append(
            {
                "repetition_index": repetition,
                "execution_order": pair["execution_order"],
                "pair_attempt_checksum": pair["attempt_checksum"],
                "paired_semantic_checksum": pair["result"]["semantic_checksum"],
                "paired_artifact_checksum": pair["result"]["artifact_checksum"],
                "baseline": _arm_projection(pair["baseline"]),
                "candidate": _arm_projection(pair["candidate"]),
            }
        )
    if [row["execution_order"] for row in rows].count(0) != 10 or [
        row["execution_order"] for row in rows
    ].count(1) != 10:
        raise ProjectionError("Operational Projection v1 requires ten AB and ten BA repetitions")
    baseline_process = _process_projection(attempts, rows, 0)
    candidate_process = _process_projection(attempts, rows, 1)
    if (
        baseline_process["process_instance_identity"]
        == candidate_process["process_instance_identity"]
    ):
        raise ProjectionError("Operational Projection v1 requires two distinct arm processes")
    first = attempts[0]["baseline"]["record"]["semantics"]
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "eligible_input_to_statistics": True,
        "standalone_decision_eligible": False,
        "coverage_complete": False,
        "phase4_telemetry_complete": False,
        "raw_binding": {
            "raw_wire_schema_version": raw["wire_schema_version"],
            "config": copy.deepcopy(raw["config"]),
            "corpus_checksum": raw["corpus_checksum"],
            "cell_plan_checksum": raw["cell_plan_checksum"],
            "environment_checksum": raw["environment"]["environment_checksum"],
            "authority_run_identity": raw["authority_run_identity"],
            "controller_identity": raw["controller_identity"],
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        },
        "cell_identity": {
            "case_id": first["case_id"],
            "descriptor_fingerprint": first["descriptor_fingerprint"],
            "case_checksum": first["case_checksum"],
            "board_content_hash": first["board_content_hash"],
            "workload_checksum": first["workload_checksum"],
            "capacity_model_checksum": first["capacity_model_checksum"],
            "budget_checksum": first["budget_checksum"],
            "workload_net_count": first["workload_net_count"],
            "root_seed": first["root_seed"],
        },
        "host_environment": copy.deepcopy(raw["environment"]),
        "execution_model": {
            "contender_backend": "cpu_only",
            "candidate_preparation": "persistent_four_worker_preparer",
            "prepared_view_execution": "precompiled_direct_per_net_no_runtime_cache",
            "measurement_origin": "phase4_isolated_raw_evidence_v1",
        },
        "measurement_availability": {
            "cpu_utilization": _availability(
                "unavailable", "Raw v1 records elapsed time but no CPU utilization samples"
            ),
            "gpu_utilization": _availability(
                "not_applicable", "the measured contenders are CPU-only"
            ),
            "device_memory": _availability(
                "not_applicable", "the measured contenders allocate no GPU device memory"
            ),
            "batch_fill": _availability(
                "unavailable", "Raw v1 does not record compatible-batch occupancy"
            ),
            "prepared_view_cache_measurement": _availability(
                "unavailable", "Raw v1 has no runtime prepared-view cache counters"
            ),
            "finer_prepared_stage_timing": _availability(
                "unavailable", "Raw v1 records only the aggregate prepared interval"
            ),
            "import_timing": _availability(
                "unavailable", "Raw v1 does not separate import from case construction"
            ),
            "compilation_timing": _availability(
                "unavailable", "Raw v1 does not separate compilation from case construction"
            ),
            "initial_upload_timing": _availability(
                "not_applicable", "the measured contenders are CPU-only"
            ),
            "cache_miss_timing": _availability(
                "unavailable", "Raw v1 does not record cache-miss intervals"
            ),
            "transient_release_timing": _availability(
                "unavailable", "Raw v1 does not separate transient release"
            ),
            "per_repetition_rss": _availability(
                "unavailable", "wait4 exposes one process-lifetime peak, not repetition peaks"
            ),
            "complete_toolchain_hardware_provenance": _availability(
                "unavailable",
                "Raw v1 compiler_identity and host labels are not a complete toolchain and hardware manifest",
            ),
        },
        "processes": [baseline_process, candidate_process],
        "repetitions": rows,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = compute_artifact_checksum(result)
    result["source_envelope_checksum"] = compute_source_envelope_checksum(result)
    return result


def _reject_duplicate_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProjectionError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_non_json_constant(value: str) -> Any:
    raise ProjectionError(f"non-JSON numeric constant: {value}")


def _check_nesting(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > _MAXIMUM_NESTING_DEPTH:
            raise ProjectionError("operational projection exceeds the nesting-depth bound")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def read_projection(path: pathlib.Path) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(_MAXIMUM_PROJECTION_BYTES + 1)
        if len(encoded) > _MAXIMUM_PROJECTION_BYTES:
            raise ProjectionError(
                f"operational projection exceeds the {_MAXIMUM_PROJECTION_BYTES}-byte input bound"
            )
        raw = encoded.decode("utf-8")
        value = json.loads(
            raw,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_nesting(value)
        canonical = (
            json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
        )
        if raw != canonical:
            raise ProjectionError("operational projection must be canonical one-line JSON plus LF")
        if not isinstance(value, dict):
            raise ProjectionError("operational projection must be an object")
        return value
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        if isinstance(error, ProjectionError):
            raise
        raise ProjectionError(f"cannot read operational projection {path}: {error}") from error


def _assert_exact(expected: Any, actual: Any, label: str = "operational projection") -> None:
    if type(actual) is not type(expected):
        raise ProjectionError(f"{label} has a noncanonical JSON type")
    if isinstance(expected, dict):
        if tuple(actual.keys()) != tuple(expected.keys()):
            raise ProjectionError(
                f"{label} fields or field order differ from the rebuilt projection"
            )
        for key in expected:
            _assert_exact(expected[key], actual[key], f"{label}.{key}")
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            raise ProjectionError(f"{label} length differs from the rebuilt projection")
        for index, (expected_child, actual_child) in enumerate(zip(expected, actual, strict=True)):
            _assert_exact(expected_child, actual_child, f"{label}[{index}]")
    elif actual != expected:
        raise ProjectionError(f"{label} differs from the fully validated Raw v1 projection")


def validate_projection(raw: Mapping[str, Any], value: Mapping[str, Any]) -> None:
    expected = project_document(raw)
    _assert_exact(expected, value)
    if value["artifact_checksum"] != compute_artifact_checksum(value):
        raise ProjectionError("artifact_checksum does not authenticate the projection")
    if value["source_envelope_checksum"] != compute_source_envelope_checksum(value):
        raise ProjectionError(
            "source_envelope_checksum does not authenticate the projection source"
        )


def encode_projection(value: Mapping[str, Any]) -> str:
    encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
    encoded_bytes = encoded.encode("utf-8")
    if len(encoded_bytes) > _MAXIMUM_PROJECTION_BYTES:
        raise ProjectionError(
            f"operational projection exceeds the {_MAXIMUM_PROJECTION_BYTES}-byte output bound"
        )
    return encoded


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--validate", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        # This full publication validation is deliberately the first data-dependent action.
        raw = raw_validator.read_validated_publication_document(
            options.raw, expected_commit=options.expected_commit
        )
        if options.validate is None:
            sys.stdout.write(encode_projection(project_document(raw)))
        else:
            validate_projection(raw, read_projection(options.validate))
    except (raw_validator.EvidenceError, ProjectionError) as error:
        print(f"Phase 4 operational projection failed: {error}", file=sys.stderr)
        return 1
    if options.validate is not None:
        print("validated one Phase 4 Operational Projection v1 artifact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
