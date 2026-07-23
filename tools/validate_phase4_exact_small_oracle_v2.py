"""Strict exact-small oracle publication for joined Phase 4 Raw-v2 authority."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_exact_small_oracle as oracle
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

EvidenceError = raw_validator.EvidenceError
_MAXIMUM_OUTPUT_BYTES = 1024 * 1024
_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "decision_eligible",
    "case_id",
    "raw_evidence_schema_version",
    "raw_wire_schema_version",
    "raw_artifact_checksum",
    "raw_source_envelope_checksum",
    "same_run_telemetry_schema_version",
    "same_run_telemetry_wire_schema_version",
    "same_run_telemetry_artifact_checksum",
    "same_run_telemetry_source_envelope_checksum",
    "per_net_report_artifact_checksum",
    "per_net_report_source_envelope_checksum",
    "snapshot_artifact_checksum",
    "snapshot_source_envelope_checksum",
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
_OBJECTIVE_FIELDS = (
    "selected_net_count",
    "total_overuse_units",
    "total_intrinsic_base_cost",
)


def compute_artifact_checksum(artifact: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-EXACT-SMALL-ORACLE-ARTIFACT-V2")
    hashed.u32(artifact["schema_version"])
    hashed.boolean(artifact["decision_eligible"])
    hashed.u32(artifact["case_id"])
    hashed.u32(artifact["raw_evidence_schema_version"])
    hashed.u32(artifact["raw_wire_schema_version"])
    for field in (
        "raw_artifact_checksum",
        "raw_source_envelope_checksum",
        "same_run_telemetry_schema_version",
        "same_run_telemetry_wire_schema_version",
        "same_run_telemetry_artifact_checksum",
        "same_run_telemetry_source_envelope_checksum",
        "per_net_report_artifact_checksum",
        "per_net_report_source_envelope_checksum",
        "snapshot_artifact_checksum",
        "snapshot_source_envelope_checksum",
        "candidate_semantic_checksum",
        "cartesian_product",
    ):
        if field.endswith("schema_version"):
            hashed.u32(artifact[field])
        else:
            hashed.u64(artifact[field])
    for name in ("production_objective", "optimum_objective"):
        for field in _OBJECTIVE_FIELDS:
            hashed.u64(artifact[name][field])
    hashed.u64(artifact["production_overused_resource_count"])
    hashed.u64(artifact["canonical_witness_overused_resource_count"])
    hashed.boolean(artifact["production_is_optimal"])
    hashed.u64(artifact["optimum_count"])
    hashed.u64(len(artifact["canonical_witness"]))
    for row in artifact["canonical_witness"]:
        hashed.u64(row["net"]["id"])
        hashed.u32(row["net"]["generation"])
        hashed.boolean(row["candidate_id"] is not None)
        if row["candidate_id"] is not None:
            hashed.u64(row["candidate_id"]["high"])
            hashed.u64(row["candidate_id"]["low"])
    return hashed.finish()


def compute_source_envelope_checksum(artifact: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-EXACT-SMALL-ORACLE-SOURCE-ENVELOPE-V2")
    hashed.string(artifact["source_commit"])
    hashed.boolean(artifact["source_stamped"])
    hashed.boolean(artifact["source_tree_dirty"])
    hashed.u64(artifact["artifact_checksum"])
    return hashed.finish()


def validate_oracle_artifact(artifact_value: Any) -> Mapping[str, Any]:
    artifact = oracle._object(artifact_value, "exact-small Oracle Artifact v2")
    oracle._fields(artifact, _FIELDS, "exact-small Oracle Artifact v2")
    if (
        oracle._COMMIT.fullmatch(oracle._string(artifact["source_commit"], "oracle.source_commit"))
        is None
        or not oracle._boolean(artifact["source_stamped"], "oracle.source_stamped")
        or oracle._boolean(artifact["source_tree_dirty"], "oracle.source_tree_dirty")
    ):
        raise EvidenceError("exact-small Oracle Artifact v2 requires a clean stamped source")
    if oracle._u32(artifact["schema_version"], "oracle.schema_version") != 2:
        raise EvidenceError("exact-small Oracle Artifact v2 schema_version must be 2")
    if oracle._boolean(artifact["decision_eligible"], "oracle.decision_eligible"):
        raise EvidenceError("exact-small Oracle Artifact v2 is diagnostic only")
    if oracle._u32(artifact["case_id"], "oracle.case_id") not in {100, 101, 102}:
        raise EvidenceError("exact-small Oracle Artifact v2 case_id is not canonical")
    for field, expected in (
        ("raw_evidence_schema_version", 2),
        ("raw_wire_schema_version", 2),
        ("same_run_telemetry_schema_version", 1),
        ("same_run_telemetry_wire_schema_version", 2),
    ):
        if oracle._u32(artifact[field], f"oracle.{field}") != expected:
            raise EvidenceError(f"exact-small Oracle Artifact v2 {field} must be {expected}")
    for field in (
        "source_envelope_checksum",
        "raw_artifact_checksum",
        "raw_source_envelope_checksum",
        "same_run_telemetry_artifact_checksum",
        "same_run_telemetry_source_envelope_checksum",
        "per_net_report_artifact_checksum",
        "per_net_report_source_envelope_checksum",
        "snapshot_artifact_checksum",
        "snapshot_source_envelope_checksum",
        "candidate_semantic_checksum",
        "artifact_checksum",
    ):
        if oracle._u64(artifact[field], f"oracle.{field}") == 0:
            raise EvidenceError(f"exact-small Oracle Artifact v2 {field} must be nonzero")
    source = {
        "source_commit": artifact["source_commit"],
        "source_stamped": artifact["source_stamped"],
        "source_tree_dirty": artifact["source_tree_dirty"],
    }
    expected_external_envelopes = {
        "raw_source_envelope_checksum": raw_validator.compute_source_envelope_checksum(
            {
                **source,
                "raw_evidence_schema_version": artifact["raw_evidence_schema_version"],
                "wire_schema_version": artifact["raw_wire_schema_version"],
                "artifact_checksum": artifact["raw_artifact_checksum"],
            }
        ),
        "same_run_telemetry_source_envelope_checksum": (
            same_run_validator.compute_source_envelope_checksum(
                {
                    **source,
                    "artifact_checksum": artifact["same_run_telemetry_artifact_checksum"],
                }
            )
        ),
        "per_net_report_source_envelope_checksum": (
            report_validator.compute_report_source_envelope_checksum(
                {
                    **source,
                    "artifact_checksum": artifact["per_net_report_artifact_checksum"],
                }
            )
        ),
        "snapshot_source_envelope_checksum": oracle.compute_snapshot_source_envelope(
            {
                **source,
                "artifact_checksum": artifact["snapshot_artifact_checksum"],
            }
        ),
    }
    for field, expected in expected_external_envelopes.items():
        if artifact[field] != expected:
            raise EvidenceError(
                f"exact-small Oracle Artifact v2 {field} is not derivable from its artifact"
            )
    product = oracle._u64(artifact["cartesian_product"], "oracle.cartesian_product")
    if product == 0 or product > 4096:
        raise EvidenceError("exact-small Oracle Artifact v2 cartesian_product is out of bounds")
    objectives: list[Mapping[str, Any]] = []
    for name in ("production_objective", "optimum_objective"):
        objective = oracle._object(artifact[name], f"oracle.{name}")
        oracle._fields(objective, _OBJECTIVE_FIELDS, f"oracle.{name}")
        for field in _OBJECTIVE_FIELDS:
            oracle._u64(objective[field], f"oracle.{name}.{field}")
        if objective["selected_net_count"] > 6:
            raise EvidenceError(f"exact-small Oracle Artifact v2 {name} selects more than six nets")
        objectives.append(objective)
    production_overused_resources = oracle._u64(
        artifact["production_overused_resource_count"],
        "oracle.production_overused_resource_count",
    )
    witness_overused_resources = oracle._u64(
        artifact["canonical_witness_overused_resource_count"],
        "oracle.canonical_witness_overused_resource_count",
    )
    if (
        (objectives[0]["total_overuse_units"] == 0) != (production_overused_resources == 0)
        or (objectives[1]["total_overuse_units"] == 0) != (witness_overused_resources == 0)
        or production_overused_resources > objectives[0]["total_overuse_units"]
        or witness_overused_resources > objectives[1]["total_overuse_units"]
    ):
        raise EvidenceError(
            "exact-small Oracle Artifact v2 overuse units and resource counts disagree"
        )
    if not oracle._boolean(artifact["production_is_optimal"], "oracle.production_is_optimal"):
        raise EvidenceError("exact-small Oracle Artifact v2 must publish an exact optimum")
    if objectives[0] != objectives[1]:
        raise EvidenceError("exact-small Oracle Artifact v2 objectives must be equal")
    for name, objective in zip(
        ("production_objective", "optimum_objective"), objectives, strict=True
    ):
        if objective["selected_net_count"] == 0 and (
            objective["total_overuse_units"] != 0 or objective["total_intrinsic_base_cost"] != 0
        ):
            raise EvidenceError(
                f"exact-small Oracle Artifact v2 {name} has cost or overuse without selections"
            )
    optimum_count = oracle._u64(artifact["optimum_count"], "oracle.optimum_count")
    if optimum_count == 0 or optimum_count > product:
        raise EvidenceError(
            "exact-small Oracle Artifact v2 optimum_count must be within the product"
        )
    witness = oracle._array(artifact["canonical_witness"], "oracle.canonical_witness")
    if len(witness) != 6:
        raise EvidenceError("exact-small Oracle Artifact v2 witness must contain six nets")
    previous_net: tuple[int, int] | None = None
    witness_nets: list[tuple[int, int]] = []
    selected_candidate_ids: set[tuple[int, int]] = set()
    for index, item in enumerate(witness):
        label = f"oracle.canonical_witness[{index}]"
        row = oracle._object(item, label)
        oracle._fields(row, oracle._WITNESS_FIELDS, label)
        net = oracle._entity(row["net"], f"{label}.net")
        net_key = oracle._entity_key(net)
        if previous_net is not None and net_key <= previous_net:
            raise EvidenceError(
                "exact-small Oracle Artifact v2 witness nets are not strictly ordered"
            )
        previous_net = net_key
        witness_nets.append(net_key)
        if row["candidate_id"] is not None:
            candidate = oracle._hash128(row["candidate_id"], f"{label}.candidate_id")
            candidate_key = (candidate["high"], candidate["low"])
            if candidate_key in selected_candidate_ids:
                raise EvidenceError(
                    "exact-small Oracle Artifact v2 witness candidate IDs are not unique"
                )
            selected_candidate_ids.add(candidate_key)
    _, expected_nets = report_validator._frozen_roster_row(artifact["case_id"])
    if tuple(witness_nets) != expected_nets:
        raise EvidenceError(
            "exact-small Oracle Artifact v2 witness differs from the frozen case roster"
        )
    if len(selected_candidate_ids) != objectives[1]["selected_net_count"]:
        raise EvidenceError(
            "exact-small Oracle Artifact v2 witness does not reproduce selected_net_count"
        )
    if artifact["artifact_checksum"] != compute_artifact_checksum(artifact):
        raise EvidenceError("exact-small Oracle Artifact v2 checksum is invalid")
    if artifact["source_envelope_checksum"] != compute_source_envelope_checksum(artifact):
        raise EvidenceError("exact-small Oracle Artifact v2 source envelope is invalid")
    return artifact


def validate_publication(
    raw: Any,
    sidecar: Any,
    report: Any,
    snapshot: Any,
    *,
    expected_commit: str,
) -> dict[str, Any]:
    """Validate the complete versioned authority and return Oracle Artifact v2."""
    same_run_validator.validate_join(
        raw,
        sidecar,
        expected_commit=expected_commit,
    )
    if not isinstance(report, dict):
        raise EvidenceError("per-net report input must be a JSON object")
    if report.get("raw_wire_schema_version") != 2:
        raise EvidenceError("exact-small Raw-v2 publication requires a Wire-v2 per-net report")
    report_validator.validate_report_against_validated_raw(
        raw,
        report,
        expected_commit=expected_commit,
    )
    base = oracle.validate_publication_against_validated_authorities(
        raw,
        report,
        snapshot,
        expected_commit=expected_commit,
    )
    artifact: dict[str, Any] = {
        "source_commit": base["source_commit"],
        "source_stamped": base["source_stamped"],
        "source_tree_dirty": base["source_tree_dirty"],
        "source_envelope_checksum": 0,
        "schema_version": 2,
        "decision_eligible": False,
        "case_id": base["case_id"],
        "raw_evidence_schema_version": raw["raw_evidence_schema_version"],
        "raw_wire_schema_version": raw["wire_schema_version"],
        "raw_artifact_checksum": base["raw_artifact_checksum"],
        "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        "same_run_telemetry_schema_version": sidecar["schema_version"],
        "same_run_telemetry_wire_schema_version": sidecar["telemetry_wire_schema_version"],
        "same_run_telemetry_artifact_checksum": sidecar["artifact_checksum"],
        "same_run_telemetry_source_envelope_checksum": sidecar["source_envelope_checksum"],
        "per_net_report_artifact_checksum": base["per_net_report_artifact_checksum"],
        "per_net_report_source_envelope_checksum": report["source_envelope_checksum"],
        "snapshot_artifact_checksum": base["snapshot_artifact_checksum"],
        "snapshot_source_envelope_checksum": snapshot["source_envelope_checksum"],
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
    return artifact


def serialize_oracle_artifact(artifact: Mapping[str, Any]) -> str:
    validated = validate_oracle_artifact(artifact)
    output = (
        json.dumps(
            validated,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
        )
        + "\n"
    )
    if len(output.encode("utf-8")) > _MAXIMUM_OUTPUT_BYTES:
        raise EvidenceError("exact-small Oracle Artifact v2 exceeds 1 MiB")
    return output


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--snapshot", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw = raw_validator.read_document(options.raw)
        sidecar = same_run_validator.read_document(options.same_run_telemetry)
        same_run_validator.validate_join(
            raw,
            sidecar,
            expected_commit=options.expected_commit,
        )
        report = report_validator.read_report_document(options.report)
        if not isinstance(report, dict):
            raise EvidenceError("per-net report input must be a JSON object")
        if report.get("raw_wire_schema_version") != 2:
            raise EvidenceError("exact-small Raw-v2 publication requires a Wire-v2 per-net report")
        report_validator.validate_report_against_validated_raw(
            raw,
            report,
            expected_commit=options.expected_commit,
        )
        snapshot = oracle.read_snapshot_document(options.snapshot)
        artifact = validate_publication(
            raw,
            sidecar,
            report,
            snapshot,
            expected_commit=options.expected_commit,
        )
        output = serialize_oracle_artifact(artifact)
    except EvidenceError as error:
        print(f"Phase 4 exact-small Raw-v2 publication failed: {error}", file=sys.stderr)
        return 1
    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
