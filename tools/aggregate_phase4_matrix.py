"""Validate and aggregate the frozen 104-cell Phase 4 evidence matrix."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import pathlib
import re
import secrets
import stat
import sys
from collections.abc import Mapping, Sequence
from fractions import Fraction
from typing import Any

from tools import project_phase4_fixed_query_controls as fixed_query
from tools import project_phase4_operational_evidence as operational_v1
from tools import project_phase4_stress_evidence as stress
from tools import validate_phase4_exact_small_oracle as oracle_v1
from tools import validate_phase4_exact_small_oracle_v2 as oracle_v2
from tools import validate_phase4_operational_measurement as operational
from tools import validate_phase4_per_net_report as report_v1
from tools import validate_phase4_per_net_report_v2 as report_v2
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run
from tools import validate_phase4_statistical_protocol as protocol_v1
from tools import validate_phase4_statistical_protocol_v4 as protocol_v4

_COMMIT = re.compile(r"[0-9a-f]{40}")
_MAXIMUM_NESTING_DEPTH = 64
_MAXIMUM_DECISION_BYTES = 16 * 1024 * 1024
_MAXIMUM_AUTHORITY_BYTES = {
    "raw": 64 * 1024 * 1024,
    "same_run": 32 * 1024 * 1024,
    "report": 32 * 1024 * 1024,
    "capture": 32 * 1024 * 1024,
    "operational": 32 * 1024 * 1024,
    "exact_snapshot": 64 * 1024 * 1024,
    "exact_oracle": 1 * 1024 * 1024,
    "fixed_query": 4 * 1024 * 1024,
    "stress_probe": 64 * 1024,
    "stress": 4 * 1024 * 1024,
    "decision": 16 * 1024 * 1024,
    "root_marker": 4 * 1024,
}
_PROTOCOL_CHECKSUMS = [
    6007340189832929403,
    16250482876258734537,
    14444535493088350158,
    10222448264116898730,
]
_CORPUS_CHECKSUM = 7311872938254494931
_BUDGET_ROSTER_CHECKSUM = 13115713216042861392


class MatrixError(ValueError):
    """Stable failure for incomplete, malformed, or inconsistent matrix evidence."""


def _canonical(value: Any) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _reject_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise MatrixError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise MatrixError(f"non-JSON numeric constant: {value}")


def _check_depth(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > _MAXIMUM_NESTING_DEPTH:
            raise MatrixError("JSON exceeds the nesting-depth bound")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def _parse_authority_bytes(
    encoded: bytes,
    relative: pathlib.PurePosixPath,
    kind: str,
) -> tuple[Mapping[str, Any], dict[str, Any]]:
    maximum = _MAXIMUM_AUTHORITY_BYTES[kind]
    if len(encoded) > maximum:
        raise MatrixError(f"{relative} exceeds the {kind} input bound")
    if (
        encoded.startswith(b"\xef\xbb\xbf")
        or not encoded.endswith(b"\n")
        or encoded.endswith(b"\n\n")
    ):
        raise MatrixError(f"{relative} must be UTF-8 with exactly one terminal LF")
    try:
        text = encoded.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_pairs,
            parse_constant=_reject_constant,
        )
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise MatrixError(f"cannot parse {relative}: {error}") from error
    _check_depth(value)
    if not isinstance(value, dict) or text != _canonical(value) + "\n":
        raise MatrixError(f"{relative} is not canonical compact JSON")
    binding = {
        "kind": kind,
        "path": relative.as_posix(),
        "size_bytes": len(encoded),
        "sha256": hashlib.sha256(encoded).hexdigest(),
        "artifact_checksum": value.get("artifact_checksum"),
        "source_envelope_checksum": value.get("source_envelope_checksum"),
    }
    if (
        isinstance(binding["artifact_checksum"], bool)
        or not isinstance(binding["artifact_checksum"], int)
        or binding["artifact_checksum"] <= 0
    ):
        raise MatrixError(f"{relative} lacks a nonzero artifact checksum")
    source_checksum = binding["source_envelope_checksum"]
    if source_checksum is not None and (
        isinstance(source_checksum, bool)
        or not isinstance(source_checksum, int)
        or source_checksum <= 0
    ):
        raise MatrixError(f"{relative} has an invalid source-envelope checksum")
    return value, binding


def _assert_exact(expected: Any, actual: Any, label: str) -> None:
    if type(actual) is not type(expected):
        raise MatrixError(f"{label} has a noncanonical JSON type")
    if isinstance(expected, dict):
        if tuple(actual) != tuple(expected):
            raise MatrixError(f"{label} fields or field order differ")
        for key in expected:
            _assert_exact(expected[key], actual[key], f"{label}.{key}")
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            raise MatrixError(f"{label} length differs")
        for index, (expected_child, actual_child) in enumerate(zip(expected, actual, strict=True)):
            _assert_exact(expected_child, actual_child, f"{label}[{index}]")
    elif actual != expected:
        raise MatrixError(f"{label} differs from its rebuilt authority")


def _read_authority(
    root: pathlib.Path,
    relative: pathlib.PurePosixPath,
    kind: str,
) -> tuple[Mapping[str, Any], dict[str, Any]]:
    if relative.is_absolute() or ".." in relative.parts or "." in relative.parts:
        raise MatrixError("authority path is not a canonical relative path")
    directory_descriptor: int | None = None
    file_descriptor: int | None = None
    try:
        directory_descriptor = os.open(
            root,
            os.O_RDONLY | getattr(os, "O_DIRECTORY", 0) | getattr(os, "O_CLOEXEC", 0),
        )
        for component in relative.parts[:-1]:
            next_descriptor = os.open(
                component,
                os.O_RDONLY
                | getattr(os, "O_DIRECTORY", 0)
                | getattr(os, "O_CLOEXEC", 0)
                | getattr(os, "O_NOFOLLOW", 0),
                dir_fd=directory_descriptor,
            )
            os.close(directory_descriptor)
            directory_descriptor = next_descriptor
        file_descriptor = os.open(
            relative.name,
            os.O_RDONLY
            | getattr(os, "O_CLOEXEC", 0)
            | getattr(os, "O_NOFOLLOW", 0)
            | getattr(os, "O_NONBLOCK", 0),
            dir_fd=directory_descriptor,
        )
        status = os.fstat(file_descriptor)
        if not stat.S_ISREG(status.st_mode):
            raise MatrixError(f"{relative} is not a regular non-symlink authority")
        if status.st_nlink != 1:
            raise MatrixError(f"{relative} has an aliased hard-link identity")
        maximum = _MAXIMUM_AUTHORITY_BYTES[kind]
        chunks: list[bytes] = []
        remaining = maximum + 1
        while remaining > 0:
            chunk = os.read(file_descriptor, min(1024 * 1024, remaining))
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        encoded = b"".join(chunks)
        final_status = os.fstat(file_descriptor)
        if (
            (final_status.st_dev, final_status.st_ino) != (status.st_dev, status.st_ino)
            or final_status.st_size != status.st_size
            or final_status.st_mtime_ns != status.st_mtime_ns
            or len(encoded) != status.st_size
        ):
            raise MatrixError(f"{relative} changed during its pinned read")
    except OSError as error:
        raise MatrixError(f"cannot read required {kind} authority {relative}: {error}") from error
    finally:
        if file_descriptor is not None:
            os.close(file_descriptor)
        if directory_descriptor is not None:
            os.close(directory_descriptor)
    return _parse_authority_bytes(encoded, relative, kind)


def _cell_path(case_id: int, pool: int, name: str) -> pathlib.PurePosixPath:
    return pathlib.PurePosixPath("cells", f"{case_id:04d}", f"k{pool}", name)


def _special_path(name: str) -> pathlib.PurePosixPath:
    return pathlib.PurePosixPath("special", name)


def _fraction(value: Fraction) -> dict[str, int]:
    return {"numerator": value.numerator, "denominator": value.denominator}


def _outcome(semantics: Mapping[str, Any]) -> dict[str, int]:
    observed = semantics["outcome"]
    return {
        "selected_net_count": observed["selected_net_count"],
        "total_overuse_units": observed["total_overuse_units"],
        "total_intrinsic_cost": observed["total_intrinsic_cost"],
    }


def _comparison_name(comparison: int) -> str:
    return {1: "candidate_win", 0: "tie", -1: "candidate_loss"}[comparison]


def _group_metadata() -> dict[tuple[int, int], dict[str, Any]]:
    rows: dict[tuple[int, int], dict[str, Any]] = {}
    for group in protocol_v4.effective_cell_groups():
        for case_id, pool, role, evidence in protocol_v1._expand_groups([group]):
            key = (case_id, pool)
            if key in rows:
                raise MatrixError("frozen protocol contains a duplicate cell")
            rows[key] = {
                "role": role,
                "evidence_requirement": evidence,
                "in_noncalibration_closure": group["in_noncalibration_closure"],
                "required_artifacts": copy.deepcopy(group["required_artifacts"]),
            }
    if len(rows) != 104:
        raise MatrixError("frozen protocol does not expand to 104 cells")
    return rows


def _binding_reference(binding: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "kind": binding["kind"],
        "path": binding["path"],
        "artifact_checksum": binding["artifact_checksum"],
        "source_envelope_checksum": binding["source_envelope_checksum"],
    }


def _cell_checksum(row: Mapping[str, Any]) -> int:
    payload = {key: value for key, value in row.items() if key != "cell_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-MATRIX-DECISION-CELL-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def validate_success_cell_documents(
    *,
    raw: Mapping[str, Any],
    sidecar: Mapping[str, Any] | None,
    report: Mapping[str, Any],
    capture: Mapping[str, Any],
    publication: Mapping[str, Any],
    expected_commit: str,
    role: str,
    evidence_requirement: str,
    in_noncalibration_closure: bool,
    bindings: Sequence[Mapping[str, Any]],
    snapshot: Mapping[str, Any] | None = None,
    oracle_artifact: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Validate one complete success-cell authority set and normalize it."""
    if evidence_requirement == "same_run_raw_success":
        if sidecar is None:
            raise MatrixError("Raw-v2 success cell lacks same-run telemetry")
        same_run.validate_join(raw, sidecar, expected_commit=expected_commit)
        report_v2.validate_join(
            raw,
            sidecar,
            report,
            expected_commit=expected_commit,
        )
    elif evidence_requirement == "raw_success":
        if sidecar is not None:
            raise MatrixError("Raw-v1 success cell unexpectedly has same-run telemetry")
        raw_validator.validate_document(raw, expected_commit=expected_commit)
        report_v1.validate_join(raw, report, expected_commit=expected_commit)
    else:
        raise MatrixError("success cell has a non-success evidence disposition")
    operational.validate_capture(capture, expected_commit=expected_commit)
    operational.validate_publication(raw, capture, sidecar, publication)

    config = raw["config"]
    case_id = config["case_id"]
    pool = config["requested_pool_size"]
    if publication["cell_role"] != role or publication["cell_config"] != config:
        raise MatrixError("operational publication role or config differs from Raw")
    exact_binding: Mapping[str, Any] | None = None
    if role == "exact":
        if sidecar is None or snapshot is None or oracle_artifact is None:
            raise MatrixError("exact cell lacks snapshot or Oracle v2 authority")
        rebuilt_oracle = oracle_v2.validate_publication(
            raw,
            sidecar,
            report,
            snapshot,
            expected_commit=expected_commit,
        )
        oracle_v2.validate_oracle_artifact(oracle_artifact)
        _assert_exact(rebuilt_oracle, oracle_artifact, "exact Oracle v2")
        if not oracle_artifact["production_is_optimal"]:
            raise MatrixError("exact-small production result is not fixed-pool optimal")
        exact_binding = {
            "snapshot_artifact_checksum": snapshot["artifact_checksum"],
            "snapshot_source_envelope_checksum": snapshot["source_envelope_checksum"],
            "oracle_artifact_checksum": oracle_artifact["artifact_checksum"],
            "oracle_source_envelope_checksum": oracle_artifact["source_envelope_checksum"],
            "production_is_optimal": True,
        }
    elif snapshot is not None or oracle_artifact is not None:
        raise MatrixError("non-exact cell carries exact-small authorities")

    first = raw["attempts"][0]
    if first["result"] is None:
        raise MatrixError("successful Raw cell lacks a repetition-zero result")
    baseline = _outcome(first["result"]["baseline"]["semantics"])
    candidate = _outcome(first["result"]["candidate"]["semantics"])
    comparison = protocol_v1.compare_outcomes(candidate, baseline)
    timing_rows: list[dict[str, Any]] = []
    for repetition, attempt in enumerate(raw["attempts"]):
        if attempt["result"] is None:
            raise MatrixError("successful Raw cell has an incomplete repetition")
        observed_baseline = _outcome(attempt["result"]["baseline"]["semantics"])
        observed_candidate = _outcome(attempt["result"]["candidate"]["semantics"])
        if observed_baseline != baseline or observed_candidate != candidate:
            raise MatrixError("Raw repetitions disagree on deterministic board outcome")
        timing_rows.append(
            {
                "repetition": repetition,
                "order": "AB" if attempt["execution_order"] == 0 else "BA",
                "baseline_outer_ns": attempt["baseline"]["outer_elapsed_nanoseconds"],
                "candidate_outer_ns": attempt["candidate"]["outer_elapsed_nanoseconds"],
            }
        )

    equal_cost_guardrail = not (
        candidate["selected_net_count"] == baseline["selected_net_count"]
        and candidate["total_overuse_units"] == baseline["total_overuse_units"]
        and candidate["total_intrinsic_cost"] > baseline["total_intrinsic_cost"]
    )
    selected_guardrail: Mapping[str, Any]
    imported_guardrail: Mapping[str, Any]
    if role in {"heldout", "imported"}:
        selected_guardrail = {
            "status": "evaluated",
            "passed": (candidate["selected_net_count"] >= baseline["selected_net_count"]),
        }
    else:
        selected_guardrail = {
            "status": "not_applicable",
            "reason": "selected_net_nonregression_scope_is_heldout_and_imported",
        }
    if role == "imported":
        imported_guardrail = {
            "status": "evaluated",
            "passed": protocol_v1.guardrail_cell(
                candidate,
                baseline,
                imported=True,
            ),
        }
    else:
        imported_guardrail = {
            "status": "not_applicable",
            "reason": "imported_guardrails_apply_only_to_imported_cells",
        }
    exact_rejection: Mapping[str, Any]
    if role in {"exact", "heldout", "imported"}:
        if sidecar is None:
            raise MatrixError("exact-rejection scope lacks same-run authority")
        exact_rejection = {
            "status": "evaluated",
            "passed": same_run.exact_rejection_guardrail_passes(sidecar),
        }
    else:
        exact_rejection = {
            "status": "not_applicable",
            "reason": "exact_rejection_scope_is_exact_heldout_and_imported",
        }

    row: dict[str, Any] = {
        "schema_version": 1,
        "case_id": case_id,
        "requested_pool_size": pool,
        "role": role,
        "evidence_requirement": evidence_requirement,
        "in_noncalibration_closure": in_noncalibration_closure,
        "authority_bindings": [_binding_reference(binding) for binding in bindings],
        "baseline_outcome": baseline,
        "candidate_outcome": candidate,
        "comparison": _comparison_name(comparison),
        "guardrails": {
            "equal_selected_and_overuse_cost_nonregression": {
                "status": "evaluated",
                "passed": equal_cost_guardrail,
            },
            "selected_net_nonregression": selected_guardrail,
            "imported_nonregression": imported_guardrail,
            "same_run_exact_rejection": exact_rejection,
        },
        "timing_diagnostic": protocol_v1.timing_summary(timing_rows),
        "exact_small": (
            exact_binding
            if exact_binding is not None
            else {
                "status": "not_applicable",
                "reason": "cell_is_not_exact_small",
            }
        ),
        "cell_checksum": 0,
    }
    row["cell_checksum"] = _cell_checksum(row)
    return row


