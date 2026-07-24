"""Strict validator for the pre-observation Phase 4 V2 confirmatory protocol."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_representative_manifest_v2 as authorities
from tools import validate_phase4_statistical_protocol as protocol_v1
from tools import validate_phase4_statistical_protocol_v2 as protocol_v2
from tools import validate_phase4_statistical_protocol_v3 as protocol_v3
from tools import validate_phase4_statistical_protocol_v4 as protocol_v4

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_confirmatory_decision_protocol_v1.json"
_MAX_BYTES = 256 * 1024
_RAW = "phase4_confirmatory_raw_evidence_v1"
_SAME_RUN_RAW = "phase4_confirmatory_same_run_raw_evidence_v1"
_SAME_RUN = "phase4_confirmatory_same_run_decision_telemetry_v1"
_REPORT = "phase4_confirmatory_per_net_report_publication_join_v1"
_SAME_RUN_REPORT = "phase4_confirmatory_same_run_per_net_report_publication_join_v1"
_OPERATIONAL = "phase4_confirmatory_operational_measurement_publication_v1"
_SAME_RUN_OPERATIONAL = "phase4_confirmatory_same_run_operational_measurement_publication_v1"
_ORACLE = "phase4_confirmatory_exact_small_oracle_v1"
_FIXED = "phase4_confirmatory_fixed_query_control_v1"
_STRESS = "phase4_confirmatory_stress_evidence_v1"


class ConfirmatoryProtocolError(ValueError):
    """Stable malformed-confirmatory-protocol diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _cell_groups() -> list[dict[str, Any]]:
    diagnostic = [_RAW, _REPORT, _OPERATIONAL]
    decision = [_SAME_RUN_RAW, _SAME_RUN, _SAME_RUN_REPORT, _SAME_RUN_OPERATIONAL]
    return [
        {
            "group": "exact",
            "case_ids": [10100, 10101, 10102],
            "pools": [4],
            "expansion": "cartesian",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "exact_small_oracle",
            "required_artifacts": decision + [_ORACLE],
            "in_noncalibration_closure": True,
        },
        {
            "group": "calibration",
            "case_ids": [10200, 10201, 10210, 10211, 10220, 10221],
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "calibration_only",
            "required_artifacts": diagnostic,
            "in_noncalibration_closure": False,
        },
        {
            "group": "heldout",
            "case_ids": (
                list(range(11000, 11008)) + list(range(11100, 11108)) + list(range(11200, 11208))
            ),
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "primary_inference_and_guardrails",
            "required_artifacts": decision,
            "in_noncalibration_closure": True,
        },
        {
            "group": "fixed_query_excluded",
            "case_pool_cells": [[12000, 1024], [12001, 1]],
            "expansion": "explicit",
            "evidence_requirement": "descriptor_only_excluded",
            "decision_use": "fixed_query_control",
            "required_artifacts": [_FIXED],
            "in_noncalibration_closure": True,
        },
        {
            "group": "fixed_query_raw",
            "case_pool_cells": [[12002, 4], [12003, 8], [12004, 16]],
            "expansion": "explicit",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "fixed_query_control",
            "required_artifacts": diagnostic + [_FIXED],
            "in_noncalibration_closure": True,
        },
        {
            "group": "stress_raw",
            "case_pool_cells": [[13000, 4]],
            "expansion": "explicit",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "stress_scalability",
            "required_artifacts": diagnostic + [_STRESS],
            "in_noncalibration_closure": True,
        },
        {
            "group": "stress_work_bound",
            "case_pool_cells": [[13001, 4], [13002, 4]],
            "expansion": "explicit",
            "evidence_requirement": "compiled_work_bound",
            "decision_use": "stress_scalability",
            "required_artifacts": [_STRESS],
            "in_noncalibration_closure": True,
        },
        {
            "group": "imported",
            "case_ids": [14000],
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "confirmatory_raw_success",
            "decision_use": "imported_veto",
            "required_artifacts": decision,
            "in_noncalibration_closure": True,
        },
    ]


def _expand_groups(
    groups: Sequence[Mapping[str, Any]],
) -> list[tuple[int, int, str, str]]:
    rows: list[tuple[int, int, str, str]] = []
    role_by_group = {
        "fixed_query_excluded": "fixed_query",
        "fixed_query_raw": "fixed_query",
        "stress_raw": "stress",
        "stress_work_bound": "stress",
    }
    for group in groups:
        role = role_by_group.get(group["group"], group["group"])
        if group["expansion"] == "cartesian":
            cells = ((case_id, pool) for case_id in group["case_ids"] for pool in group["pools"])
        elif group["expansion"] == "explicit":
            cells = (tuple(cell) for cell in group["case_pool_cells"])
        else:
            raise ConfirmatoryProtocolError("unknown cell-group expansion")
        rows.extend((case_id, pool, role, group["evidence_requirement"]) for case_id, pool in cells)
    return sorted(rows)


