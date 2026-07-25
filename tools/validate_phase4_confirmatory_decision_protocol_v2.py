"""Validate the acquisition-free Phase 4 confirmatory H=4096 protocol."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
from tools import validate_phase4_confirmatory_decision_protocol as protocol_v1
from tools import validate_phase4_statistical_protocol as statistical_v1
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_confirmatory_decision_protocol_v2.json"
_MAX_BYTES = 64 * 1024

_ARTIFACT_SUBSTITUTIONS = (
    (
        "ordinary_raw_outcome_and_timing",
        "phase4_confirmatory_raw_evidence_v1",
        "phase4_confirmatory_raw_evidence_v2",
    ),
    (
        "same_run_raw_outcome_and_timing",
        "phase4_confirmatory_same_run_raw_evidence_v1",
        "phase4_confirmatory_same_run_raw_evidence_v2",
    ),
    (
        "same_run_exact_rejection_guardrail",
        "phase4_confirmatory_same_run_decision_telemetry_v1",
        "phase4_confirmatory_same_run_decision_telemetry_v2",
    ),
    (
        "ordinary_per_net_diagnostic_join",
        "phase4_confirmatory_per_net_report_publication_join_v1",
        "phase4_confirmatory_per_net_report_publication_join_v2",
    ),
    (
        "same_run_per_net_diagnostic_join",
        "phase4_confirmatory_same_run_per_net_report_publication_join_v1",
        "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
    ),
    (
        "ordinary_operational_measurement",
        "phase4_confirmatory_operational_measurement_publication_v1",
        "phase4_confirmatory_operational_measurement_publication_v2",
    ),
    (
        "same_run_operational_measurement",
        "phase4_confirmatory_same_run_operational_measurement_publication_v1",
        "phase4_confirmatory_same_run_operational_measurement_publication_v2",
    ),
    (
        "exact_small_oracle",
        "phase4_confirmatory_exact_small_oracle_v1",
        "phase4_confirmatory_exact_small_oracle_v2",
    ),
    (
        "fixed_query_control",
        "phase4_confirmatory_fixed_query_control_v1",
        "phase4_confirmatory_fixed_query_control_v2",
    ),
    (
        "stress_evidence",
        "phase4_confirmatory_stress_evidence_v1",
        "phase4_confirmatory_stress_evidence_v2",
    ),
)
_MATRIX_DECISION = "phase4_confirmatory_matrix_decision_publication_v2"
_INITIAL_DEVELOPMENT_CELLS = (
    {
        "case_id": 10100,
        "requested_pool_size": 4,
        "role": "exact",
        "carrier": "same_run",
        "raw_wire_schema_version": 2,
        "raw_authority": "phase4_confirmatory_same_run_raw_evidence_v2",
    },
    {
        "case_id": 10200,
        "requested_pool_size": 8,
        "role": "calibration",
        "carrier": "ordinary",
        "raw_wire_schema_version": 1,
        "raw_authority": "phase4_confirmatory_raw_evidence_v2",
    },
)


class ConfirmatoryProtocolV2Error(ValueError):
    """Stable malformed-confirmatory-protocol-v2 diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V2")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _substitution_rows() -> list[dict[str, str]]:
    return [
        {
            "purpose": purpose,
            "supersedes": supersedes,
            "authority": authority,
        }
        for purpose, supersedes, authority in _ARTIFACT_SUBSTITUTIONS
    ]


def _replacement_map(
    substitutions: Sequence[Mapping[str, Any]],
) -> dict[str, str]:
    replacements: dict[str, str] = {}
    purposes: set[str] = set()
    authorities: set[str] = set()
    for index, substitution in enumerate(substitutions):
        if not isinstance(substitution, dict):
            raise ConfirmatoryProtocolV2Error(f"artifact substitution {index} must be an object")
        if tuple(substitution) != ("purpose", "supersedes", "authority"):
            raise ConfirmatoryProtocolV2Error(
                f"artifact substitution {index} has unexpected fields or order"
            )
        purpose = substitution["purpose"]
        supersedes = substitution["supersedes"]
        authority = substitution["authority"]
        if not all(isinstance(item, str) and item for item in (purpose, supersedes, authority)):
            raise ConfirmatoryProtocolV2Error(
                f"artifact substitution {index} must contain nonempty strings"
            )
        if purpose in purposes or supersedes in replacements or authority in authorities:
            raise ConfirmatoryProtocolV2Error("artifact substitutions must be one-to-one")
        if supersedes == authority:
            raise ConfirmatoryProtocolV2Error("artifact substitution must change authority")
        purposes.add(purpose)
        authorities.add(authority)
        replacements[supersedes] = authority
    return replacements