def _special_cell(
    *,
    case_id: int,
    pool: int,
    role: str,
    evidence_requirement: str,
    in_noncalibration_closure: bool,
    binding: Mapping[str, Any],
    reason: str,
) -> dict[str, Any]:
    row: dict[str, Any] = {
        "schema_version": 1,
        "case_id": case_id,
        "requested_pool_size": pool,
        "role": role,
        "evidence_requirement": evidence_requirement,
        "in_noncalibration_closure": in_noncalibration_closure,
        "authority_bindings": [_binding_reference(binding)],
        "baseline_outcome": {"status": "not_applicable", "reason": reason},
        "candidate_outcome": {"status": "not_applicable", "reason": reason},
        "comparison": "not_applicable",
        "guardrails": {
            "equal_selected_and_overuse_cost_nonregression": {
                "status": "not_applicable",
                "reason": reason,
            },
            "selected_net_nonregression": {
                "status": "not_applicable",
                "reason": reason,
            },
            "imported_nonregression": {
                "status": "not_applicable",
                "reason": reason,
            },
            "same_run_exact_rejection": {
                "status": "not_applicable",
                "reason": reason,
            },
        },
        "timing_diagnostic": {"status": "not_applicable", "reason": reason},
        "exact_small": {"status": "not_applicable", "reason": reason},
        "cell_checksum": 0,
    }
    row["cell_checksum"] = _cell_checksum(row)
    return row


