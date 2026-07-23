"""Authority-only v3 supersession of the frozen Phase 4 decision protocol."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_statistical_protocol as protocol_v1
from tools import validate_phase4_statistical_protocol_v2 as protocol_v2
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_statistical_decision_protocol_v3.json"
_MAX_BYTES = 64 * 1024
_ORACLE_V1 = "phase4_exact_small_oracle_v1"
_ORACLE_V2 = "phase4_exact_small_oracle_v2"


class ProtocolV3Error(ValueError):
    """Stable malformed-v3 diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V3")
    hashed.string(_canonical(payload))
    return hashed.finish()


def effective_cell_groups() -> list[dict[str, Any]]:
    """Apply only the exact-small publication substitution to protocol v2."""
    groups = copy.deepcopy(protocol_v2.effective_cell_groups())
    exact = next(group for group in groups if group["group"] == "exact")
    exact["required_artifacts"] = [
        _ORACLE_V2 if artifact == _ORACLE_V1 else artifact
        for artifact in exact["required_artifacts"]
    ]
    return groups


def expanded_cells() -> list[tuple[int, int, str, str]]:
    return protocol_v1._expand_groups(effective_cell_groups())


def expected_protocol() -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema_version": 3,
        "supersedes": {
            "schema_version": 2,
            "artifact_checksum": protocol_v2.expected_protocol()["artifact_checksum"],
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "authority_only_supersession": True,
        },
        "authority_change": {
            "reason": "exact_small_oracle_must_bind_same_run_raw_v2_authority",
            "decision_group": "exact",
            "superseded_exact_small_oracle_authority": _ORACLE_V1,
            "exact_small_oracle_authority": _ORACLE_V2,
        },
        "matrix_authority_counts": copy.deepcopy(
            protocol_v2.expected_protocol()["matrix_authority_counts"]
        ),
        "unchanged_v2_sections": [
            "manifest_binding",
            "logical_matrix",
            "families",
            "outcome",
            "inference",
            "guardrail_thresholds",
            "timing_statistics",
            "completion_requirements",
            "raw_report_operational_authorities",
        ],
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def validate_document(value: Any) -> Mapping[str, Any]:
    protocol_v2.read_protocol()
    if not isinstance(value, dict):
        raise ProtocolV3Error("protocol v3 must be a JSON object")
    protocol_v1._check_depth(value)
    expected = expected_protocol()
    if not protocol_v1._exact_equal(value, expected):
        raise ProtocolV3Error("protocol does not exactly reconstruct the frozen v3 literal")
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ProtocolV3Error("artifact_checksum does not authenticate protocol v3")
    groups = effective_cell_groups()
    exact = next(group for group in groups if group["group"] == "exact")
    if _ORACLE_V2 not in exact["required_artifacts"] or _ORACLE_V1 in exact["required_artifacts"]:
        raise ProtocolV3Error("exact group does not exclusively require Oracle Artifact v2")
    cells = expanded_cells()
    if len(cells) != 104 or len(set(cells)) != 104:
        raise ProtocolV3Error("v3 authority expansion is not 104 unique logical cells")
    if cells != protocol_v2.expanded_cells():
        raise ProtocolV3Error("v3 changes the frozen logical matrix or evidence dispositions")
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolV3Error(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ProtocolV3Error(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise ProtocolV3Error(f"cannot read protocol v3: {error}") from error
    if len(data) > _MAX_BYTES:
        raise ProtocolV3Error("protocol v3 exceeds 64 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ProtocolV3Error("protocol v3 must be UTF-8 without BOM and end in one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ProtocolV3Error(f"invalid protocol v3 JSON: {error}") from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ProtocolV3Error("protocol v3 is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except (
        ProtocolV3Error,
        protocol_v2.ProtocolV2Error,
        protocol_v1.ProtocolError,
    ) as error:
        print(f"phase4 statistical protocol v3 validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 statistical decision protocol v3 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