def expanded_cells() -> list[tuple[int, int, str, str]]:
    return _expand_groups(_cell_groups())


def effective_decision_sections() -> dict[str, Any]:
    """Return the inherited analysis rules anchored through Protocol v4."""
    base = protocol_v1.expected_protocol()
    return {
        "outcome": base["outcome"],
        "inference": base["inference"],
        "guardrails": base["guardrails"],
        "timing": base["timing"],
        "completion_requirements": base["completion_requirements"],
    }


def expected_protocol() -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema_version": 1,
        "campaign": {
            "campaign_id": "phase4_confirmatory_corpus_v2",
            "preserves_v1_negative_matrix": True,
            "does_not_supersede_v1_decision": True,
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "heldout_outcomes_observed": False,
        },
        "base_decision_contract": {
            "protocol_schema_version": 4,
            "protocol_artifact_checksum": protocol_v4.expected_protocol()["artifact_checksum"],
            "inherited_sections": [
                "outcome",
                "inference",
                "guardrail_thresholds",
                "timing_statistics",
                "completion_requirements",
            ],
        },
        "authority_binding": {
            "representative_manifest_schema_version": 2,
            "corpus_version": authorities._CORPUS_VERSION,
            "corpus_checksum": authorities._CORPUS_CHECKSUM,
            "representative_manifest_checksum": (authorities._REPRESENTATIVE_MANIFEST_CHECKSUM),
            "workload_roster_manifest_checksum": authorities._ROSTER_MANIFEST_CHECKSUM,
            "canonical_budget_roster_schema_version": 2,
            "canonical_budget_cell_count": 102,
            "canonical_budget_roster_checksum": authorities._BUDGET_ROSTER_CHECKSUM,
        },
        "observation_firewall": {
            "development_case_ids": [
                10100,
                10101,
                10102,
                10200,
                10201,
                10210,
                10211,
                10220,
                10221,
            ],
            "heldout_case_ids": (
                list(range(11000, 11008)) + list(range(11100, 11108)) + list(range(11200, 11208))
            ),
            "heldout_execution_requires_this_protocol_commit": True,
            "evidence_source_tree_must_be_clean": True,
            "premature_heldout_observation_invalidates_roster": True,
        },
        "matrix": {
            "logical_cell_count": 104,
            "noncalibration_closure_cell_count": 86,
            "confirmatory_raw_success_cell_count": 100,
            "noncalibration_confirmatory_raw_success_cell_count": 82,
            "ordinary_raw_success_cell_count": 22,
            "same_run_raw_success_cell_count": 78,
            "same_run_guardrail_cell_count": 78,
            "role_cell_counts": [
                {"role": "exact", "count": 3},
                {"role": "calibration", "count": 18},
                {"role": "heldout", "count": 72},
                {"role": "fixed_query", "count": 5},
                {"role": "stress", "count": 3},
                {"role": "imported", "count": 3},
            ],
            "cell_groups": _cell_groups(),
        },
        "families": [
            {
                "family_id": 0,
                "name": "portal_channels_v2",
                "heldout_case_ids": list(range(11000, 11008)),
                "globally_coupled_conflict_graph": False,
            },
            {
                "family_id": 1,
                "name": "pin_field_crossbar_v2",
                "heldout_case_ids": list(range(11100, 11108)),
                "globally_coupled_conflict_graph": True,
            },
            {
                "family_id": 2,
                "name": "fragmented_maze_v2",
                "heldout_case_ids": list(range(11200, 11208)),
                "globally_coupled_conflict_graph": False,
            },
        ],
        "artifact_authorities": {
            "ordinary_raw_outcome_and_timing": _RAW,
            "same_run_raw_outcome_and_timing": _SAME_RUN_RAW,
            "same_run_exact_rejection_guardrail": _SAME_RUN,
            "ordinary_per_net_diagnostic_join": _REPORT,
            "same_run_per_net_diagnostic_join": _SAME_RUN_REPORT,
            "ordinary_operational_measurement": _OPERATIONAL,
            "same_run_operational_measurement": _SAME_RUN_OPERATIONAL,
            "exact_small_oracle": _ORACLE,
            "fixed_query_control": _FIXED,
            "stress_evidence": _STRESS,
        },
        "unchanged_v1_decision_sections": [
            "outcome",
            "inference",
            "guardrails",
            "timing",
            "completion_requirements",
        ],
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def validate_document(value: Any) -> Mapping[str, Any]:
    protocol_v4.read_protocol()
    authorities.validate_authorities()
    if not isinstance(value, dict):
        raise ConfirmatoryProtocolError("confirmatory protocol must be a JSON object")
    protocol_v1._check_depth(value)
    expected = expected_protocol()
    if not protocol_v1._exact_equal(value, expected):
        raise ConfirmatoryProtocolError(
            "protocol does not exactly reconstruct the frozen confirmatory literal"
        )
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ConfirmatoryProtocolError(
            "artifact_checksum does not authenticate the confirmatory protocol"
        )

    cells = _expand_groups(value["matrix"]["cell_groups"])
    if cells != expanded_cells() or len(cells) != 104 or len(set(cells)) != 104:
        raise ConfirmatoryProtocolError(
            "confirmatory protocol does not expand to 104 unique logical cells"
        )
    if sum(row[3] == "confirmatory_raw_success" for row in cells) != 100:
        raise ConfirmatoryProtocolError(
            "confirmatory protocol does not contain 100 successful cells"
        )
    if sum(row[2] != "calibration" for row in cells) != 86:
        raise ConfirmatoryProtocolError("confirmatory protocol does not contain 86 closure cells")
    v1_case_ids = {case_id for case_id, _, _, _ in protocol_v4.expanded_cells()}
    if v1_case_ids & {case_id for case_id, _, _, _ in cells}:
        raise ConfirmatoryProtocolError("V1 and V2 logical case identities overlap")

    ordinary_raw_cells = 0
    same_run_raw_cells = 0
    for group in value["matrix"]["cell_groups"]:
        count = len(_expand_groups([group]))
        successful = group["evidence_requirement"] == "confirmatory_raw_success"
        artifacts = group["required_artifacts"]
        decision_group = group["group"] in {"exact", "heldout", "imported"}
        ordinary_authorities = (_RAW, _REPORT, _OPERATIONAL)
        same_run_authorities = (
            _SAME_RUN_RAW,
            _SAME_RUN,
            _SAME_RUN_REPORT,
            _SAME_RUN_OPERATIONAL,
        )
        if successful and decision_group:
            if any(artifact in artifacts for artifact in ordinary_authorities):
                raise ConfirmatoryProtocolError(
                    "same-run decision group mixes ordinary successful-cell authorities"
                )
            if any(artifacts.count(artifact) != 1 for artifact in same_run_authorities):
                raise ConfirmatoryProtocolError(
                    "same-run decision group lacks one complete same-run authority chain"
                )
            same_run_raw_cells += count
        elif successful:
            if any(artifact in artifacts for artifact in same_run_authorities):
                raise ConfirmatoryProtocolError(
                    "ordinary successful group mixes same-run authorities"
                )
            if any(artifacts.count(artifact) != 1 for artifact in ordinary_authorities):
                raise ConfirmatoryProtocolError(
                    "ordinary successful group lacks one complete ordinary authority chain"
                )
            ordinary_raw_cells += count
        elif any(artifact in artifacts for artifact in ordinary_authorities + same_run_authorities):
            raise ConfirmatoryProtocolError(
                "non-success group requires a successful-cell authority"
            )
        if (group["group"] == "exact") != (_ORACLE in artifacts):
            raise ConfirmatoryProtocolError("exact Oracle authority placement drifted")
    if ordinary_raw_cells != 22 or same_run_raw_cells != 78:
        raise ConfirmatoryProtocolError("confirmatory artifact authority counts drifted")
    if effective_decision_sections() != {
        key: protocol_v1.expected_protocol()[key]
        for key in (
            "outcome",
            "inference",
            "guardrails",
            "timing",
            "completion_requirements",
        )
    }:
        raise ConfirmatoryProtocolError("inherited V1 decision sections drifted")
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, item in pairs:
        if key in result:
            raise ConfirmatoryProtocolError(f"duplicate JSON key: {key}")
        result[key] = item
    return result


def _reject_constant(value: str) -> None:
    raise ConfirmatoryProtocolError(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise ConfirmatoryProtocolError(f"cannot read confirmatory protocol: {error}") from error
    if len(data) > _MAX_BYTES:
        raise ConfirmatoryProtocolError("confirmatory protocol exceeds 256 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ConfirmatoryProtocolError(
            "confirmatory protocol must be UTF-8 without BOM and end in one LF"
        )
    try:
        text = data.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ConfirmatoryProtocolError(f"invalid confirmatory protocol JSON: {error}") from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ConfirmatoryProtocolError("confirmatory protocol is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except (
        ConfirmatoryProtocolError,
        authorities.AuthorityError,
        protocol_v4.ProtocolV4Error,
        protocol_v3.ProtocolV3Error,
        protocol_v2.ProtocolV2Error,
        protocol_v1.ProtocolError,
        raw_validator.EvidenceError,
    ) as error:
        print(f"phase4 confirmatory protocol validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 V2 confirmatory decision protocol validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
