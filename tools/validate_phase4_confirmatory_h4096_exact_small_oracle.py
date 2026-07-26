"""Publish the frozen H=4096 confirmatory exact-small oracle development cell."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import (
    validate_phase4_confirmatory_h4096_same_run_decision_telemetry as h4096_telemetry,
)
from tools import (
    validate_phase4_confirmatory_h4096_same_run_per_net_report as h4096_report_join,
)
from tools import validate_phase4_exact_small_oracle as oracle
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

EvidenceError = raw_validator.EvidenceError

_CAMPAIGN = "phase4_confirmatory_corpus_v2"
_RAW_AUTHORITY = "phase4_confirmatory_same_run_raw_evidence_v2"
_TELEMETRY_AUTHORITY = "phase4_confirmatory_same_run_decision_telemetry_v2"
_REPORT_AUTHORITY = "phase4_confirmatory_same_run_per_net_report_publication_join_v2"
_SNAPSHOT_AUTHORITY = "phase4_exact_small_snapshot_v1"
_REPLAY_TARGET = "phase4_confirmatory_h4096_exact_small_candidate_admission_replay"
_PUBLICATION_AUTHORITY = {
    "purpose": "exact_small_oracle",
    "supersedes": "phase4_confirmatory_exact_small_oracle_v1",
    "authority": "phase4_confirmatory_exact_small_oracle_v2",
}
_CORPUS_VERSION = 2
_PRIVATE_WIRE_VERSION = 3
_CORPUS_CHECKSUM = 4182833841936446798
_CANONICAL_ALGORITHM_BUDGET_CHECKSUM = 8829615204625848656
_PAIRED_SEMANTIC_BUDGET_CHECKSUM = 5851813264366095594
_CASE_ID = 10100
_POOL_SIZE = 4
_MAXIMUM_OUTPUT_BYTES = 1024 * 1024
_FROZEN_CONFIG = {
    "schema_version": 1,
    "case_id": _CASE_ID,
    "requested_pool_size": _POOL_SIZE,
    "preparation_worker_count": 4,
    "repetitions": 20,
    "maximum_setup_elapsed_nanoseconds": 300_000_000_000,
    "external_budget": {
        "maximum_prepared_elapsed_nanoseconds": 300_000_000_000,
        "maximum_cold_elapsed_nanoseconds": 300_000_000_000,
        "maximum_address_space_bytes": 68_719_476_736,
        "maximum_peak_host_bytes": 17_179_869_184,
    },
    "corpus_limits": {
        "maximum_nets": 4_096,
        "maximum_compiled_nodes": 100_000_000,
        "maximum_compiled_host_bytes": 8_589_934_592,
        "maximum_active_regions": 250_000,
        "maximum_board_entities": 100_000,
    },
}

_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "campaign_id",
    "cell_role",
    "eligible_input_to_phase4_aggregation",
    "standalone_decision_eligible",
    "statistical_timing_eligible",
    "coverage_complete",
    "exact_small_oracle_complete",
    "config",
    "corpus_version",
    "corpus_checksum",
    "raw_binding",
    "same_run_telemetry_binding",
    "per_net_report_binding",
    "snapshot_binding",
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
_RAW_BINDING_FIELDS = (
    "authority",
    "raw_evidence_schema_version",
    "wire_schema_version",
    "cell_plan_checksum",
    "artifact_checksum",
    "source_envelope_checksum",
)
_TELEMETRY_BINDING_FIELDS = (
    "authority",
    "schema_version",
    "raw_evidence_schema_version",
    "raw_wire_schema_version",
    "telemetry_wire_schema_version",
    "raw_artifact_checksum",
    "raw_source_envelope_checksum",
    "artifact_checksum",
    "source_envelope_checksum",
    "exact_rejection_guardrail_passed",
)
_REPORT_BINDING_FIELDS = (
    "authority",
    "schema_version",
    "corpus_version",
    "raw_wire_schema_version",
    "raw_artifact_checksum",
    "raw_source_envelope_checksum",
    "artifact_checksum",
    "source_envelope_checksum",
)
_SNAPSHOT_BINDING_FIELDS = (
    "authority",
    "schema_version",
    "corpus_version",
    "raw_artifact_checksum",
    "raw_source_envelope_checksum",
    "per_net_report_artifact_checksum",
    "per_net_report_source_envelope_checksum",
    "artifact_checksum",
    "source_envelope_checksum",
)
_OBJECTIVE_FIELDS = (
    "selected_net_count",
    "total_overuse_units",
    "total_intrinsic_base_cost",
)


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _require_publication_authority() -> None:
    try:
        protocol = protocol_v2.read_protocol()
        roster = budget_v3.validate_roster()
    except (OSError, ValueError) as error:
        raise EvidenceError(
            f"cannot authenticate frozen H=4096 exact-small authority: {error}"
        ) from error

    substitutions = [
        row
        for row in protocol["artifact_authority_namespace"]["substitutions"]
        if row["purpose"] == _PUBLICATION_AUTHORITY["purpose"]
    ]
    if substitutions != [_PUBLICATION_AUTHORITY]:
        raise EvidenceError("frozen H=4096 exact-small publication authority drifted")

    protocol_budget = protocol["canonical_algorithm_budget_authority"]
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
        if protocol_budget[field] != roster[field]:
            raise EvidenceError(
                f"H=4096 exact-small Protocol-v2 budget field {field} differs from roster v3"
            )
    if (
        protocol_budget["superseded_roster_schema_version"]
        != roster["supersedes"]["schema_version"]
        or protocol_budget["superseded_roster_checksum"] != roster["supersedes"]["roster_checksum"]
        or protocol_budget["configuration"] != roster["configuration"]
        or protocol["campaign"]["configuration_authority"] != roster["configuration_authority"]
        or budget_v3.budget_map(roster).get((_CASE_ID, _POOL_SIZE))
        != _CANONICAL_ALGORITHM_BUDGET_CHECKSUM
    ):
        raise EvidenceError(
            "H=4096 exact-small Protocol-v2 budget ancestry or exact-cell authority drifted"
        )


def compute_artifact_checksum(artifact: Mapping[str, Any]) -> int:
    payload = {field: artifact[field] for field in _FIELDS[4:-1]}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-ARTIFACT-V2")
    hashed.string(_canonical(payload))
    return hashed.finish()


def compute_source_envelope_checksum(artifact: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-SOURCE-V2")
    hashed.string(artifact["source_commit"])
    hashed.boolean(artifact["source_stamped"])
    hashed.boolean(artifact["source_tree_dirty"])
    hashed.u64(artifact["artifact_checksum"])
    return hashed.finish()


def _require_scope(raw: Mapping[str, Any]) -> None:
    config = report_validator.validate_config(raw["config"])
    if (
        raw.get("raw_evidence_schema_version") != 2
        or raw.get("wire_schema_version") != 2
        or config != _FROZEN_CONFIG
    ):
        raise EvidenceError(
            "H=4096 exact-small oracle authority is restricted to Raw/Wire 2 "
            "and the frozen (10100,4) configuration"
        )
    for index, attempt_value in enumerate(raw["attempts"]):
        attempt = oracle._object(attempt_value, f"H=4096 Raw attempt[{index}]")
        candidate = oracle._object(
            attempt["candidate"],
            f"H=4096 Raw attempt[{index}].candidate",
        )
        record = oracle._object(
            candidate["record"],
            f"H=4096 Raw attempt[{index}].candidate.record",
        )
        semantics = oracle._object(
            record["semantics"],
            f"H=4096 Raw attempt[{index}].candidate.record.semantics",
        )
        if (
            oracle._u64(
                semantics["budget_checksum"],
                f"H=4096 Raw attempt[{index}].candidate.record.semantics.budget_checksum",
            )
            != _PAIRED_SEMANTIC_BUDGET_CHECKSUM
        ):
            raise EvidenceError("H=4096 candidate semantic budget differs from the frozen cell")


def _validate_authorities(
    raw_value: Any,
    sidecar_value: Any,
    report_value: Any,
    *,
    expected_commit: str,
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]:
    _require_publication_authority()
    h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
        raw_value,
        expected_commit=expected_commit,
    )
    raw = oracle._object(raw_value, "H=4096 exact-small Raw input")
    _require_scope(raw)
    sidecar = h4096_telemetry.validate_confirmatory_h4096_join(
        raw,
        sidecar_value,
        expected_commit=expected_commit,
    )
    h4096_report_join.validate_join(
        raw,
        sidecar,
        report_value,
        expected_commit=expected_commit,
    )
    report = oracle._object(report_value, "H=4096 exact-small per-net report")
    return raw, sidecar, report


def _build_artifact(
    raw: Mapping[str, Any],
    sidecar: Mapping[str, Any],
    report: Mapping[str, Any],
    snapshot: Any,
    *,
    expected_commit: str,
) -> dict[str, Any]:
    snapshot_document = oracle._object(snapshot, "H=4096 exact-small snapshot")
    if (
        oracle._u64(
            snapshot_document.get("budget_checksum"),
            "H=4096 exact-small snapshot.budget_checksum",
        )
        != _PAIRED_SEMANTIC_BUDGET_CHECKSUM
    ):
        raise EvidenceError("H=4096 snapshot budget differs from the frozen paired semantics")
    base = oracle.validate_publication_against_validated_authorities(
        raw,
        report,
        snapshot_document,
        expected_commit=expected_commit,
        corpus_version=_CORPUS_VERSION,
        allowed_case_ids=frozenset({_CASE_ID}),
        private_wire_version=_PRIVATE_WIRE_VERSION,
        admission_replay_target=_REPLAY_TARGET,
    )
    artifact: dict[str, Any] = {
        "source_commit": expected_commit,
        "source_stamped": True,
        "source_tree_dirty": False,
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "campaign_id": _CAMPAIGN,
        "cell_role": "exact",
        "eligible_input_to_phase4_aggregation": True,
        "standalone_decision_eligible": False,
        "statistical_timing_eligible": False,
        "coverage_complete": False,
        "exact_small_oracle_complete": True,
        "config": raw["config"],
        "corpus_version": _CORPUS_VERSION,
        "corpus_checksum": raw["corpus_checksum"],
        "raw_binding": {
            "authority": _RAW_AUTHORITY,
            "raw_evidence_schema_version": raw["raw_evidence_schema_version"],
            "wire_schema_version": raw["wire_schema_version"],
            "cell_plan_checksum": raw["cell_plan_checksum"],
            "artifact_checksum": raw["artifact_checksum"],
            "source_envelope_checksum": raw["source_envelope_checksum"],
        },
        "same_run_telemetry_binding": {
            "authority": _TELEMETRY_AUTHORITY,
            "schema_version": sidecar["schema_version"],
            "raw_evidence_schema_version": sidecar["raw_evidence_schema_version"],
            "raw_wire_schema_version": sidecar["raw_wire_schema_version"],
            "telemetry_wire_schema_version": sidecar["telemetry_wire_schema_version"],
            "raw_artifact_checksum": sidecar["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": sidecar["raw_source_envelope_checksum"],
            "artifact_checksum": sidecar["artifact_checksum"],
            "source_envelope_checksum": sidecar["source_envelope_checksum"],
            "exact_rejection_guardrail_passed": (
                telemetry_validator.exact_rejection_guardrail_passes(sidecar)
            ),
        },
        "per_net_report_binding": {
            "authority": _REPORT_AUTHORITY,
            "schema_version": report["schema_version"],
            "corpus_version": _CORPUS_VERSION,
            "raw_wire_schema_version": report["raw_wire_schema_version"],
            "raw_artifact_checksum": report["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": report["raw_source_envelope_checksum"],
            "artifact_checksum": report["artifact_checksum"],
            "source_envelope_checksum": report["source_envelope_checksum"],
        },
        "snapshot_binding": {
            "authority": _SNAPSHOT_AUTHORITY,
            "schema_version": snapshot_document["schema_version"],
            "corpus_version": _CORPUS_VERSION,
            "raw_artifact_checksum": snapshot_document["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": snapshot_document["raw_source_envelope_checksum"],
            "per_net_report_artifact_checksum": snapshot_document[
                "per_net_report_artifact_checksum"
            ],
            "per_net_report_source_envelope_checksum": snapshot_document[
                "per_net_report_source_envelope_checksum"
            ],
            "artifact_checksum": snapshot_document["artifact_checksum"],
            "source_envelope_checksum": snapshot_document["source_envelope_checksum"],
        },
        "candidate_semantic_checksum": base["candidate_semantic_checksum"],
        "cartesian_product": base["cartesian_product"],
        "production_objective": base["production_objective"],
        "optimum_objective": base["optimum_objective"],
        "production_overused_resource_count": base["production_overused_resource_count"],
        "canonical_witness_overused_resource_count": base[
            "canonical_witness_overused_resource_count"
        ],
        "production_is_optimal": base["production_is_optimal"],
        "optimum_count": base["optimum_count"],
        "canonical_witness": base["canonical_witness"],
        "artifact_checksum": 0,
    }
    artifact["artifact_checksum"] = compute_artifact_checksum(artifact)
    artifact["source_envelope_checksum"] = compute_source_envelope_checksum(artifact)
    validate_oracle_artifact(artifact)
    return artifact


def validate_publication(
    raw: Any,
    sidecar: Any,
    report: Any,
    snapshot: Any,
    *,
    expected_commit: str,
) -> dict[str, Any]:
    """Validate all H=4096 inputs and return the configuration-specific authority."""
    validated_raw, validated_sidecar, validated_report = _validate_authorities(
        raw,
        sidecar,
        report,
        expected_commit=expected_commit,
    )
    return _build_artifact(
        validated_raw,
        validated_sidecar,
        validated_report,
        snapshot,
        expected_commit=expected_commit,
    )


def _validate_binding_checksums(
    artifact: Mapping[str, Any],
    raw_binding: Mapping[str, Any],
    telemetry_binding: Mapping[str, Any],
    report_binding: Mapping[str, Any],
    snapshot_binding: Mapping[str, Any],
) -> None:
    source = {
        "source_commit": artifact["source_commit"],
        "source_stamped": artifact["source_stamped"],
        "source_tree_dirty": artifact["source_tree_dirty"],
    }
    expected_raw_envelope = raw_validator.compute_source_envelope_checksum(
        {
            **source,
            "raw_evidence_schema_version": raw_binding["raw_evidence_schema_version"],
            "wire_schema_version": raw_binding["wire_schema_version"],
            "artifact_checksum": raw_binding["artifact_checksum"],
        }
    )
    expected_telemetry_envelope = telemetry_validator.compute_source_envelope_checksum(
        {
            **source,
            "artifact_checksum": telemetry_binding["artifact_checksum"],
        }
    )
    expected_report_envelope = report_validator.compute_report_source_envelope_checksum(
        {
            **source,
            "artifact_checksum": report_binding["artifact_checksum"],
        }
    )
    expected_snapshot_envelope = oracle.compute_snapshot_source_envelope(
        {
            **source,
            "artifact_checksum": snapshot_binding["artifact_checksum"],
        }
    )
    for observed, expected, label in (
        (raw_binding["source_envelope_checksum"], expected_raw_envelope, "Raw"),
        (
            telemetry_binding["source_envelope_checksum"],
            expected_telemetry_envelope,
            "same-run telemetry",
        ),
        (report_binding["source_envelope_checksum"], expected_report_envelope, "per-net report"),
        (snapshot_binding["source_envelope_checksum"], expected_snapshot_envelope, "snapshot"),
    ):
        if observed != expected:
            raise EvidenceError(f"H=4096 exact-small {label} source envelope is not derivable")


def validate_oracle_artifact(artifact_value: Any) -> Mapping[str, Any]:
    _require_publication_authority()
    artifact = oracle._object(artifact_value, "H=4096 exact-small Oracle Artifact v2")
    oracle._fields(artifact, _FIELDS, "H=4096 exact-small Oracle Artifact v2")
    if (
        oracle._COMMIT.fullmatch(oracle._string(artifact["source_commit"], "oracle.source_commit"))
        is None
        or not oracle._boolean(artifact["source_stamped"], "oracle.source_stamped")
        or oracle._boolean(artifact["source_tree_dirty"], "oracle.source_tree_dirty")
    ):
        raise EvidenceError("H=4096 exact-small oracle requires a clean stamped source")
    if oracle._u32(artifact["schema_version"], "oracle.schema_version") != 1:
        raise EvidenceError("H=4096 exact-small oracle payload schema_version must be 1")
    if oracle._string(artifact["campaign_id"], "oracle.campaign_id") != _CAMPAIGN:
        raise EvidenceError("H=4096 exact-small oracle campaign is invalid")
    if oracle._string(artifact["cell_role"], "oracle.cell_role") != "exact":
        raise EvidenceError("H=4096 exact-small oracle cell_role must be exact")
    for field, expected in (
        ("eligible_input_to_phase4_aggregation", True),
        ("standalone_decision_eligible", False),
        ("statistical_timing_eligible", False),
        ("coverage_complete", False),
        ("exact_small_oracle_complete", True),
    ):
        if oracle._boolean(artifact[field], f"oracle.{field}") is not expected:
            raise EvidenceError(f"H=4096 exact-small oracle {field} must be {expected}")

    config = report_validator.validate_config(artifact["config"])
    if config != _FROZEN_CONFIG:
        raise EvidenceError("H=4096 exact-small oracle config differs from frozen (10100,4)")
    if oracle._u32(artifact["corpus_version"], "oracle.corpus_version") != _CORPUS_VERSION:
        raise EvidenceError("H=4096 exact-small oracle corpus_version must be 2")
    if oracle._u64(artifact["corpus_checksum"], "oracle.corpus_checksum") != _CORPUS_CHECKSUM:
        raise EvidenceError("H=4096 exact-small oracle corpus checksum is invalid")

    raw_binding = oracle._object(artifact["raw_binding"], "oracle.raw_binding")
    oracle._fields(raw_binding, _RAW_BINDING_FIELDS, "oracle.raw_binding")
    if (
        oracle._string(raw_binding["authority"], "oracle.raw_binding.authority") != _RAW_AUTHORITY
        or oracle._u32(
            raw_binding["raw_evidence_schema_version"],
            "oracle.raw_binding.raw_evidence_schema_version",
        )
        != 2
        or oracle._u32(raw_binding["wire_schema_version"], "oracle.raw_binding.wire_schema_version")
        != 2
    ):
        raise EvidenceError("H=4096 exact-small Raw binding authority is invalid")
    expected_plan = raw_validator.compute_cell_plan_checksum(
        {"config": config, "corpus_checksum": artifact["corpus_checksum"]},
        corpus_version=_CORPUS_VERSION,
    )
    if oracle._u64(raw_binding["cell_plan_checksum"], "oracle.raw_binding.cell_plan_checksum") != (
        expected_plan
    ):
        raise EvidenceError("H=4096 exact-small Raw cell plan is invalid")

    telemetry_binding = oracle._object(
        artifact["same_run_telemetry_binding"],
        "oracle.same_run_telemetry_binding",
    )
    oracle._fields(
        telemetry_binding,
        _TELEMETRY_BINDING_FIELDS,
        "oracle.same_run_telemetry_binding",
    )
    if (
        oracle._string(
            telemetry_binding["authority"],
            "oracle.same_run_telemetry_binding.authority",
        )
        != _TELEMETRY_AUTHORITY
        or oracle._u32(
            telemetry_binding["schema_version"],
            "oracle.same_run_telemetry_binding.schema_version",
        )
        != 1
        or oracle._u32(
            telemetry_binding["raw_evidence_schema_version"],
            "oracle.same_run_telemetry_binding.raw_evidence_schema_version",
        )
        != 2
        or oracle._u32(
            telemetry_binding["raw_wire_schema_version"],
            "oracle.same_run_telemetry_binding.raw_wire_schema_version",
        )
        != 2
        or oracle._u32(
            telemetry_binding["telemetry_wire_schema_version"],
            "oracle.same_run_telemetry_binding.telemetry_wire_schema_version",
        )
        != 2
    ):
        raise EvidenceError("H=4096 exact-small telemetry binding authority is invalid")
    oracle._boolean(
        telemetry_binding["exact_rejection_guardrail_passed"],
        "oracle.same_run_telemetry_binding.exact_rejection_guardrail_passed",
    )

    report_binding = oracle._object(
        artifact["per_net_report_binding"],
        "oracle.per_net_report_binding",
    )
    oracle._fields(report_binding, _REPORT_BINDING_FIELDS, "oracle.per_net_report_binding")
    if (
        oracle._string(report_binding["authority"], "oracle.per_net_report_binding.authority")
        != _REPORT_AUTHORITY
        or oracle._u32(
            report_binding["schema_version"],
            "oracle.per_net_report_binding.schema_version",
        )
        != 1
        or oracle._u32(
            report_binding["corpus_version"],
            "oracle.per_net_report_binding.corpus_version",
        )
        != 2
        or oracle._u32(
            report_binding["raw_wire_schema_version"],
            "oracle.per_net_report_binding.raw_wire_schema_version",
        )
        != 2
    ):
        raise EvidenceError("H=4096 exact-small report binding authority is invalid")

    snapshot_binding = oracle._object(artifact["snapshot_binding"], "oracle.snapshot_binding")
    oracle._fields(snapshot_binding, _SNAPSHOT_BINDING_FIELDS, "oracle.snapshot_binding")
    if (
        oracle._string(snapshot_binding["authority"], "oracle.snapshot_binding.authority")
        != _SNAPSHOT_AUTHORITY
        or oracle._u32(
            snapshot_binding["schema_version"],
            "oracle.snapshot_binding.schema_version",
        )
        != 1
        or oracle._u32(
            snapshot_binding["corpus_version"],
            "oracle.snapshot_binding.corpus_version",
        )
        != 2
    ):
        raise EvidenceError("H=4096 exact-small snapshot binding authority is invalid")

    for binding, fields, label in (
        (raw_binding, ("artifact_checksum", "source_envelope_checksum"), "Raw"),
        (
            telemetry_binding,
            (
                "raw_artifact_checksum",
                "raw_source_envelope_checksum",
                "artifact_checksum",
                "source_envelope_checksum",
            ),
            "telemetry",
        ),
        (
            report_binding,
            (
                "raw_artifact_checksum",
                "raw_source_envelope_checksum",
                "artifact_checksum",
                "source_envelope_checksum",
            ),
            "report",
        ),
        (
            snapshot_binding,
            (
                "raw_artifact_checksum",
                "raw_source_envelope_checksum",
                "per_net_report_artifact_checksum",
                "per_net_report_source_envelope_checksum",
                "artifact_checksum",
                "source_envelope_checksum",
            ),
            "snapshot",
        ),
    ):
        for field in fields:
            if oracle._u64(binding[field], f"oracle.{label}.{field}") == 0:
                raise EvidenceError(f"H=4096 exact-small {label} {field} must be nonzero")
    for binding, field, expected, label in (
        (telemetry_binding, "raw_artifact_checksum", raw_binding["artifact_checksum"], "telemetry"),
        (
            telemetry_binding,
            "raw_source_envelope_checksum",
            raw_binding["source_envelope_checksum"],
            "telemetry",
        ),
        (report_binding, "raw_artifact_checksum", raw_binding["artifact_checksum"], "report"),
        (
            report_binding,
            "raw_source_envelope_checksum",
            raw_binding["source_envelope_checksum"],
            "report",
        ),
        (snapshot_binding, "raw_artifact_checksum", raw_binding["artifact_checksum"], "snapshot"),
        (
            snapshot_binding,
            "raw_source_envelope_checksum",
            raw_binding["source_envelope_checksum"],
            "snapshot",
        ),
        (
            snapshot_binding,
            "per_net_report_artifact_checksum",
            report_binding["artifact_checksum"],
            "snapshot",
        ),
        (
            snapshot_binding,
            "per_net_report_source_envelope_checksum",
            report_binding["source_envelope_checksum"],
            "snapshot",
        ),
    ):
        if binding[field] != expected:
            raise EvidenceError(f"H=4096 exact-small {label} binding does not join")
    _validate_binding_checksums(
        artifact,
        raw_binding,
        telemetry_binding,
        report_binding,
        snapshot_binding,
    )

    if (
        oracle._u64(
            artifact["candidate_semantic_checksum"],
            "oracle.candidate_semantic_checksum",
        )
        == 0
    ):
        raise EvidenceError("H=4096 exact-small candidate semantic checksum must be nonzero")
    product = oracle._u64(artifact["cartesian_product"], "oracle.cartesian_product")
    if product == 0 or product > oracle._MAXIMUM_CARTESIAN_PRODUCT:
        raise EvidenceError("H=4096 exact-small Cartesian product is out of bounds")
    objectives: list[Mapping[str, Any]] = []
    for name in ("production_objective", "optimum_objective"):
        objective = oracle._object(artifact[name], f"oracle.{name}")
        oracle._fields(objective, _OBJECTIVE_FIELDS, f"oracle.{name}")
        for field in _OBJECTIVE_FIELDS:
            oracle._u64(objective[field], f"oracle.{name}.{field}")
        if objective["selected_net_count"] > oracle._POOL_COUNT:
            raise EvidenceError(f"H=4096 exact-small {name} selects more than six nets")
        if objective["selected_net_count"] == 0 and (
            objective["total_overuse_units"] != 0 or objective["total_intrinsic_base_cost"] != 0
        ):
            raise EvidenceError(f"H=4096 exact-small {name} has cost without selections")
        objectives.append(objective)
    production_resources = oracle._u64(
        artifact["production_overused_resource_count"],
        "oracle.production_overused_resource_count",
    )
    witness_resources = oracle._u64(
        artifact["canonical_witness_overused_resource_count"],
        "oracle.canonical_witness_overused_resource_count",
    )
    if (
        (objectives[0]["total_overuse_units"] == 0) != (production_resources == 0)
        or (objectives[1]["total_overuse_units"] == 0) != (witness_resources == 0)
        or production_resources > objectives[0]["total_overuse_units"]
        or witness_resources > objectives[1]["total_overuse_units"]
    ):
        raise EvidenceError("H=4096 exact-small overuse units and resource counts disagree")
    if not oracle._boolean(artifact["production_is_optimal"], "oracle.production_is_optimal"):
        raise EvidenceError("H=4096 exact-small publication must prove exact optimality")
    if objectives[0] != objectives[1]:
        raise EvidenceError("H=4096 exact-small production and optimum objectives differ")
    optimum_count = oracle._u64(artifact["optimum_count"], "oracle.optimum_count")
    if optimum_count == 0 or optimum_count > product:
        raise EvidenceError("H=4096 exact-small optimum_count is outside the product")

    witness = oracle._array(artifact["canonical_witness"], "oracle.canonical_witness")
    if len(witness) != oracle._POOL_COUNT:
        raise EvidenceError("H=4096 exact-small witness must contain six nets")
    previous_net: tuple[int, int] | None = None
    witness_nets: list[tuple[int, int]] = []
    selected_ids: set[tuple[int, int]] = set()
    for index, item in enumerate(witness):
        label = f"oracle.canonical_witness[{index}]"
        row = oracle._object(item, label)
        oracle._fields(row, oracle._WITNESS_FIELDS, label)
        net_key = oracle._entity_key(oracle._entity(row["net"], f"{label}.net"))
        if previous_net is not None and net_key <= previous_net:
            raise EvidenceError("H=4096 exact-small witness nets are not strictly ordered")
        previous_net = net_key
        witness_nets.append(net_key)
        if row["candidate_id"] is not None:
            candidate = oracle._hash128(row["candidate_id"], f"{label}.candidate_id")
            candidate_key = oracle._id_key(candidate)
            if candidate_key in selected_ids:
                raise EvidenceError("H=4096 exact-small witness candidate IDs are not unique")
            selected_ids.add(candidate_key)
    expected_nets = h4096_raw.validated_confirmatory_h4096_workload_roster(_CASE_ID)
    if tuple(witness_nets) != expected_nets:
        raise EvidenceError("H=4096 exact-small witness differs from the Corpus-v2 roster")
    if len(selected_ids) != objectives[1]["selected_net_count"]:
        raise EvidenceError("H=4096 exact-small witness does not reproduce selected count")

    artifact_checksum = oracle._u64(artifact["artifact_checksum"], "oracle.artifact_checksum")
    if artifact_checksum == 0 or artifact_checksum != compute_artifact_checksum(artifact):
        raise EvidenceError("H=4096 exact-small oracle artifact checksum is invalid")
    source_envelope_checksum = oracle._u64(
        artifact["source_envelope_checksum"],
        "oracle.source_envelope_checksum",
    )
    if (
        source_envelope_checksum == 0
        or source_envelope_checksum != compute_source_envelope_checksum(artifact)
    ):
        raise EvidenceError("H=4096 exact-small oracle source envelope is invalid")
    return artifact


def serialize_oracle_artifact(artifact: Mapping[str, Any]) -> str:
    validated = validate_oracle_artifact(artifact)
    output = _canonical(validated) + "\n"
    if len(output.encode("utf-8")) > _MAXIMUM_OUTPUT_BYTES:
        raise EvidenceError("H=4096 exact-small oracle output exceeds 1 MiB")
    return output


def main(argv: Sequence[str] | None = None) -> int:
    if __name__ == "__main__":
        from tools.phase4_exact_small_oracle_launcher_handshake import require_launcher

        require_launcher("phase4_confirmatory_h4096_exact_small_oracle_validator_py")
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        _require_publication_authority()
        raw = raw_validator.read_document(options.raw)
        h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = oracle._object(raw, "H=4096 exact-small Raw input")
        _require_scope(raw_document)

        sidecar = telemetry_validator.read_document(options.same_run_telemetry)
        validated_sidecar = h4096_telemetry.validate_confirmatory_h4096_join(
            raw_document,
            sidecar,
            expected_commit=options.expected_commit,
        )

        report = report_validator.read_report_document(options.report)
        h4096_report_join.validate_join(
            raw_document,
            validated_sidecar,
            report,
            expected_commit=options.expected_commit,
        )

        snapshot = oracle.read_snapshot_document(options.snapshot)
        artifact = _build_artifact(
            raw_document,
            validated_sidecar,
            oracle._object(report, "H=4096 exact-small per-net report"),
            snapshot,
            expected_commit=options.expected_commit,
        )
        output = serialize_oracle_artifact(artifact)
    except (EvidenceError, OSError, ValueError) as error:
        print(
            f"Phase 4 confirmatory H=4096 exact-small publication failed: {error}", file=sys.stderr
        )
        return 1
    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