def _family_results(cells: Sequence[Mapping[str, Any]]) -> tuple[list[dict[str, Any]], bool]:
    by_key = {
        (row["case_id"], row["requested_pool_size"]): row
        for row in cells
        if row["role"] == "heldout"
    }
    families = protocol_v1.expected_protocol()["families"]
    counts: dict[int, tuple[int, int, int]] = {}
    pool_counts: dict[int, dict[int, tuple[int, int, int]]] = {}
    for family in families:
        family_id = family["family_id"]
        strata: dict[int, tuple[int, int, int]] = {}
        for pool in (4, 8, 16):
            comparisons = [
                by_key[(case_id, pool)]["comparison"] for case_id in family["heldout_case_ids"]
            ]
            strata[pool] = (
                comparisons.count("candidate_win"),
                comparisons.count("candidate_loss"),
                comparisons.count("tie"),
            )
        counts[family_id] = strata[8]
        pool_counts[family_id] = strata
    qualified = set(protocol_v1.holm_qualifying_families(counts))
    ordering = sorted((protocol_v1.exact_sign_p(*counts[f][:2]), f) for f in counts)
    ranks = {family: rank for rank, (_, family) in enumerate(ordering)}
    results: list[dict[str, Any]] = []
    for family in families:
        family_id = family["family_id"]
        wins, losses, ties = counts[family_id]
        lower, upper = protocol_v1.tie_bounded_win_fraction(wins, losses, ties)
        rank = ranks[family_id]
        threshold = Fraction(1, 20 * (3 - rank))
        results.append(
            {
                "family_id": family_id,
                "name": family["name"],
                "globally_coupled_conflict_graph": family["globally_coupled_conflict_graph"],
                "primary_pool": 8,
                "primary_counts": {
                    "wins": wins,
                    "losses": losses,
                    "ties": ties,
                },
                "sensitivity_counts": [
                    {
                        "pool": pool,
                        "wins": pool_counts[family_id][pool][0],
                        "losses": pool_counts[family_id][pool][1],
                        "ties": pool_counts[family_id][pool][2],
                    }
                    for pool in (4, 16)
                ],
                "sign_p_value": _fraction(protocol_v1.exact_sign_p(wins, losses)),
                "holm_rank": rank,
                "holm_threshold": _fraction(threshold),
                "holm_qualified": family_id in qualified,
                "tie_bounded_win_fraction": {
                    "lower": _fraction(lower),
                    "upper": _fraction(upper),
                },
                "clopper_pearson_95_ppb": protocol_v1.clopper_pearson_ppb(
                    wins,
                    losses,
                ),
            }
        )
    family_gate = len(qualified) >= 2 and 1 in qualified
    return results, family_gate


