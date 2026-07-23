"""Project Phase 4 operational evidence from joined Raw-v2 same-run authority."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import project_phase4_operational_evidence as v1_projector
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_MAXIMUM_PROJECTION_BYTES = 16 * 1024 * 1024
ProjectionError = v1_projector.ProjectionError


def _canonical_payload(document: Mapping[str, Any]) -> str:
    payload = {
        key: value
        for key, value in document.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    return json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def compute_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-PROJECTION-ARTIFACT-V2")
    hashed.string(_canonical_payload(document))
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-PROJECTION-SOURCE-ENVELOPE-V2")
    hashed.u32(document["schema_version"])
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def _raw_v2_reason(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: _raw_v2_reason(child) for key, child in value.items()}
    if isinstance(value, list):
        return [_raw_v2_reason(child) for child in value]
    if isinstance(value, str):
        return value.replace("Raw v1", "Raw v2")
    return value


def project_document(raw: Mapping[str, Any], sidecar: Mapping[str, Any]) -> dict[str, Any]:
    """Derive Operational Projection v2 from a previously validated joined pair."""
    if raw.get("raw_evidence_schema_version") != 2 or raw.get("wire_schema_version") != 2:
        raise ProjectionError("Operational Projection v2 requires Raw-v2/Wire-v2")
    attempts = raw["attempts"]
    captures = sidecar["attempts"]
    if len(attempts) != len(captures):
        raise ProjectionError("Operational Projection v2 requires every same-run capture")
    base = v1_projector.project_document(raw)
    raw_binding = {
        "raw_evidence_schema_version": raw["raw_evidence_schema_version"],
        **copy.deepcopy(base["raw_binding"]),
    }
    repetitions: list[dict[str, Any]] = []
    for row, capture in zip(base["repetitions"], captures, strict=True):
        projected = copy.deepcopy(row)
        projected["same_run_pair_capture_checksum"] = capture["capture_checksum"]
        projected["baseline"]["same_run_arm_capture_checksum"] = capture["baseline"][
            "capture_checksum"
        ]
        projected["candidate"]["same_run_arm_capture_checksum"] = capture["candidate"][
            "capture_checksum"
        ]
        repetitions.append(projected)
    execution_model = copy.deepcopy(base["execution_model"])
    execution_model["measurement_origin"] = "phase4_same_run_raw_evidence_v2"
    result: dict[str, Any] = {
        "schema_version": 2,
        "source_commit": base["source_commit"],
        "source_stamped": base["source_stamped"],
        "source_tree_dirty": base["source_tree_dirty"],
        "eligible_input_to_statistics": True,
        "standalone_decision_eligible": False,
        "coverage_complete": False,
        "phase4_telemetry_complete": False,
        "raw_binding": raw_binding,
        "same_run_telemetry_binding": {
            "schema_version": sidecar["schema_version"],
            "telemetry_wire_schema_version": sidecar["telemetry_wire_schema_version"],
            "artifact_checksum": sidecar["artifact_checksum"],
            "source_envelope_checksum": sidecar["source_envelope_checksum"],
            "exact_rejection_guardrail_passed": (
                same_run_validator.exact_rejection_guardrail_passes(sidecar)
            ),
        },
        "cell_identity": copy.deepcopy(base["cell_identity"]),
        "host_environment": copy.deepcopy(base["host_environment"]),
        "execution_model": execution_model,
        "measurement_availability": _raw_v2_reason(base["measurement_availability"]),
        "processes": copy.deepcopy(base["processes"]),
        "repetitions": repetitions,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = compute_artifact_checksum(result)
    result["source_envelope_checksum"] = compute_source_envelope_checksum(result)
    return result


def validate_projection(
    raw: Mapping[str, Any],
    sidecar: Mapping[str, Any],
    value: Mapping[str, Any],
) -> None:
    expected = project_document(raw, sidecar)
    v1_projector.assert_exact_projection(expected, value)
    if value["artifact_checksum"] != compute_artifact_checksum(value):
        raise ProjectionError("artifact_checksum does not authenticate the v2 projection")
    if value["source_envelope_checksum"] != compute_source_envelope_checksum(value):
        raise ProjectionError("source_envelope_checksum does not authenticate the v2 source")


def read_projection(path: pathlib.Path) -> Mapping[str, Any]:
    return v1_projector.read_projection(path)


def encode_projection(value: Mapping[str, Any]) -> str:
    encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
    if len(encoded.encode("utf-8")) > _MAXIMUM_PROJECTION_BYTES:
        raise ProjectionError(
            f"operational projection exceeds the {_MAXIMUM_PROJECTION_BYTES}-byte output bound"
        )
    return encoded


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--validate", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw = raw_validator.read_document(options.raw)
        sidecar = same_run_validator.read_document(options.same_run_telemetry)
        same_run_validator.validate_join(
            raw,
            sidecar,
            expected_commit=options.expected_commit,
        )
        if options.validate is None:
            sys.stdout.write(encode_projection(project_document(raw, sidecar)))
        else:
            validate_projection(raw, sidecar, read_projection(options.validate))
    except (raw_validator.EvidenceError, ProjectionError) as error:
        print(f"Phase 4 Operational Projection v2 failed: {error}", file=sys.stderr)
        return 1
    if options.validate is not None:
        print("validated one Phase 4 Operational Projection v2 artifact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
