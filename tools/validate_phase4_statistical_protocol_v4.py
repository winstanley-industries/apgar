"""Authority-only v4 supersession of the frozen Phase 4 decision protocol."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_statistical_protocol as protocol_v1
from tools import validate_phase4_statistical_protocol_v2 as protocol_v2
from tools import validate_phase4_statistical_protocol_v3 as protocol_v3

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_statistical_decision_protocol_v4.json"
_MAX_BYTES = 64 * 1024
_PROJECTION_V1 = "phase4_operational_projection_v1"
_PROJECTION_V2 = "phase4_operational_projection_v2"
_PUBLICATION_V1 = "phase4_operational_measurement_publication_v1"
_CANONICAL_BUDGET_ROSTER_SCHEMA_VERSION = 1
_CANONICAL_BUDGET_ROSTER_CORPUS_CHECKSUM = 7311872938254494931
_CANONICAL_BUDGET_ROSTER_CELL_COUNT = 102
_CANONICAL_BUDGET_ROSTER_CHECKSUM = 13115713216042861392


class ProtocolV4Error(ValueError):
    """Stable malformed-v4 diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V4")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _compute_budget_roster_checksum(
    budgets: Mapping[tuple[int, int], int], corpus_checksum: int
) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V1")
    hashed.u32(_CANONICAL_BUDGET_ROSTER_SCHEMA_VERSION)
    hashed.u64(corpus_checksum)
    hashed.u64(len(budgets))
    for (case_id, pool), checksum in sorted(budgets.items()):
        hashed.u32(case_id)
        hashed.u32(pool)
        hashed.u64(checksum)
    return hashed.finish()


def _validate_canonical_budget_roster() -> None:
    corpus_checksum, _, budgets = raw_validator._representative_manifest()
    if corpus_checksum != _CANONICAL_BUDGET_ROSTER_CORPUS_CHECKSUM:
        raise ProtocolV4Error("canonical budget roster corpus checksum drifted")
    if len(budgets) != _CANONICAL_BUDGET_ROSTER_CELL_COUNT:
        raise ProtocolV4Error("canonical budget roster cell count drifted")
    if (
        _compute_budget_roster_checksum(budgets, corpus_checksum)
        != _CANONICAL_BUDGET_ROSTER_CHECKSUM
    ):
        raise ProtocolV4Error("canonical budget roster checksum drifted")


def effective_cell_groups() -> list[dict[str, Any]]:
    """Replace legacy projections with the complete publication on success cells."""
    groups = copy.deepcopy(protocol_v3.effective_cell_groups())
    for group in groups:
        artifacts = group["required_artifacts"]
        if group["evidence_requirement"] in {"raw_success", "same_run_raw_success"}:
            group["required_artifacts"] = [
                _PUBLICATION_V1 if artifact in {_PROJECTION_V1, _PROJECTION_V2} else artifact
                for artifact in artifacts
            ]
    return groups


def expanded_cells() -> list[tuple[int, int, str, str]]:
    return protocol_v1._expand_groups(effective_cell_groups())


def expected_protocol() -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema_version": 4,
        "supersedes": {
            "schema_version": 3,
            "artifact_checksum": protocol_v3.expected_protocol()["artifact_checksum"],
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "authority_only_supersession": True,
        },
        "authority_change": {
            "reason": "operational_measurement_publication_and_canonical_budget_roster",
            "successful_cell_count": 100,
            "superseded_operational_authorities": [_PROJECTION_V1, _PROJECTION_V2],
            "operational_measurement_authority": _PUBLICATION_V1,
        },
        "canonical_algorithm_budget_authority": {
            "manifest_schema_version": _CANONICAL_BUDGET_ROSTER_SCHEMA_VERSION,
            "corpus_checksum": _CANONICAL_BUDGET_ROSTER_CORPUS_CHECKSUM,
            "cell_count": _CANONICAL_BUDGET_ROSTER_CELL_COUNT,
            "roster_checksum": _CANONICAL_BUDGET_ROSTER_CHECKSUM,
        },
        "matrix_authority_counts": copy.deepcopy(
            protocol_v3.expected_protocol()["matrix_authority_counts"]
        ),
        "unchanged_v3_sections": [
            "logical_matrix",
            "families",
            "outcome",
            "inference",
            "guardrail_thresholds",
            "timing_statistics",
            "completion_requirements",
            "raw_report_authorities",
            "exact_small_oracle_authority",
        ],
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def validate_document(value: Any) -> Mapping[str, Any]:
    protocol_v3.read_protocol()
    _validate_canonical_budget_roster()
    if not isinstance(value, dict):
        raise ProtocolV4Error("protocol v4 must be a JSON object")
    protocol_v1._check_depth(value)
    expected = expected_protocol()
    if not protocol_v1._exact_equal(value, expected):
        raise ProtocolV4Error("protocol does not exactly reconstruct the frozen v4 literal")
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ProtocolV4Error("artifact_checksum does not authenticate protocol v4")

    v3_groups = protocol_v3.effective_cell_groups()
    groups = effective_cell_groups()
    successful_cells = 0
    for prior, current in zip(v3_groups, groups, strict=True):
        expected_group = copy.deepcopy(prior)
        if current["evidence_requirement"] in {"raw_success", "same_run_raw_success"}:
            expected_group["required_artifacts"] = [
                _PUBLICATION_V1 if artifact in {_PROJECTION_V1, _PROJECTION_V2} else artifact
                for artifact in prior["required_artifacts"]
            ]
            successful_cells += len(protocol_v1._expand_groups([current]))
            if (
                current["required_artifacts"].count(_PUBLICATION_V1) != 1
                or _PROJECTION_V1 in current["required_artifacts"]
                or _PROJECTION_V2 in current["required_artifacts"]
            ):
                raise ProtocolV4Error(
                    "successful group does not exclusively require Operational Measurement v1"
                )
        elif _PUBLICATION_V1 in current["required_artifacts"]:
            raise ProtocolV4Error("non-success group requires Operational Measurement v1")
        if current != expected_group:
            raise ProtocolV4Error("v4 changes authority outside the declared substitution")
    if successful_cells != 100:
        raise ProtocolV4Error("v4 does not bind all 100 successful cells")
    cells = expanded_cells()
    if len(cells) != 104 or len(set(cells)) != 104:
        raise ProtocolV4Error("v4 authority expansion is not 104 unique logical cells")
    if cells != protocol_v3.expanded_cells():
        raise ProtocolV4Error("v4 changes the frozen logical matrix or evidence dispositions")
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolV4Error(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ProtocolV4Error(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise ProtocolV4Error(f"cannot read protocol v4: {error}") from error
    if len(data) > _MAX_BYTES:
        raise ProtocolV4Error("protocol v4 exceeds 64 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ProtocolV4Error("protocol v4 must be UTF-8 without BOM and end in one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ProtocolV4Error(f"invalid protocol v4 JSON: {error}") from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ProtocolV4Error("protocol v4 is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except (
        ProtocolV4Error,
        protocol_v3.ProtocolV3Error,
        protocol_v2.ProtocolV2Error,
        protocol_v1.ProtocolError,
        raw_validator.EvidenceError,
    ) as error:
        print(f"phase4 statistical protocol v4 validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 statistical decision protocol v4 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