def _decision_checksum(value: Mapping[str, Any]) -> int:
    payload = {
        key: item
        for key, item in value.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-MATRIX-DECISION-PUBLICATION-ARTIFACT-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _source_checksum(value: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-MATRIX-DECISION-PUBLICATION-SOURCE-V1")
    hashed.u32(value["schema_version"])
    hashed.string(value["source_commit"])
    hashed.boolean(value["source_stamped"])
    hashed.boolean(value["source_tree_dirty"])
    hashed.u64(value["artifact_checksum"])
    return hashed.finish()


def _root_marker(expected_commit: str) -> dict[str, Any]:
    value = {
        "schema_version": 1,
        "expected_commit": expected_commit,
        "artifact_checksum": 0,
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-MATRIX-EVIDENCE-ROOT-V1")
    hashed.u32(value["schema_version"])
    hashed.string(value["expected_commit"])
    value["artifact_checksum"] = hashed.finish()
    return value


def validate_root_marker(root: pathlib.Path, expected_commit: str) -> None:
    marker, _ = _read_authority(
        root,
        pathlib.PurePosixPath(".phase4-root.json"),
        "root_marker",
    )
    _assert_exact(_root_marker(expected_commit), marker, "evidence-root marker")


def aggregate_validated_cells(
    *,
    expected_commit: str,
    cells: Sequence[Mapping[str, Any]],
    evidence_files: Sequence[Mapping[str, Any]],
    host_environment: Mapping[str, Any],
    reproducibility_provenance: Mapping[str, Any],
    fixed_binding: Mapping[str, Any],
    stress_binding: Mapping[str, Any],
) -> dict[str, Any]:
    """Apply the frozen decision to a complete, already validated matrix."""
    expected_kind_counts = {
        "raw": 100,
        "same_run": 78,
        "report": 100,
        "capture": 100,
        "operational": 100,
        "exact_snapshot": 3,
        "exact_oracle": 3,
        "fixed_query": 1,
        "stress_probe": 1,
        "stress": 1,
    }
    observed_kind_counts = {
        kind: sum(binding["kind"] == kind for binding in evidence_files)
        for kind in expected_kind_counts
    }
    evidence_paths = [binding["path"] for binding in evidence_files]
    if (
        observed_kind_counts != expected_kind_counts
        or evidence_paths != sorted(evidence_paths)
        or len(evidence_paths) != len(set(evidence_paths))
    ):
        raise MatrixError("evidence-file registry is incomplete, duplicated, or reordered")
    metadata = _group_metadata()
    expected_order = sorted(metadata)
    actual_order = [(row["case_id"], row["requested_pool_size"]) for row in cells]
    if actual_order != expected_order or len(set(actual_order)) != 104:
        raise MatrixError("normalized matrix is missing, duplicated, or reordered")
    for row in cells:
        meta = metadata[(row["case_id"], row["requested_pool_size"])]
        if (
            row["role"] != meta["role"]
            or row["evidence_requirement"] != meta["evidence_requirement"]
            or row["in_noncalibration_closure"] != meta["in_noncalibration_closure"]
            or row["cell_checksum"] != _cell_checksum(row)
        ):
            raise MatrixError("normalized cell differs from the frozen protocol")

    family_results, family_gate = _family_results(cells)
    success = [
        row
        for row in cells
        if row["evidence_requirement"] in {"raw_success", "same_run_raw_success"}
    ]
    equal_cost_failures = [
        [row["case_id"], row["requested_pool_size"]]
        for row in success
        if not row["guardrails"]["equal_selected_and_overuse_cost_nonregression"]["passed"]
    ]
    selected_failures = [
        [row["case_id"], row["requested_pool_size"]]
        for row in success
        if row["guardrails"]["selected_net_nonregression"]["status"] == "evaluated"
        and not row["guardrails"]["selected_net_nonregression"]["passed"]
    ]
    imported_failures = [
        [row["case_id"], row["requested_pool_size"]]
        for row in success
        if row["guardrails"]["imported_nonregression"]["status"] == "evaluated"
        and not row["guardrails"]["imported_nonregression"]["passed"]
    ]
    exact_rejection_failures = [
        [row["case_id"], row["requested_pool_size"]]
        for row in success
        if row["guardrails"]["same_run_exact_rejection"]["status"] == "evaluated"
        and not row["guardrails"]["same_run_exact_rejection"]["passed"]
    ]
    guardrails_passed = not (
        equal_cost_failures or selected_failures or imported_failures or exact_rejection_failures
    )
    completion = [
        {"name": "exact_small_exhaustive_oracle", "complete": True, "authority_count": 3},
        {"name": "fixed_query_controls", "complete": True, "authority_count": 5},
        {"name": "stress_ladder", "complete": True, "authority_count": 3},
        {"name": "same_run_decision_telemetry", "complete": True, "authority_count": 78},
        {"name": "complete_stage_timing", "complete": True, "authority_count": 100},
        {"name": "cpu_gpu_utilization_context", "complete": True, "authority_count": 100},
        {"name": "compatible_batch_fill", "complete": True, "authority_count": 100},
        {"name": "prepared_view_cache_behavior", "complete": True, "authority_count": 100},
        {
            "name": "complete_toolchain_hardware_provenance",
            "complete": True,
            "authority_count": 100,
        },
    ]
    phase4_complete = family_gate and guardrails_passed
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": expected_commit,
        "source_stamped": True,
        "source_tree_dirty": False,
        "matrix_evidence_status": "complete",
        "phase4_exit_status": "passed" if phase4_complete else "failed",
        "phase4_complete": phase4_complete,
        "protocol_binding": {
            "schema_versions": [1, 2, 3, 4],
            "artifact_checksums": list(_PROTOCOL_CHECKSUMS),
            "corpus_checksum": _CORPUS_CHECKSUM,
            "canonical_budget_roster_cell_count": 102,
            "canonical_budget_roster_checksum": _BUDGET_ROSTER_CHECKSUM,
        },
        "coverage": {
            "logical_cell_count": 104,
            "successful_cell_count": 100,
            "legacy_raw_v1_success_cell_count": 22,
            "same_run_raw_v2_success_cell_count": 78,
            "noncalibration_closure_cell_count": 86,
            "role_cell_counts": [
                {"role": role, "count": count}
                for role, count in (
                    ("exact", 3),
                    ("calibration", 18),
                    ("heldout", 72),
                    ("fixed_query", 5),
                    ("stress", 3),
                    ("imported", 3),
                )
            ],
        },
        "host_environment": copy.deepcopy(host_environment),
        "reproducibility_provenance": copy.deepcopy(reproducibility_provenance),
        "shared_authority_bindings": {
            "fixed_query": _binding_reference(fixed_binding),
            "stress": _binding_reference(stress_binding),
        },
        "evidence_files": [copy.deepcopy(binding) for binding in evidence_files],
        "cells": [copy.deepcopy(row) for row in cells],
        "families": family_results,
        "guardrail_summary": {
            "passed": guardrails_passed,
            "equal_selected_and_overuse_cost_failures": equal_cost_failures,
            "heldout_or_imported_selected_net_failures": selected_failures,
            "imported_nonregression_failures": imported_failures,
            "same_run_exact_rejection_failures": exact_rejection_failures,
        },
        "family_decision": {
            "passed": family_gate,
            "minimum_qualifying_primary_families": 2,
            "required_qualifying_family_id": 1,
            "qualifying_family_ids": [
                row["family_id"] for row in family_results if row["holm_qualified"]
            ],
        },
        "completion_requirements": completion,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = _decision_checksum(result)
    result["source_envelope_checksum"] = _source_checksum(result)
    return result


def _verify_inventory(root: pathlib.Path, expected_files: set[str]) -> None:
    observed_files: set[str] = set()
    observed_directories: set[str] = set()
    expected_directories = {"cells", "special"}
    for name in expected_files:
        parent = pathlib.PurePosixPath(name).parent
        while parent.parts:
            expected_directories.add(parent.as_posix())
            parent = parent.parent
    allowed_top_level = {
        ".phase4-root.json",
        ".phase4-run.lock",
        "cells",
        "logs",
        "matrix",
        "special",
    }
    try:
        top_level = list(root.iterdir())
    except OSError as error:
        raise MatrixError(f"cannot inspect evidence-root inventory: {error}") from error
    for child in top_level:
        if child.name not in allowed_top_level:
            raise MatrixError(f"evidence root contains unknown operator state: {child.name}")
        if child.name in {"logs", "matrix"} and (
            child.is_symlink() or not stat.S_ISDIR(child.lstat().st_mode)
        ):
            raise MatrixError(f"{child.name} operator state is not a regular directory")
        if child.name == ".phase4-run.lock" and (
            child.is_symlink() or not stat.S_ISREG(child.lstat().st_mode)
        ):
            raise MatrixError("evidence-root lock is not a regular file")
        if child.name == ".phase4-root.json" and (
            child.is_symlink() or not stat.S_ISREG(child.lstat().st_mode)
        ):
            raise MatrixError("evidence-root marker is not a regular file")
    for top in ("cells", "special"):
        start = root / top
        try:
            if not stat.S_ISDIR(start.lstat().st_mode) or start.is_symlink():
                raise MatrixError(f"{top} is not a regular evidence directory")
        except OSError as error:
            raise MatrixError(f"cannot inspect evidence directory {top}: {error}") from error
        for directory, names, files in os.walk(start, followlinks=False):
            directory_path = pathlib.Path(directory)
            observed_directories.add(directory_path.relative_to(root).as_posix())
            for name in names:
                child = directory_path / name
                if child.is_symlink() or not stat.S_ISDIR(child.lstat().st_mode):
                    raise MatrixError("evidence bundle contains an aliased directory")
            for name in files:
                child = directory_path / name
                observed_files.add(child.relative_to(root).as_posix())
    if observed_files != expected_files or observed_directories != expected_directories:
        missing = sorted(expected_files - observed_files)
        extra = sorted(observed_files - expected_files)
        missing_directories = sorted(expected_directories - observed_directories)
        extra_directories = sorted(observed_directories - expected_directories)
        raise MatrixError(
            "evidence bundle inventory differs; "
            f"missing={missing[:8]} extra={extra[:8]} "
            f"missing_directories={missing_directories[:8]} "
            f"extra_directories={extra_directories[:8]}"
        )


def aggregate_evidence_root(root: pathlib.Path, *, expected_commit: str) -> dict[str, Any]:
    """Fully validate a canonical external bundle and return its decision publication."""
    if _COMMIT.fullmatch(expected_commit) is None:
        raise MatrixError("aggregation requires an independently supplied clean commit")
    protocol_v4.read_protocol()
    try:
        root = root.resolve(strict=True)
    except OSError as error:
        raise MatrixError(f"cannot resolve evidence root: {error}") from error
    if not root.is_dir():
        raise MatrixError("evidence root is not a directory")
    validate_root_marker(root, expected_commit)

    metadata = _group_metadata()
    cells: list[dict[str, Any]] = []
    evidence_files: list[dict[str, Any]] = []
    expected_files: set[str] = set()
    common_environment: Mapping[str, Any] | None = None
    common_provenance: Mapping[str, Any] | None = None
    fixed_authorities: dict[
        int,
        tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]],
    ] = {}
    stress_authority: (
        tuple[
            Mapping[str, Any],
            Mapping[str, Any],
            Mapping[str, Any],
        ]
        | None
    ) = None

    def read(
        relative: pathlib.PurePosixPath,
        kind: str,
    ) -> tuple[Mapping[str, Any], dict[str, Any]]:
        document, binding = _read_authority(root, relative, kind)
        if binding["path"] in expected_files:
            raise MatrixError("evidence layout aliases one authority path")
        expected_files.add(binding["path"])
        evidence_files.append(binding)
        return document, binding

    for case_id, pool in sorted(metadata):
        meta = metadata[(case_id, pool)]
        evidence = meta["evidence_requirement"]
        role = meta["role"]
        if evidence not in {"raw_success", "same_run_raw_success"}:
            continue
        raw, raw_binding = read(_cell_path(case_id, pool, "raw.json"), "raw")
        sidecar: Mapping[str, Any] | None = None
        bindings: list[Mapping[str, Any]] = [raw_binding]
        if evidence == "same_run_raw_success":
            sidecar, sidecar_binding = read(
                _cell_path(case_id, pool, "same-run.json"),
                "same_run",
            )
            bindings.append(sidecar_binding)
        report, report_binding = read(
            _cell_path(case_id, pool, "per-net-report.json"),
            "report",
        )
        capture, capture_binding = read(
            _cell_path(case_id, pool, "operational-capture.json"),
            "capture",
        )
        publication, publication_binding = read(
            _cell_path(case_id, pool, "operational-publication.json"),
            "operational",
        )
        bindings.extend((report_binding, capture_binding, publication_binding))
        snapshot: Mapping[str, Any] | None = None
        oracle_artifact: Mapping[str, Any] | None = None
        if role == "exact":
            snapshot, snapshot_binding = read(
                _cell_path(case_id, pool, "exact-snapshot.json"),
                "exact_snapshot",
            )
            oracle_artifact, oracle_binding = read(
                _cell_path(case_id, pool, "exact-oracle.json"),
                "exact_oracle",
            )
            bindings.extend((snapshot_binding, oracle_binding))
        row = validate_success_cell_documents(
            raw=raw,
            sidecar=sidecar,
            report=report,
            capture=capture,
            publication=publication,
            expected_commit=expected_commit,
            role=role,
            evidence_requirement=evidence,
            in_noncalibration_closure=meta["in_noncalibration_closure"],
            bindings=bindings,
            snapshot=snapshot,
            oracle_artifact=oracle_artifact,
        )
        if (row["case_id"], row["requested_pool_size"]) != (case_id, pool):
            raise MatrixError("authority path differs from its frozen cell identity")
        if common_environment is None:
            common_environment = raw["environment"]
            common_provenance = publication["reproducibility_provenance"]
        elif (
            raw["environment"] != common_environment
            or publication["reproducibility_provenance"] != common_provenance
        ):
            raise MatrixError(
                "successful cells do not share one host/toolchain execution environment"
            )
        if case_id in {2002, 2003, 2004}:
            fixed_authorities[case_id] = (
                raw,
                report,
                operational_v1.project_document(raw),
            )
        if case_id == 3000:
            stress_authority = (
                raw,
                report,
                operational_v1.project_document(raw),
            )
        cells.append(row)

    fixed_artifact, fixed_binding = read(
        _special_path("fixed-query.json"),
        "fixed_query",
    )
    if tuple(fixed_authorities) != (2002, 2003, 2004):
        raise MatrixError("fixed-query executed authorities are incomplete")
    fixed_query.validate_artifact(
        fixed_authorities,
        fixed_artifact,
        expected_commit=expected_commit,
    )
    if not fixed_artifact["fixed_query_coverage_complete"] or fixed_artifact["phase4_complete"]:
        raise MatrixError("fixed-query authority has invalid coverage flags")

    stress_probe, stress_probe_binding = read(
        _special_path("stress-work-bound-probe.json"),
        "stress_probe",
    )
    stress_artifact, stress_binding = read(
        _special_path("stress.json"),
        "stress",
    )
    if stress_authority is None:
        raise MatrixError("stress success authority is incomplete")
    stress.validate_artifact(
        *stress_authority,
        stress_probe,
        stress_artifact,
        expected_commit=expected_commit,
    )
    if not stress_artifact["stress_ladder_coverage_complete"] or stress_artifact["phase4_complete"]:
        raise MatrixError("stress authority has invalid coverage flags")

    by_key = {(row["case_id"], row["requested_pool_size"]): row for row in cells}
    for case_id, pool in sorted(metadata):
        meta = metadata[(case_id, pool)]
        evidence = meta["evidence_requirement"]
        if evidence in {"raw_success", "same_run_raw_success"}:
            row = by_key[(case_id, pool)]
            role = row["role"]
            if role == "fixed_query":
                row["authority_bindings"].append(_binding_reference(fixed_binding))
            elif role == "stress":
                row["authority_bindings"].append(_binding_reference(stress_binding))
            row["cell_checksum"] = _cell_checksum(row)
            continue
        if evidence == "descriptor_only_excluded":
            cells.append(
                _special_cell(
                    case_id=case_id,
                    pool=pool,
                    role=meta["role"],
                    evidence_requirement=evidence,
                    in_noncalibration_closure=meta["in_noncalibration_closure"],
                    binding=fixed_binding,
                    reason="descriptor_only_fixed_query_control",
                )
            )
        elif evidence == "compiled_work_bound":
            special = _special_cell(
                case_id=case_id,
                pool=pool,
                role=meta["role"],
                evidence_requirement=evidence,
                in_noncalibration_closure=meta["in_noncalibration_closure"],
                binding=stress_binding,
                reason="compiled_work_bound_stress_control",
            )
            special["authority_bindings"].append(_binding_reference(stress_probe_binding))
            special["cell_checksum"] = _cell_checksum(special)
            cells.append(special)
        else:
            raise MatrixError("frozen protocol contains an unknown evidence disposition")

    cells.sort(key=lambda row: (row["case_id"], row["requested_pool_size"]))
    evidence_files.sort(key=lambda binding: binding["path"])
    _verify_inventory(root, expected_files)
    if common_environment is None or common_provenance is None:
        raise MatrixError("matrix lacks a common execution environment")
    return aggregate_validated_cells(
        expected_commit=expected_commit,
        cells=cells,
        evidence_files=evidence_files,
        host_environment=common_environment,
        reproducibility_provenance=common_provenance,
        fixed_binding=fixed_binding,
        stress_binding=stress_binding,
    )


def validate_decision_publication(
    value: Mapping[str, Any],
    root: pathlib.Path,
    *,
    expected_commit: str,
) -> None:
    expected = aggregate_evidence_root(root, expected_commit=expected_commit)
    _assert_exact(expected, value, "matrix decision publication")
    if value["artifact_checksum"] != _decision_checksum(value):
        raise MatrixError("matrix decision artifact checksum is invalid")
    if value["source_envelope_checksum"] != _source_checksum(value):
        raise MatrixError("matrix decision source envelope checksum is invalid")


def read_decision(path: pathlib.Path) -> Mapping[str, Any]:
    requested = path if path.is_absolute() else pathlib.Path.cwd() / path
    try:
        root = requested.parent.resolve(strict=True)
    except OSError as error:
        raise MatrixError(f"cannot read matrix decision publication: {error}") from error
    value, _ = _read_authority(
        root,
        pathlib.PurePosixPath(requested.name),
        "decision",
    )
    return value


def _write_no_replace(path: pathlib.Path, encoded: bytes) -> None:
    requested = path if path.is_absolute() else pathlib.Path.cwd() / path
    try:
        parent = requested.parent.resolve(strict=True)
    except OSError as error:
        raise MatrixError("decision output parent is not a directory") from error
    destination = parent / requested.name
    temporary = parent / f".{destination.name}.phase4-tmp-{os.getpid()}-{secrets.token_hex(8)}"
    descriptor: int | None = None
    directory_descriptor: int | None = None
    installed = False
    temporary_identity: tuple[int, int] | None = None
    try:
        directory_descriptor = os.open(parent, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
        descriptor = os.open(
            temporary,
            os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
            0o600,
        )
        offset = 0
        while offset < len(encoded):
            written = os.write(descriptor, encoded[offset:])
            if written <= 0:
                raise MatrixError("decision output write made no progress")
            offset += written
        os.fsync(descriptor)
        os.close(descriptor)
        descriptor = None
        status = temporary.stat(follow_symlinks=False)
        temporary_identity = (status.st_dev, status.st_ino)
        os.link(temporary, destination)
        installed = True
        os.fsync(directory_descriptor)
        temporary.unlink()
        os.fsync(directory_descriptor)
    except FileExistsError as error:
        raise MatrixError("decision output already exists") from error
    except OSError as error:
        rollback_error: OSError | None = None
        if installed and temporary_identity is not None:
            try:
                status = destination.stat(follow_symlinks=False)
                if (status.st_dev, status.st_ino) == temporary_identity:
                    destination.unlink()
                    installed = False
                    if directory_descriptor is not None:
                        os.fsync(directory_descriptor)
            except FileNotFoundError:
                installed = False
            except OSError as cleanup_error:
                rollback_error = cleanup_error
        if rollback_error is not None:
            raise MatrixError(
                f"decision output installation and rollback both failed: {rollback_error}"
            ) from error
        raise MatrixError(f"cannot atomically install decision output: {error}") from error
    finally:
        if descriptor is not None:
            os.close(descriptor)
        if directory_descriptor is not None:
            os.close(directory_descriptor)
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        except OSError as cleanup_error:
            if installed:
                raise MatrixError(
                    f"cannot remove private decision temporary: {cleanup_error}"
                ) from cleanup_error


def _validate_output_location(root: pathlib.Path, output: pathlib.Path) -> None:
    try:
        resolved_root = root.resolve(strict=True)
        requested = output if output.is_absolute() else pathlib.Path.cwd() / output
        resolved_output = requested.parent.resolve(strict=True) / requested.name
    except OSError as error:
        raise MatrixError("cannot resolve decision output location") from error
    try:
        resolved_output.relative_to(resolved_root)
    except ValueError:
        return
    canonical = resolved_root / "matrix" / "decision-publication.json"
    if resolved_output != canonical:
        raise MatrixError(
            "decision output inside the evidence root must be matrix/decision-publication.json"
        )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence-root", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--validate", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        if options.validate is not None and options.output is not None:
            raise MatrixError("--validate and --output are mutually exclusive")
        if options.validate is not None:
            value = read_decision(options.validate)
            validate_decision_publication(
                value,
                options.evidence_root,
                expected_commit=options.expected_commit,
            )
            print("validated complete Phase 4 matrix decision publication")
            return 0
        if options.output is not None:
            _validate_output_location(options.evidence_root, options.output)
        value = aggregate_evidence_root(
            options.evidence_root,
            expected_commit=options.expected_commit,
        )
        encoded = (_canonical(value) + "\n").encode("utf-8")
        if len(encoded) > _MAXIMUM_DECISION_BYTES:
            raise MatrixError("matrix decision publication exceeds 16 MiB")
        if options.output is None:
            sys.stdout.write(encoded.decode("utf-8"))
        else:
            _write_no_replace(options.output, encoded)
    except (
        MatrixError,
        fixed_query.ControlError,
        oracle_v1.EvidenceError,
        operational.EvidenceError,
        protocol_v1.ProtocolError,
        protocol_v4.ProtocolV4Error,
        stress.StressError,
    ) as error:
        print(f"Phase 4 matrix aggregation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
