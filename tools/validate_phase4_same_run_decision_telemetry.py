"""Strict join for Phase 4 same-run Raw v2 and decision telemetry v1."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_statistical_protocol_v2 as protocol_validator
from tools import validate_phase4_workload_net_roster_manifest as roster_validator

_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_MAX_BYTES = 32 * 1024 * 1024
_MAX_DEPTH = 64
_COMMIT = re.compile(r"[0-9a-f]{40}")

_TOP_FIELDS = (
    "source_commit",
    "source_stamped",
    "source_tree_dirty",
    "source_envelope_checksum",
    "schema_version",
    "raw_evidence_schema_version",
    "raw_wire_schema_version",
    "telemetry_wire_schema_version",
    "config",
    "corpus_checksum",
    "raw_cell_plan_checksum",
    "raw_environment_checksum",
    "raw_authority_run_identity",
    "raw_controller_identity",
    "raw_cell_artifact_checksum",
    "raw_source_envelope_checksum",
    "attempts",
    "artifact_checksum",
)
_PAIR_FIELDS = (
    "schema_version",
    "case_id",
    "requested_pool_size",
    "repetition_index",
    "root_seed",
    "execution_order",
    "associated_raw_pair_attempt_checksum",
    "associated_paired_semantic_checksum",
    "associated_paired_artifact_checksum",
    "baseline",
    "candidate",
    "capture_checksum",
)
_ARM_FIELDS = (
    "schema_version",
    "arm",
    "repetition_index",
    "execution_order",
    "dispatch_ordinal",
    "process_instance_identity",
    "associated_semantic_checksum",
    "associated_arm_artifact_checksum",
    "associated_authority_checksum",
    "associated_arm_attempt_checksum",
    "telemetry",
    "capture_checksum",
)
_TELEMETRY_FIELDS = (
    "schema_version",
    "associated_semantic_checksum",
    "outcome",
    "per_net",
    "telemetry_checksum",
)
_OUTCOME_FIELDS = (
    "selected_net_count",
    "no_candidate_net_count",
    "overused_resource_count",
    "total_overuse_units",
    "total_intrinsic_cost",
    "world_checksum",
)
_ROW_FIELDS = ("net", "columns")
_NET_FIELDS = ("id", "generation")
_COLUMN_FIELDS = (
    "requested_columns",
    "executed_route_queries",
    "admitted_candidates",
    "duplicate_candidates",
    "disconnected_columns",
    "unsupported_columns",
    "skipped_columns",
    "exact_validation_rejections",
    "other_rejections",
)


EvidenceError = raw_validator.EvidenceError
StableHashBuilder = raw_validator.StableHashBuilder


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise EvidenceError(f"{label} must be a JSON object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise EvidenceError(f"{label} must be a JSON array")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value) != tuple(expected):
        missing = sorted(set(expected) - set(value))
        extra = sorted(set(value) - set(expected))
        raise EvidenceError(
            f"{label} fields are missing, extra, or outside canonical order: "
            f"missing={missing}, extra={extra}"
        )


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise EvidenceError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _bool(value: Any, label: str) -> bool:
    if not isinstance(value, bool):
        raise EvidenceError(f"{label} must be a boolean")
    return value


def _version(value: Mapping[str, Any], label: str) -> None:
    if _u32(value["schema_version"], f"{label}.schema_version") != 1:
        raise EvidenceError(f"{label}.schema_version must be 1")


def _exact_equal(left: Any, right: Any) -> bool:
    if type(left) is not type(right):
        return False
    if isinstance(left, dict):
        return tuple(left) == tuple(right) and all(
            _exact_equal(left[key], right[key]) for key in left
        )
    if isinstance(left, list):
        return len(left) == len(right) and all(
            _exact_equal(a, b) for a, b in zip(left, right, strict=True)
        )
    return left == right


def _check_depth(value: Any, depth: int = 1) -> None:
    if depth > _MAX_DEPTH:
        raise EvidenceError(f"same-run telemetry exceeds maximum JSON depth {_MAX_DEPTH}")
    if isinstance(value, dict):
        for child in value.values():
            _check_depth(child, depth + 1)
    elif isinstance(value, list):
        for child in value:
            _check_depth(child, depth + 1)


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise EvidenceError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise EvidenceError(f"non-finite JSON number {value!r} is forbidden")


def read_document(path: pathlib.Path) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(_MAX_BYTES + 1)
        if len(encoded) > _MAX_BYTES:
            raise EvidenceError(f"same-run telemetry exceeds {_MAX_BYTES} bytes")
        if (
            encoded.startswith(b"\xef\xbb\xbf")
            or not encoded.endswith(b"\n")
            or encoded.endswith(b"\n\n")
        ):
            raise EvidenceError("same-run telemetry must be UTF-8 without BOM and end in one LF")
        text = encoded.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
        _check_depth(value)
        document = _object(value, "same-run telemetry")
        canonical = (
            json.dumps(document, ensure_ascii=False, separators=(",", ":"), allow_nan=False) + "\n"
        )
        if text != canonical:
            raise EvidenceError("same-run telemetry must use canonical one-line JSON")
        return document
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError) as error:
        if isinstance(error, EvidenceError):
            raise
        raise EvidenceError(f"cannot read same-run telemetry {path}: {error}") from error


def _hash_outcome(hashed: StableHashBuilder, outcome: Mapping[str, Any]) -> None:
    for field in _OUTCOME_FIELDS:
        hashed.u64(outcome[field])


def compute_telemetry_checksum(telemetry: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-SAME-RUN-ARM-DECISION-TELEMETRY-V1")
    hashed.u32(telemetry["schema_version"])
    hashed.u64(telemetry["associated_semantic_checksum"])
    _hash_outcome(hashed, telemetry["outcome"])
    hashed.u64(len(telemetry["per_net"]))
    for row in telemetry["per_net"]:
        hashed.u64(row["net"]["id"])
        hashed.u32(row["net"]["generation"])
        for field in _COLUMN_FIELDS:
            hashed.u64(row["columns"][field])
    return hashed.finish()


def compute_arm_capture_checksum(capture: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-ISOLATED-SAME-RUN-ARM-CAPTURE-V1")
    hashed.u32(capture["schema_version"])
    hashed.byte(capture["arm"])
    hashed.u32(capture["repetition_index"])
    hashed.byte(capture["execution_order"])
    for field in (
        "dispatch_ordinal",
        "process_instance_identity",
        "associated_semantic_checksum",
        "associated_arm_artifact_checksum",
        "associated_authority_checksum",
        "associated_arm_attempt_checksum",
    ):
        hashed.u64(capture[field])
    hashed.u64(capture["telemetry"]["telemetry_checksum"])
    return hashed.finish()


def compute_pair_capture_checksum(capture: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-ISOLATED-SAME-RUN-PAIR-CAPTURE-V1")
    hashed.u32(capture["schema_version"])
    hashed.u32(capture["case_id"])
    hashed.u32(capture["requested_pool_size"])
    hashed.u32(capture["repetition_index"])
    hashed.u64(capture["root_seed"])
    hashed.byte(capture["execution_order"])
    for field in (
        "associated_raw_pair_attempt_checksum",
        "associated_paired_semantic_checksum",
        "associated_paired_artifact_checksum",
    ):
        hashed.u64(capture[field])
    hashed.u64(capture["baseline"]["capture_checksum"])
    hashed.u64(capture["candidate"]["capture_checksum"])
    return hashed.finish()


def compute_cell_capture_checksum(document: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-ISOLATED-SAME-RUN-CELL-CAPTURE-V1")
    hashed.u32(document["schema_version"])
    hashed.u32(document["raw_evidence_schema_version"])
    hashed.u32(document["raw_wire_schema_version"])
    hashed.u32(document["telemetry_wire_schema_version"])
    config = document["config"]
    for field in (
        "schema_version",
        "case_id",
        "requested_pool_size",
        "preparation_worker_count",
        "repetitions",
    ):
        hashed.u32(config[field])
    hashed.u64(config["maximum_setup_elapsed_nanoseconds"])
    for field in (
        "maximum_prepared_elapsed_nanoseconds",
        "maximum_cold_elapsed_nanoseconds",
        "maximum_address_space_bytes",
        "maximum_peak_host_bytes",
    ):
        hashed.u64(config["external_budget"][field])
    for field in (
        "maximum_nets",
        "maximum_compiled_nodes",
        "maximum_compiled_host_bytes",
        "maximum_active_regions",
        "maximum_board_entities",
    ):
        hashed.u64(config["corpus_limits"][field])
    for field in (
        "corpus_checksum",
        "raw_cell_plan_checksum",
        "raw_environment_checksum",
        "raw_authority_run_identity",
        "raw_controller_identity",
        "raw_cell_artifact_checksum",
        "raw_source_envelope_checksum",
    ):
        hashed.u64(document[field])
    hashed.u64(len(document["attempts"]))
    for attempt in document["attempts"]:
        hashed.u64(attempt["capture_checksum"])
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-SAME-RUN-DECISION-TELEMETRY-SOURCE-ENVELOPE-V1")
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def _outcome(value: Any, label: str) -> Mapping[str, Any]:
    outcome = _object(value, label)
    _fields(outcome, _OUTCOME_FIELDS, label)
    for field in _OUTCOME_FIELDS:
        _u64(outcome[field], f"{label}.{field}")
    return outcome


def _telemetry(
    value: Any,
    label: str,
    *,
    raw_semantics: Mapping[str, Any],
    roster: tuple[tuple[int, int], ...],
) -> Mapping[str, Any]:
    telemetry = _object(value, label)
    _fields(telemetry, _TELEMETRY_FIELDS, label)
    _version(telemetry, label)
    associated = _u64(
        telemetry["associated_semantic_checksum"], f"{label}.associated_semantic_checksum"
    )
    if associated == 0 or associated != raw_semantics["semantic_checksum"]:
        raise EvidenceError(f"{label} names another Raw arm semantics")
    outcome = _outcome(telemetry["outcome"], f"{label}.outcome")
    if not _exact_equal(outcome, raw_semantics["outcome"]):
        raise EvidenceError(f"{label}.outcome differs from the same Raw arm")
    per_net = _array(telemetry["per_net"], f"{label}.per_net")
    if len(per_net) != len(roster):
        raise EvidenceError(f"{label}.per_net does not contain the complete frozen roster")
    totals = {field: 0 for field in _COLUMN_FIELDS}
    for index, (raw_row, expected_net) in enumerate(zip(per_net, roster, strict=True)):
        row_label = f"{label}.per_net[{index}]"
        row = _object(raw_row, row_label)
        _fields(row, _ROW_FIELDS, row_label)
        net = _object(row["net"], f"{row_label}.net")
        _fields(net, _NET_FIELDS, f"{row_label}.net")
        actual_net = (
            _u64(net["id"], f"{row_label}.net.id"),
            _u32(net["generation"], f"{row_label}.net.generation"),
        )
        if actual_net != expected_net:
            raise EvidenceError(f"{row_label}.net differs from the frozen full EntityRef roster")
        columns = _object(row["columns"], f"{row_label}.columns")
        _fields(columns, _COLUMN_FIELDS, f"{row_label}.columns")
        for field in _COLUMN_FIELDS:
            totals[field] += _u64(columns[field], f"{row_label}.columns.{field}")
            if totals[field] > _U64_MAX:
                raise EvidenceError(f"{label} aggregate {field} overflows u64")
        if columns["requested_columns"] != (
            columns["executed_route_queries"] + columns["skipped_columns"]
        ):
            raise EvidenceError(f"{row_label} executed/skipped partition does not close")
        terminal = sum(columns[field] for field in _COLUMN_FIELDS[2:])
        if terminal != columns["requested_columns"]:
            raise EvidenceError(f"{row_label} terminal column partition does not close")
    rejected = sum(totals[field] for field in _COLUMN_FIELDS[3:])
    if (
        totals["requested_columns"] != raw_semantics["requested_columns"]
        or totals["executed_route_queries"] != raw_semantics["actual"]["route_queries"]
        or totals["admitted_candidates"] != raw_semantics["admitted_candidates"]
        or rejected != raw_semantics["rejected_columns"]
    ):
        raise EvidenceError(f"{label} aggregates do not close the associated Raw semantics")
    checksum = _u64(telemetry["telemetry_checksum"], f"{label}.telemetry_checksum")
    if checksum == 0 or checksum != compute_telemetry_checksum(telemetry):
        raise EvidenceError(f"{label}.telemetry_checksum does not authenticate the leaf")
    return telemetry


def _arm_capture(
    value: Any,
    label: str,
    *,
    expected_arm: int,
    pair: Mapping[str, Any],
    raw_attempt: Mapping[str, Any],
    raw_record: Mapping[str, Any],
    roster: tuple[tuple[int, int], ...],
) -> Mapping[str, Any]:
    capture = _object(value, label)
    _fields(capture, _ARM_FIELDS, label)
    _version(capture, label)
    for field in ("arm", "repetition_index"):
        _u32(capture[field], f"{label}.{field}")
    if capture["arm"] != expected_arm:
        raise EvidenceError(f"{label}.arm is reversed or duplicated")
    if capture["repetition_index"] != pair["repetition_index"]:
        raise EvidenceError(f"{label}.repetition_index differs from Raw")
    if _u32(capture["execution_order"], f"{label}.execution_order") not in {0, 1}:
        raise EvidenceError(f"{label}.execution_order is unknown")
    if capture["execution_order"] != pair["execution_order"]:
        raise EvidenceError(f"{label}.execution_order differs from Raw")
    associations = {
        "dispatch_ordinal": raw_attempt["dispatch_ordinal"],
        "process_instance_identity": raw_attempt["process_instance_identity"],
        "associated_semantic_checksum": raw_record["semantics"]["semantic_checksum"],
        "associated_arm_artifact_checksum": raw_record["artifact_checksum"],
        "associated_authority_checksum": raw_record["external_observation"]["authority_checksum"],
        "associated_arm_attempt_checksum": raw_attempt["attempt_checksum"],
    }
    for field, expected in associations.items():
        actual = _u64(capture[field], f"{label}.{field}")
        if actual == 0 or actual != expected:
            raise EvidenceError(f"{label}.{field} differs from Raw")
    _telemetry(
        capture["telemetry"],
        f"{label}.telemetry",
        raw_semantics=raw_record["semantics"],
        roster=roster,
    )
    checksum = _u64(capture["capture_checksum"], f"{label}.capture_checksum")
    if checksum == 0 or checksum != compute_arm_capture_checksum(capture):
        raise EvidenceError(f"{label}.capture_checksum does not authenticate the arm join")
    return capture


def validate_join(
    raw: Any,
    sidecar: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> Mapping[str, Any]:
    if not allow_unstamped and (expected_repetitions != 20 or expected_workers != 4):
        raise EvidenceError("testing cardinality overrides require --testing-allow-unstamped")
    raw_validator.validate_same_run_document_v2(
        raw,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
    )
    raw_document = _object(raw, "raw cell")
    document = _object(sidecar, "same-run telemetry")
    _fields(document, _TOP_FIELDS, "same-run telemetry")
    source_commit = document["source_commit"]
    if not isinstance(source_commit, str):
        raise EvidenceError("source_commit must be a string")
    source_stamped = _bool(document["source_stamped"], "source_stamped")
    source_dirty = _bool(document["source_tree_dirty"], "source_tree_dirty")
    if not allow_unstamped:
        if expected_commit is None or _COMMIT.fullmatch(expected_commit) is None:
            raise EvidenceError("publication requires an independently supplied expected commit")
        if (
            _COMMIT.fullmatch(source_commit) is None
            or source_commit != expected_commit
            or not source_stamped
            or source_dirty
        ):
            raise EvidenceError("same-run telemetry is not from the expected clean stamped commit")
    if (
        source_commit != raw_document["source_commit"]
        or source_stamped != raw_document["source_stamped"]
        or source_dirty != raw_document["source_tree_dirty"]
    ):
        raise EvidenceError("same-run and Raw source envelopes differ")
    if _u32(document["schema_version"], "schema_version") != 1:
        raise EvidenceError("schema_version must be 1")
    if _u32(document["raw_evidence_schema_version"], "raw_evidence_schema_version") != 2:
        raise EvidenceError("raw_evidence_schema_version must be 2")
    if _u32(document["raw_wire_schema_version"], "raw_wire_schema_version") != 2:
        raise EvidenceError("raw_wire_schema_version must be 2")
    if _u32(document["telemetry_wire_schema_version"], "telemetry_wire_schema_version") != 2:
        raise EvidenceError("telemetry_wire_schema_version must be 2")
    if document["raw_evidence_schema_version"] != raw_document["raw_evidence_schema_version"]:
        raise EvidenceError("same-run Raw evidence schema association differs")
    if not _exact_equal(document["config"], raw_document["config"]):
        raise EvidenceError("same-run config differs from Raw")
    raw_associations = {
        "corpus_checksum": raw_document["corpus_checksum"],
        "raw_cell_plan_checksum": raw_document["cell_plan_checksum"],
        "raw_environment_checksum": raw_document["environment"]["environment_checksum"],
        "raw_authority_run_identity": raw_document["authority_run_identity"],
        "raw_controller_identity": raw_document["controller_identity"],
        "raw_cell_artifact_checksum": raw_document["artifact_checksum"],
        "raw_source_envelope_checksum": raw_document["source_envelope_checksum"],
    }
    for field, expected in raw_associations.items():
        actual = _u64(document[field], field)
        if actual == 0 or actual != expected:
            raise EvidenceError(f"{field} differs from Raw")
    try:
        protocol_validator.read_protocol()
        cells = {
            (case_id, pool): role
            for case_id, pool, role, disposition in protocol_validator.expanded_cells()
            if disposition == "same_run_raw_success" and role in {"exact", "heldout", "imported"}
        }
    except ValueError as error:
        raise EvidenceError(f"cannot authenticate the frozen decision protocol: {error}") from error
    config = raw_document["config"]
    if cells.get((config["case_id"], config["requested_pool_size"])) is None:
        raise EvidenceError("same-run telemetry cell is outside the frozen decision scope")
    try:
        _, roster = roster_validator.validated_successful_case_roster(config["case_id"])
    except roster_validator.ManifestError as error:
        raise EvidenceError(f"cannot authenticate the workload roster: {error}") from error
    attempts = _array(document["attempts"], "attempts")
    raw_attempts = raw_document["attempts"]
    if len(attempts) != expected_repetitions or len(attempts) != len(raw_attempts):
        raise EvidenceError("same-run telemetry must contain every Raw repetition")
    previous_pair_checksum = None
    for index, (raw_pair_value, raw_capture) in enumerate(zip(attempts, raw_attempts, strict=True)):
        label = f"attempts[{index}]"
        capture = _object(raw_pair_value, label)
        _fields(capture, _PAIR_FIELDS, label)
        _version(capture, label)
        raw_pair = _object(raw_capture, f"raw.attempts[{index}]")
        expected_scalars = {
            "case_id": raw_pair["case_id"],
            "requested_pool_size": raw_pair["requested_pool_size"],
            "repetition_index": raw_pair["repetition_index"],
            "root_seed": raw_pair["root_seed"],
            "execution_order": raw_pair["execution_order"],
            "associated_raw_pair_attempt_checksum": raw_pair["attempt_checksum"],
            "associated_paired_semantic_checksum": raw_pair["result"]["semantic_checksum"],
            "associated_paired_artifact_checksum": raw_pair["result"]["artifact_checksum"],
        }
        for field, expected in expected_scalars.items():
            actual = _u64(capture[field], f"{label}.{field}")
            if field in {"case_id", "requested_pool_size", "repetition_index", "execution_order"}:
                actual = _u32(capture[field], f"{label}.{field}")
            if actual != expected:
                raise EvidenceError(f"{label}.{field} differs from Raw")
        _arm_capture(
            capture["baseline"],
            f"{label}.baseline",
            expected_arm=0,
            pair=raw_pair,
            raw_attempt=raw_pair["baseline"],
            raw_record=raw_pair["result"]["baseline"],
            roster=roster,
        )
        _arm_capture(
            capture["candidate"],
            f"{label}.candidate",
            expected_arm=1,
            pair=raw_pair,
            raw_attempt=raw_pair["candidate"],
            raw_record=raw_pair["result"]["candidate"],
            roster=roster,
        )
        checksum = _u64(capture["capture_checksum"], f"{label}.capture_checksum")
        if checksum == 0 or checksum != compute_pair_capture_checksum(capture):
            raise EvidenceError(f"{label}.capture_checksum does not authenticate the pair join")
        if previous_pair_checksum == checksum:
            raise EvidenceError("adjacent same-run pair captures unexpectedly share identity")
        previous_pair_checksum = checksum
    artifact_checksum = _u64(document["artifact_checksum"], "artifact_checksum")
    if artifact_checksum == 0 or artifact_checksum != compute_cell_capture_checksum(document):
        raise EvidenceError("artifact_checksum does not authenticate the same-run cell")
    source_checksum = _u64(document["source_envelope_checksum"], "source_envelope_checksum")
    if source_checksum == 0 or source_checksum != compute_source_envelope_checksum(document):
        raise EvidenceError("source_envelope_checksum does not authenticate the source envelope")
    return document


def exact_rejection_guardrail_passes(document: Mapping[str, Any]) -> bool:
    """Return the frozen decision guardrail result after structural validation."""
    return all(
        row["columns"]["exact_validation_rejections"] == 0
        for attempt in document["attempts"]
        for arm in (attempt["baseline"], attempt["candidate"])
        for row in arm["telemetry"]["per_net"]
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--testing-repetitions", type=int, default=20)
    parser.add_argument("--testing-workers", type=int, default=4)
    parser.add_argument("--expected-commit")
    parser.add_argument("raw", type=pathlib.Path)
    parser.add_argument("sidecar", type=pathlib.Path)
    options = parser.parse_args(argv)
    if not options.testing_allow_unstamped and (
        options.testing_repetitions != 20 or options.testing_workers != 4
    ):
        parser.error(
            "--testing-repetitions and --testing-workers require --testing-allow-unstamped"
        )
    try:
        raw = raw_validator.read_document(options.raw)
        sidecar = read_document(options.sidecar)
        validate_join(
            raw,
            sidecar,
            allow_unstamped=options.testing_allow_unstamped,
            expected_commit=options.expected_commit,
            expected_repetitions=options.testing_repetitions,
            expected_workers=options.testing_workers,
        )
    except EvidenceError as error:
        print(f"Phase 4 same-run telemetry validation failed: {error}", file=sys.stderr)
        return 1
    status = "passed" if exact_rejection_guardrail_passes(sidecar) else "failed"
    print(
        "validated 1 Phase 4 same-run decision telemetry artifact; "
        f"exact-rejection guardrail {status}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
