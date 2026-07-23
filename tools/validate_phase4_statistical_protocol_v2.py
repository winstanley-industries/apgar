"""Authority-only v2 supersession of the frozen Phase 4 decision protocol."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_statistical_protocol as protocol_v1
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_statistical_decision_protocol_v2.json"
_MAX_BYTES = 64 * 1024
_DECISION_GROUPS = ("exact", "heldout", "imported")
_RAW_V2 = "phase4_same_run_raw_evidence_v2"
_REPORT_V2 = "phase4_per_net_report_publication_join_v2"
_OPERATIONAL_V2 = "phase4_operational_projection_v2"


class ProtocolV2Error(ValueError):
    """Stable malformed-v2 diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V2")
    hashed.string(_canonical(payload))
    return hashed.finish()


def effective_cell_groups() -> list[dict[str, Any]]:
    """Apply the authority-only v2 substitutions to frozen v1 cell groups."""
    groups = copy.deepcopy(protocol_v1.expected_protocol()["matrix"]["cell_groups"])
    replacements = {
        "phase4_raw_evidence_v1": _RAW_V2,
        "phase4_per_net_report_publication_join_v1": _REPORT_V2,
        "phase4_operational_projection_v1": _OPERATIONAL_V2,
    }
    for group in groups:
        if group["group"] not in _DECISION_GROUPS:
            continue
        group["evidence_requirement"] = "same_run_raw_success"
        group["required_artifacts"] = [
            replacements.get(artifact, artifact) for artifact in group["required_artifacts"]
        ]
    return groups


def expanded_cells() -> list[tuple[int, int, str, str]]:
    """Return the v2 effective matrix with same-run decision-cell authority."""
    return protocol_v1._expand_groups(effective_cell_groups())


def expected_protocol() -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema_version": 2,
        "supersedes": {
            "schema_version": 1,
            "artifact_checksum": protocol_v1.expected_protocol()["artifact_checksum"],
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "authority_only_supersession": True,
        },
        "authority_change": {
            "reason": "wire_v2_same_execution_telemetry_requires_distinct_raw_authority",
            "decision_groups": list(_DECISION_GROUPS),
            "raw_outcome_and_timing_authority": _RAW_V2,
            "same_run_guardrail_authority": "phase4_same_run_decision_telemetry_v1",
            "per_net_join_authority": _REPORT_V2,
            "operational_projection_authority": _OPERATIONAL_V2,
            "legacy_raw_v1_role": "calibration_fixed_query_and_stress_only",
        },
        "matrix_authority_counts": {
            "legacy_raw_v1_success_cells": 22,
            "same_run_raw_v2_success_cells": 78,
            "noncalibration_legacy_raw_v1_success_cells": 4,
            "noncalibration_same_run_raw_v2_success_cells": 78,
        },
        "unchanged_v1_sections": [
            "manifest_binding",
            "logical_matrix",
            "families",
            "outcome",
            "inference",
            "guardrail_thresholds",
            "timing_statistics",
            "completion_requirements",
        ],
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def validate_document(value: Any) -> Mapping[str, Any]:
    protocol_v1.read_protocol()
    if not isinstance(value, dict):
        raise ProtocolV2Error("protocol v2 must be a JSON object")
    protocol_v1._check_depth(value)
    expected = expected_protocol()
    if not protocol_v1._exact_equal(value, expected):
        raise ProtocolV2Error("protocol does not exactly reconstruct the frozen v2 literal")
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ProtocolV2Error("artifact_checksum does not authenticate protocol v2")
    cells = expanded_cells()
    if len(cells) != 104 or len(set(cells)) != 104:
        raise ProtocolV2Error("v2 authority expansion is not 104 unique logical cells")
    if sum(row[3] == "same_run_raw_success" for row in cells) != 78:
        raise ProtocolV2Error("v2 authority expansion is not 78 same-run Raw-v2 cells")
    if sum(row[3] == "raw_success" for row in cells) != 22:
        raise ProtocolV2Error("v2 authority expansion is not 22 legacy Raw-v1 cells")
    closure_cells = [
        row
        for group in effective_cell_groups()
        if group["in_noncalibration_closure"]
        for row in protocol_v1._expand_groups([group])
    ]
    derived_counts = {
        "legacy_raw_v1_success_cells": sum(row[3] == "raw_success" for row in cells),
        "same_run_raw_v2_success_cells": sum(row[3] == "same_run_raw_success" for row in cells),
        "noncalibration_legacy_raw_v1_success_cells": sum(
            row[3] == "raw_success" for row in closure_cells
        ),
        "noncalibration_same_run_raw_v2_success_cells": sum(
            row[3] == "same_run_raw_success" for row in closure_cells
        ),
    }
    if value["matrix_authority_counts"] != derived_counts:
        raise ProtocolV2Error("v2 matrix authority counts do not reconstruct from cell groups")
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolV2Error(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ProtocolV2Error(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ProtocolV2Error(f"cannot read protocol v2: {error}") from error
    if len(data) > _MAX_BYTES:
        raise ProtocolV2Error("protocol v2 exceeds 64 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ProtocolV2Error("protocol v2 must be UTF-8 without BOM and end in one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ProtocolV2Error(f"invalid protocol v2 JSON: {error}") from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ProtocolV2Error("protocol v2 is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except (ProtocolV2Error, protocol_v1.ProtocolError) as error:
        print(f"phase4 statistical protocol v2 validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 statistical decision protocol v2 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
