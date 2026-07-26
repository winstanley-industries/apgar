"""Validate the acquisition-free Phase 4 Session-v5 H=4096 protocol."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v4 as budget_v4
from tools import validate_phase4_confirmatory_decision_protocol as protocol_v1
from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_statistical_protocol as statistical_v1
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_confirmatory_decision_protocol_v3.json"
_MAX_BYTES = 64 * 1024
_ARTIFACT_CHECKSUM = 4963299999381388941

_ARTIFACT_SUBSTITUTIONS = (
    (
        "ordinary_raw_outcome_and_timing",
        "phase4_confirmatory_raw_evidence_v2",
        "phase4_confirmatory_raw_evidence_v3",
    ),
    (
        "same_run_raw_outcome_and_timing",
        "phase4_confirmatory_same_run_raw_evidence_v2",
        "phase4_confirmatory_same_run_raw_evidence_v3",
    ),
    (
        "same_run_exact_rejection_guardrail",
        "phase4_confirmatory_same_run_decision_telemetry_v2",
        "phase4_confirmatory_same_run_decision_telemetry_v3",
    ),
    (
        "ordinary_per_net_diagnostic_join",
        "phase4_confirmatory_per_net_report_publication_join_v2",
        "phase4_confirmatory_per_net_report_publication_join_v3",
    ),
    (
        "same_run_per_net_diagnostic_join",
        "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
        "phase4_confirmatory_same_run_per_net_report_publication_join_v3",
    ),
    (
        "ordinary_operational_measurement",
        "phase4_confirmatory_operational_measurement_publication_v2",
        "phase4_confirmatory_operational_measurement_publication_v3",
    ),
    (
        "same_run_operational_measurement",
        "phase4_confirmatory_same_run_operational_measurement_publication_v2",
        "phase4_confirmatory_same_run_operational_measurement_publication_v3",
    ),
    (
        "exact_small_oracle",
        "phase4_confirmatory_exact_small_oracle_v2",
        "phase4_confirmatory_exact_small_oracle_v3",
    ),
    (
        "fixed_query_control",
        "phase4_confirmatory_fixed_query_control_v2",
        "phase4_confirmatory_fixed_query_control_v3",
    ),
    (
        "stress_evidence",
        "phase4_confirmatory_stress_evidence_v2",
        "phase4_confirmatory_stress_evidence_v3",
    ),
)
_MATRIX_DECISION_SUPERSEDES = "phase4_confirmatory_matrix_decision_publication_v2"
_MATRIX_DECISION = "phase4_confirmatory_matrix_decision_publication_v3"
_INITIAL_DEVELOPMENT_CELLS = (
    {
        "case_id": 10100,
        "requested_pool_size": 4,
        "role": "exact",
        "carrier": "same_run",
        "raw_wire_schema_version": 2,
        "raw_authority": "phase4_confirmatory_same_run_raw_evidence_v3",
    },
    {
        "case_id": 10200,
        "requested_pool_size": 8,
        "role": "calibration",
        "carrier": "ordinary",
        "raw_wire_schema_version": 1,
        "raw_authority": "phase4_confirmatory_raw_evidence_v3",
    },
)


class ConfirmatoryProtocolV3Error(ValueError):
    """Stable malformed-confirmatory-protocol-v3 diagnostic."""


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V3")
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
            raise ConfirmatoryProtocolV3Error(f"artifact substitution {index} must be an object")
        if tuple(substitution) != ("purpose", "supersedes", "authority"):
            raise ConfirmatoryProtocolV3Error(
                f"artifact substitution {index} has unexpected fields or order"
            )
        purpose = substitution["purpose"]
        supersedes = substitution["supersedes"]
        authority = substitution["authority"]
        if not all(isinstance(item, str) and item for item in (purpose, supersedes, authority)):
            raise ConfirmatoryProtocolV3Error(
                f"artifact substitution {index} must contain nonempty strings"
            )
        if purpose in purposes or supersedes in replacements or authority in authorities:
            raise ConfirmatoryProtocolV3Error("artifact substitutions must be one-to-one")
        if supersedes == authority:
            raise ConfirmatoryProtocolV3Error("artifact substitution must change authority")
        purposes.add(purpose)
        authorities.add(authority)
        replacements[supersedes] = authority
    return replacements


def _effective_cell_groups(
    substitutions: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    replacements = _replacement_map(substitutions)
    groups = copy.deepcopy(protocol_v2.effective_cell_groups())
    for group in groups:
        group["required_artifacts"] = [
            replacements.get(artifact, artifact) for artifact in group["required_artifacts"]
        ]
    return groups


def effective_cell_groups() -> list[dict[str, Any]]:
    """Return Protocol-v2 groups with only the frozen v3 authority substitution."""
    return _effective_cell_groups(_substitution_rows())


def expanded_cells() -> list[tuple[int, int, str, str]]:
    return protocol_v1._expand_groups(effective_cell_groups())


def effective_decision_sections() -> dict[str, Any]:
    """Return the unchanged decision rules inherited through Protocol v2."""
    return protocol_v2.effective_decision_sections()


def _budget_authority() -> dict[str, Any]:
    return {
        "authority": budget_v4._AUTHORITY,
        "schema_version": budget_v4._SCHEMA_VERSION,
        "corpus_version": budget_v4._CORPUS_VERSION,
        "corpus_checksum": budget_v4._CORPUS_CHECKSUM,
        "representative_manifest_schema_version": (
            budget_v4._REPRESENTATIVE_MANIFEST_SCHEMA_VERSION
        ),
        "representative_manifest_checksum": budget_v4._REPRESENTATIVE_MANIFEST_CHECKSUM,
        "workload_roster_manifest_schema_version": (
            budget_v4._WORKLOAD_ROSTER_MANIFEST_SCHEMA_VERSION
        ),
        "workload_roster_manifest_checksum": budget_v4._WORKLOAD_ROSTER_MANIFEST_CHECKSUM,
        "superseded_roster_schema_version": budget_v4._SUPERSEDED_ROSTER_SCHEMA_VERSION,
        "superseded_roster_authority": budget_v4._SUPERSEDED_ROSTER_AUTHORITY,
        "superseded_roster_checksum": budget_v4._SUPERSEDED_ROSTER_CHECKSUM,
        "superseded_configuration_authority": (budget_v4._SUPERSEDED_CONFIGURATION_AUTHORITY),
        "configuration_authority": budget_v4._CONFIGURATION_AUTHORITY,
        "configuration": budget_v4._expected_configuration(),
        "cell_count": budget_v4._CELL_COUNT,
        "roster_checksum": budget_v4._ROSTER_CHECKSUM,
    }


def expected_protocol() -> dict[str, Any]:
    prior = protocol_v2.expected_protocol()
    value: dict[str, Any] = {
        "schema_version": 3,
        "supersedes": {
            "schema_version": 2,
            "artifact_checksum": prior["artifact_checksum"],
        },
        "campaign": {
            "campaign_id": prior["campaign"]["campaign_id"],
            "configuration_authority": budget_v4._CONFIGURATION_AUTHORITY,
            "preserves_v1_negative_matrix": True,
            "preserves_v2_development_observations": True,
            "does_not_supersede_v1_decision": True,
        },
        "protocol_state": {
            "protocol_frozen": True,
            "decision_not_evaluated": True,
            "heldout_outcomes_observed": False,
            "protocol_v2_development_outcomes_observed": True,
            "session_v5_h4096_allocation_outcomes_observed": False,
            "publishable_session_v5_h4096_evidence_created": False,
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
            "future_authority_must_bind_protocol_v3_and_roster_v4": True,
            "future_authority_must_bind_complete_execution_and_publication_chain": True,
            "future_publication_source_commit_must_equal_acquisition_commit": True,
            "protocol_and_roster_commit_alone_sufficient_for_heldout": False,
            "cross_configuration_evidence_relabeling_forbidden": True,
            "protocol_v2_evidence_cannot_satisfy_protocol_v3": True,
            "premature_session_v5_h4096_heldout_observation_invalidates_roster": True,
        },
        "development_execution_plan": {
            "protocol_freeze_includes_entrypoints": False,
            "separately_reviewed_session_v5_h4096_entrypoints_required": True,
            "initial_development_cells_are_complete_scope": True,
            "initial_development_cells": copy.deepcopy(list(_INITIAL_DEVELOPMENT_CELLS)),
            "closed_execution_roles": copy.deepcopy(
                prior["development_execution_plan"]["closed_execution_roles"]
            ),
        },
        "artifact_authority_namespace": {
            "configuration_specific": True,
            "authority_version_does_not_imply_payload_schema_change": True,
            "substitutions": _substitution_rows(),
            "matrix_decision_publication_supersedes": _MATRIX_DECISION_SUPERSEDES,
            "matrix_decision_publication": _MATRIX_DECISION,
        },
        "matrix_authority_counts": copy.deepcopy(prior["matrix_authority_counts"]),
        "unchanged_v1_sections": copy.deepcopy(prior["unchanged_v1_sections"]),
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def _validate_protocol_ancestry(
    value: Mapping[str, Any],
    prior: Mapping[str, Any],
) -> None:
    if value["supersedes"] != {
        "schema_version": prior["schema_version"],
        "artifact_checksum": prior["artifact_checksum"],
    }:
        raise ConfirmatoryProtocolV3Error(
            "Protocol v3 does not exactly bind checked-in Protocol v2"
        )
    campaign = value["campaign"]
    if (
        campaign["campaign_id"] != prior["campaign"]["campaign_id"]
        or campaign["configuration_authority"] != budget_v4._CONFIGURATION_AUTHORITY
        or campaign["preserves_v1_negative_matrix"] is not True
        or campaign["preserves_v2_development_observations"] is not True
        or campaign["does_not_supersede_v1_decision"] is not True
    ):
        raise ConfirmatoryProtocolV3Error("Protocol v3 campaign ancestry drifted")


def _validate_budget_binding(
    value: Mapping[str, Any],
    roster: Mapping[str, Any],
) -> None:
    authority = value["canonical_algorithm_budget_authority"]
    if authority != _budget_authority():
        raise ConfirmatoryProtocolV3Error(
            "canonical budget authority does not exactly bind Session-v5 roster v4"
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
            raise ConfirmatoryProtocolV3Error(
                f"canonical budget authority field {field} differs from roster v4"
            )
    supersedes = roster["supersedes"]
    if (
        authority["superseded_roster_schema_version"] != supersedes["schema_version"]
        or authority["superseded_roster_authority"] != supersedes["authority"]
        or authority["superseded_roster_checksum"] != supersedes["roster_checksum"]
        or authority["superseded_configuration_authority"] != supersedes["configuration_authority"]
        or authority["configuration"] != roster["configuration"]
    ):
        raise ConfirmatoryProtocolV3Error(
            "canonical budget authority ancestry or configuration differs from roster v4"
        )


def _validate_authority_substitution(
    value: Mapping[str, Any],
    prior: Mapping[str, Any],
) -> None:
    namespace = value["artifact_authority_namespace"]
    substitutions = namespace["substitutions"]
    replacements = _replacement_map(substitutions)
    expected_replacements = {
        supersedes: authority for _, supersedes, authority in _ARTIFACT_SUBSTITUTIONS
    }
    if replacements != expected_replacements:
        raise ConfirmatoryProtocolV3Error("artifact authority substitution set drifted")

    predecessor_rows = prior["artifact_authority_namespace"]["substitutions"]
    if len(predecessor_rows) != len(substitutions):
        raise ConfirmatoryProtocolV3Error("artifact authority predecessor row count drifted")
    for predecessor, successor in zip(predecessor_rows, substitutions, strict=True):
        if (
            successor["purpose"] != predecessor["purpose"]
            or successor["supersedes"] != predecessor["authority"]
        ):
            raise ConfirmatoryProtocolV3Error(
                "Protocol v3 substitution does not exactly advance its Protocol-v2 row"
            )

    prior_groups = protocol_v2.effective_cell_groups()
    groups = _effective_cell_groups(substitutions)
    if len(groups) != len(prior_groups):
        raise ConfirmatoryProtocolV3Error("effective matrix group count drifted")
    for predecessor, current in zip(prior_groups, groups, strict=True):
        expected = copy.deepcopy(predecessor)
        expected["required_artifacts"] = [
            replacements.get(artifact, artifact) for artifact in predecessor["required_artifacts"]
        ]
        if current != expected:
            raise ConfirmatoryProtocolV3Error(
                "Protocol v3 changes a matrix group outside authority substitution"
            )

    flattened = [artifact for group in groups for artifact in group["required_artifacts"]]
    if any(superseded in flattened for superseded in replacements):
        raise ConfirmatoryProtocolV3Error("effective Protocol v3 retains a v2 artifact authority")
    if any(authority not in flattened for authority in replacements.values()):
        raise ConfirmatoryProtocolV3Error("effective Protocol v3 omits a v3 artifact authority")
    if (
        namespace["matrix_decision_publication_supersedes"]
        != prior["artifact_authority_namespace"]["matrix_decision_publication"]
        or namespace["matrix_decision_publication"] != _MATRIX_DECISION
    ):
        raise ConfirmatoryProtocolV3Error("matrix decision publication ancestry drifted")
    if namespace["matrix_decision_publication"] in flattened:
        raise ConfirmatoryProtocolV3Error(
            "matrix decision authority must remain outside per-cell requirements"
        )


def _validate_matrix_and_development_scope(
    value: Mapping[str, Any],
    prior: Mapping[str, Any],
) -> None:
    cells = expanded_cells()
    if cells != protocol_v2.expanded_cells() or len(cells) != 104 or len(set(cells)) != 104:
        raise ConfirmatoryProtocolV3Error(
            "Protocol v3 changes the frozen logical matrix or evidence dispositions"
        )
    if effective_decision_sections() != protocol_v2.effective_decision_sections():
        raise ConfirmatoryProtocolV3Error("Protocol v3 changes inherited decision sections")

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
    if counts != actual_counts or counts != prior["matrix_authority_counts"]:
        raise ConfirmatoryProtocolV3Error("Protocol v3 matrix authority counts drifted")
    if value["unchanged_v1_sections"] != prior["unchanged_v1_sections"]:
        raise ConfirmatoryProtocolV3Error("Protocol v3 inherited decision-section roster drifted")

    by_cell = {(case_id, pool): (role, disposition) for case_id, pool, role, disposition in cells}
    initial = value["development_execution_plan"]["initial_development_cells"]
    if initial != list(_INITIAL_DEVELOPMENT_CELLS):
        raise ConfirmatoryProtocolV3Error("initial Session-v5 development cell scope drifted")
    for cell in initial:
        identity = (cell["case_id"], cell["requested_pool_size"])
        if by_cell.get(identity) != (cell["role"], "confirmatory_raw_success"):
            raise ConfirmatoryProtocolV3Error(
                "initial Session-v5 development cell is outside its frozen matrix role"
            )

    firewall = value["observation_firewall"]
    prior_firewall = prior["observation_firewall"]
    if (
        firewall["development_case_ids"] != prior_firewall["development_case_ids"]
        or firewall["heldout_case_ids"] != prior_firewall["heldout_case_ids"]
    ):
        raise ConfirmatoryProtocolV3Error("Protocol v3 observation-firewall case rosters drifted")
    heldout = set(firewall["heldout_case_ids"])
    if any(cell["case_id"] in heldout for cell in initial):
        raise ConfirmatoryProtocolV3Error("initial development scope contains a heldout case")

    state = value["protocol_state"]
    if (
        state["protocol_v2_development_outcomes_observed"] is not True
        or state["session_v5_h4096_allocation_outcomes_observed"] is not False
        or state["publishable_session_v5_h4096_evidence_created"] is not False
        or state["protocol_alone_authorizes_execution"] is not False
        or state["protocol_alone_authorizes_acquisition"] is not False
    ):
        raise ConfirmatoryProtocolV3Error("Protocol v3 observation or authority state drifted")


def validate_document(value: Any) -> Mapping[str, Any]:
    prior = protocol_v2.read_protocol()
    roster = budget_v4.validate_roster()
    if not isinstance(value, dict):
        raise ConfirmatoryProtocolV3Error("confirmatory protocol v3 must be a JSON object")
    statistical_v1._check_depth(value)
    expected = expected_protocol()
    if not statistical_v1._exact_equal(value, expected):
        raise ConfirmatoryProtocolV3Error(
            "protocol does not exactly reconstruct the frozen confirmatory v3 literal"
        )
    if value["artifact_checksum"] != _ARTIFACT_CHECKSUM:
        raise ConfirmatoryProtocolV3Error(
            "artifact_checksum differs from the frozen confirmatory v3 authority"
        )
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ConfirmatoryProtocolV3Error(
            "artifact_checksum does not authenticate confirmatory protocol v3"
        )
    _validate_protocol_ancestry(value, prior)
    _validate_budget_binding(value, roster)
    _validate_authority_substitution(value, prior)
    _validate_matrix_and_development_scope(value, prior)
    return value


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, item in pairs:
        if key in result:
            raise ConfirmatoryProtocolV3Error(f"duplicate JSON key: {key}")
        result[key] = item
    return result


def _reject_constant(value: str) -> None:
    raise ConfirmatoryProtocolV3Error(f"non-finite JSON number: {value}")


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise ConfirmatoryProtocolV3Error(
            f"cannot read confirmatory protocol v3: {error}"
        ) from error
    if len(data) > _MAX_BYTES:
        raise ConfirmatoryProtocolV3Error("confirmatory protocol v3 exceeds 64 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ConfirmatoryProtocolV3Error(
            "confirmatory protocol v3 must be UTF-8 without BOM and end in one LF"
        )
    try:
        text = data.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ConfirmatoryProtocolV3Error(
            f"invalid confirmatory protocol v3 JSON: {error}"
        ) from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ConfirmatoryProtocolV3Error("confirmatory protocol v3 is not canonical compact JSON")
    return validated


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except ValueError as error:
        print(
            f"phase4 confirmatory protocol v3 validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("phase4 Session-v5 H=4096 confirmatory decision protocol v3 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