def _effective_cell_groups(
    substitutions: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    replacements = _replacement_map(substitutions)
    groups = copy.deepcopy(protocol_v1._cell_groups())
    for group in groups:
        group["required_artifacts"] = [
            replacements.get(artifact, artifact) for artifact in group["required_artifacts"]
        ]
    return groups


def effective_cell_groups() -> list[dict[str, Any]]:
    """Return Protocol-v1 groups with only the frozen v2 authority substitution."""
    return _effective_cell_groups(_substitution_rows())


def expanded_cells() -> list[tuple[int, int, str, str]]:
    return protocol_v1._expand_groups(effective_cell_groups())


def effective_decision_sections() -> dict[str, Any]:
    """Return the unchanged decision rules inherited through Protocol v1."""
    return protocol_v1.effective_decision_sections()


def _budget_authority() -> dict[str, Any]:
    return {
        "authority": budget_v3._AUTHORITY,
        "schema_version": budget_v3._SCHEMA_VERSION,
        "corpus_version": budget_v3._CORPUS_VERSION,
        "corpus_checksum": budget_v3._CORPUS_CHECKSUM,
        "representative_manifest_schema_version": (
            budget_v3._REPRESENTATIVE_MANIFEST_SCHEMA_VERSION
        ),
        "representative_manifest_checksum": budget_v3._REPRESENTATIVE_MANIFEST_CHECKSUM,
        "workload_roster_manifest_schema_version": (
            budget_v3._WORKLOAD_ROSTER_MANIFEST_SCHEMA_VERSION
        ),
        "workload_roster_manifest_checksum": budget_v3._WORKLOAD_ROSTER_MANIFEST_CHECKSUM,
        "superseded_roster_schema_version": budget_v3._SUPERSEDED_ROSTER_SCHEMA_VERSION,
        "superseded_roster_checksum": budget_v3._SUPERSEDED_ROSTER_CHECKSUM,
        "configuration_authority": budget_v3._CONFIGURATION_AUTHORITY,
        "configuration": budget_v3._expected_configuration(),
        "cell_count": budget_v3._CELL_COUNT,
        "roster_checksum": budget_v3._ROSTER_CHECKSUM,
    }


def expected_protocol() -> dict[str, Any]:
    prior = protocol_v1.expected_protocol()
    value: dict[str, Any] = {
        "schema_version": 2,
        "supersedes": {
            "schema_version": 1,
            "artifact_checksum": prior["artifact_checksum"],
        },
        "campaign": {
            "campaign_id": prior["campaign"]["campaign_id"],
            "configuration_authority": budget_v3._CONFIGURATION_AUTHORITY,
            "preserves_v1_negative_matrix": True,
            "does_not_supersede_v1_decision": True,
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "heldout_outcomes_observed": False,
            "h4096_allocation_outcomes_observed": False,
            "publishable_h4096_evidence_created": False,
            "authority_only_supersession": True,
            "protocol_alone_authorizes_execution": False,
            "protocol_alone_authorizes_acquisition": False,
        },
        "canonical_algorithm_budget_authority": _budget_authority(),
        "observation_firewall": {
            "development_case_ids": copy.deepcopy(
                prior["observation_firewall"]["development_case_ids"]
            ),
            "heldout_case_ids": copy.deepcopy(prior["observation_firewall"]["heldout_case_ids"]),
            "heldout_execution_closed": True,
            "future_campaign_acquisition_authority_required": True,
            "future_authority_must_bind_clean_stamped_source_commit": True,
            "future_authority_must_bind_protocol_v2_and_roster_v3": True,
            "future_authority_must_bind_complete_execution_and_publication_chain": True,
            "future_publication_source_commit_must_equal_acquisition_commit": True,
            "protocol_and_roster_commit_alone_sufficient_for_heldout": False,
            "cross_configuration_evidence_relabeling_forbidden": True,
            "premature_heldout_observation_invalidates_roster": True,
        },
        "development_execution_plan": {
            "protocol_freeze_includes_entrypoints": False,
            "separately_reviewed_h4096_entrypoints_required": True,
            "initial_development_cells_are_complete_scope": True,
            "initial_development_cells": copy.deepcopy(list(_INITIAL_DEVELOPMENT_CELLS)),
            "closed_execution_roles": [
                "heldout",
                "imported",
                "fixed_query",
                "stress",
                "matrix",
                "decision",
            ],
        },
        "artifact_authority_namespace": {
            "configuration_specific": True,
            "authority_version_does_not_imply_payload_schema_change": True,
            "substitutions": _substitution_rows(),
            "matrix_decision_publication": _MATRIX_DECISION,
        },
        "matrix_authority_counts": {
            "logical_cell_count": 104,
            "confirmatory_raw_success_cell_count": 100,
            "noncalibration_closure_cell_count": 86,
            "noncalibration_confirmatory_raw_success_cell_count": 82,
            "ordinary_raw_success_cell_count": 22,
            "same_run_raw_success_cell_count": 78,
            "same_run_guardrail_cell_count": 78,
        },
        "unchanged_v1_sections": [
            "corpus_case_and_workload_authorities",
            "logical_matrix",
            "cell_roles_and_evidence_dispositions",
            "families",
            "base_decision_contract",
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


def _validate_budget_binding(
    value: Mapping[str, Any],
    roster: Mapping[str, Any],
) -> None:
    authority = value["canonical_algorithm_budget_authority"]
    if authority != _budget_authority():
        raise ConfirmatoryProtocolV2Error(
            "canonical budget authority does not exactly bind H=4096 roster v3"
        )
    for field in (
        "authority",
        "schema_version",
        "corpus_version",
        "corpus_checksum",
        "representative_manifest_schema_version",
        "representative_manifest_checksum",
        "workload_roster_manifest_schema_version",
        "workload_roster_manifest_checksum",
        "configuration_authority",
        "cell_count",
        "roster_checksum",
    ):
        if authority[field] != roster[field]:
            raise ConfirmatoryProtocolV2Error(
                f"canonical budget authority field {field} differs from roster v3"
            )
    if (
        authority["superseded_roster_schema_version"] != roster["supersedes"]["schema_version"]
        or authority["superseded_roster_checksum"] != roster["supersedes"]["roster_checksum"]
        or authority["configuration"] != roster["configuration"]
    ):
        raise ConfirmatoryProtocolV2Error(
            "canonical budget authority ancestry or configuration differs from roster v3"
        )


def _validate_authority_substitution(value: Mapping[str, Any]) -> None:
    namespace = value["artifact_authority_namespace"]
    substitutions = namespace["substitutions"]
    replacements = _replacement_map(substitutions)
    expected_replacements = {
        supersedes: authority for _, supersedes, authority in _ARTIFACT_SUBSTITUTIONS
    }
    if replacements != expected_replacements:
        raise ConfirmatoryProtocolV2Error("artifact authority substitution set drifted")

    prior_groups = protocol_v1._cell_groups()
    groups = _effective_cell_groups(substitutions)
    if len(groups) != len(prior_groups):
        raise ConfirmatoryProtocolV2Error("effective matrix group count drifted")
    for prior, current in zip(prior_groups, groups, strict=True):
        expected = copy.deepcopy(prior)
        expected["required_artifacts"] = [
            replacements.get(artifact, artifact) for artifact in prior["required_artifacts"]
        ]
        if current != expected:
            raise ConfirmatoryProtocolV2Error(
                "Protocol v2 changes a matrix group outside authority substitution"
            )
    flattened = [artifact for group in groups for artifact in group["required_artifacts"]]
    if any(superseded in flattened for superseded in replacements):
        raise ConfirmatoryProtocolV2Error("effective Protocol v2 retains a v1 artifact authority")
    if any(authority not in flattened for authority in replacements.values()):
        raise ConfirmatoryProtocolV2Error("effective Protocol v2 omits a v2 artifact authority")
    if namespace["matrix_decision_publication"] in flattened:
        raise ConfirmatoryProtocolV2Error(
            "matrix decision authority must remain outside per-cell requirements"
        )


def _validate_matrix_and_development_scope(value: Mapping[str, Any]) -> None:
    cells = expanded_cells()
    if cells != protocol_v1.expanded_cells() or len(cells) != 104 or len(set(cells)) != 104:
        raise ConfirmatoryProtocolV2Error(
            "Protocol v2 changes the frozen logical matrix or evidence dispositions"
        )
    base_decision = statistical_v1.expected_protocol()
    if effective_decision_sections() != {
        key: base_decision[key]
        for key in (
            "outcome",
            "inference",
            "guardrails",
            "timing",
            "completion_requirements",
        )
    }:
        raise ConfirmatoryProtocolV2Error("Protocol v2 changes inherited decision sections")

    counts = value["matrix_authority_counts"]
    groups = effective_cell_groups()
    ordinary_raw = _ARTIFACT_SUBSTITUTIONS[0][2]
    same_run_raw = _ARTIFACT_SUBSTITUTIONS[1][2]
    actual_counts = {
        "logical_cell_count": len(cells),
        "confirmatory_raw_success_cell_count": sum(
            disposition == "confirmatory_raw_success" for _, _, _, disposition in cells
        ),
        "noncalibration_closure_cell_count": sum(role != "calibration" for _, _, role, _ in cells),
        "noncalibration_confirmatory_raw_success_cell_count": sum(
            role != "calibration" and disposition == "confirmatory_raw_success"
            for _, _, role, disposition in cells
        ),
        "ordinary_raw_success_cell_count": sum(
            len(protocol_v1._expand_groups([group]))
            for group in groups
            if group["evidence_requirement"] == "confirmatory_raw_success"
            and ordinary_raw in group["required_artifacts"]
        ),
        "same_run_raw_success_cell_count": sum(
            len(protocol_v1._expand_groups([group]))
            for group in groups
            if group["evidence_requirement"] == "confirmatory_raw_success"
            and same_run_raw in group["required_artifacts"]
        ),
        "same_run_guardrail_cell_count": sum(
            len(protocol_v1._expand_groups([group]))
            for group in groups
            if _ARTIFACT_SUBSTITUTIONS[2][2] in group["required_artifacts"]
        ),
    }
    if counts != actual_counts:
        raise ConfirmatoryProtocolV2Error("Protocol v2 matrix authority counts drifted")

    by_cell = {(case_id, pool): (role, disposition) for case_id, pool, role, disposition in cells}
    initial = value["development_execution_plan"]["initial_development_cells"]
    if initial != list(_INITIAL_DEVELOPMENT_CELLS):
        raise ConfirmatoryProtocolV2Error("initial H=4096 development cell scope drifted")
    for cell in initial:
        identity = (cell["case_id"], cell["requested_pool_size"])
        if by_cell.get(identity) != (cell["role"], "confirmatory_raw_success"):
            raise ConfirmatoryProtocolV2Error(
                "initial H=4096 development cell is outside its frozen matrix role"
            )
    heldout = set(value["observation_firewall"]["heldout_case_ids"])
    if any(cell["case_id"] in heldout for cell in initial):
        raise ConfirmatoryProtocolV2Error("initial development scope contains a heldout case")


def validate_document(value: Any) -> Mapping[str, Any]:
    protocol_v1.read_protocol()
    roster = budget_v3.validate_roster()
    if not isinstance(value, dict):
        raise ConfirmatoryProtocolV2Error("confirmatory protocol v2 must be a JSON object")
    statistical_v1._check_depth(value)
    expected = expected_protocol()
    if not statistical_v1._exact_equal(value, expected):
        raise ConfirmatoryProtocolV2Error(
            "protocol does not exactly reconstruct the frozen confirmatory v2 literal"
        )
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ConfirmatoryProtocolV2Error(
            "artifact_checksum does not authenticate confirmatory protocol v2"
        )
    _validate_budget_binding(value, roster)
    _validate_authority_substitution(value)
    _validate_matrix_and_development_scope(value)
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, item in pairs:
        if key in result:
            raise ConfirmatoryProtocolV2Error(f"duplicate JSON key: {key}")
        result[key] = item
    return result


def _reject_constant(value: str) -> None:
    raise ConfirmatoryProtocolV2Error(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise ConfirmatoryProtocolV2Error(
            f"cannot read confirmatory protocol v2: {error}"
        ) from error
    if len(data) > _MAX_BYTES:
        raise ConfirmatoryProtocolV2Error("confirmatory protocol v2 exceeds 64 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ConfirmatoryProtocolV2Error(
            "confirmatory protocol v2 must be UTF-8 without BOM and end in one LF"
        )
    try:
        text = data.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ConfirmatoryProtocolV2Error(
            f"invalid confirmatory protocol v2 JSON: {error}"
        ) from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ConfirmatoryProtocolV2Error("confirmatory protocol v2 is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except ValueError as error:
        print(
            f"phase4 confirmatory protocol v2 validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("phase4 H=4096 confirmatory decision protocol v2 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
