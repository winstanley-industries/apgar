"""Validate the Session-v5 H=4096 same-run Raw/telemetry/report join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Callable, Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_decision_protocol_v3 as protocol_v3
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_raw_evidence as session_v5_raw,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_same_run_decision_telemetry as session_v5_telemetry,
)
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_AUTHORITY_ROWS = (
    {
        "purpose": "same_run_raw_outcome_and_timing",
        "supersedes": "phase4_confirmatory_same_run_raw_evidence_v2",
        "authority": "phase4_confirmatory_same_run_raw_evidence_v3",
    },
    {
        "purpose": "same_run_exact_rejection_guardrail",
        "supersedes": "phase4_confirmatory_same_run_decision_telemetry_v2",
        "authority": "phase4_confirmatory_same_run_decision_telemetry_v3",
    },
    {
        "purpose": "same_run_per_net_diagnostic_join",
        "supersedes": "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
        "authority": "phase4_confirmatory_same_run_per_net_report_publication_join_v3",
    },
)
_U64_MAX = (1 << 64) - 1


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory Session-v5 H=4096 same-run Raw input must be a JSON object"
        )
    return value


def _require_publication_authorities() -> None:
    try:
        protocol = protocol_v3.read_protocol()
    except ValueError as error:
        raise raw_validator.EvidenceError(
            f"cannot authenticate frozen Session-v5 H=4096 same-run report authority: {error}"
        ) from error
    substitutions = protocol["artifact_authority_namespace"]["substitutions"]
    for expected in _AUTHORITY_ROWS:
        rows = [row for row in substitutions if row["purpose"] == expected["purpose"]]
        if rows != [expected]:
            raise raw_validator.EvidenceError(
                "frozen Session-v5 H=4096 same-run publication authority drifted"
            )


def _require_development_scope(raw: Mapping[str, Any]) -> None:
    config = raw["config"]
    if (
        raw["raw_evidence_schema_version"] != 2
        or raw["wire_schema_version"] != 2
        or config["case_id"] != 10100
        or config["requested_pool_size"] != 4
    ):
        raise raw_validator.EvidenceError(
            "confirmatory Session-v5 H=4096 same-run report authority is restricted to "
            "exact (10100,4)"
        )


def _require_companion_source_envelope(
    raw: Mapping[str, Any],
    companion: Any,
    *,
    expected_commit: str,
    label: str,
    checksum: Callable[[Mapping[str, Any]], int],
) -> Mapping[str, Any]:
    if not isinstance(companion, dict):
        raise raw_validator.EvidenceError(
            f"confirmatory Session-v5 H=4096 {label} input must be a JSON object"
        )
    for field in (
        "source_commit",
        "source_stamped",
        "source_tree_dirty",
        "source_envelope_checksum",
        "artifact_checksum",
    ):
        if field not in companion:
            raise raw_validator.EvidenceError(
                f"Session-v5 H=4096 {label} source envelope is missing {field}"
            )
    source_commit = companion["source_commit"]
    source_stamped = companion["source_stamped"]
    source_dirty = companion["source_tree_dirty"]
    if (
        not isinstance(source_commit, str)
        or not isinstance(source_stamped, bool)
        or not isinstance(source_dirty, bool)
        or source_commit != expected_commit
        or not source_stamped
        or source_dirty
        or source_commit != raw["source_commit"]
        or source_stamped != raw["source_stamped"]
        or source_dirty != raw["source_tree_dirty"]
    ):
        raise raw_validator.EvidenceError(
            f"Raw and {label} must name the same clean independently expected commit"
        )
    for field in ("artifact_checksum", "source_envelope_checksum"):
        value = companion[field]
        if isinstance(value, bool) or not isinstance(value, int) or not 0 < value <= _U64_MAX:
            raise raw_validator.EvidenceError(
                f"{label}.{field} must be a nonzero unsigned 64-bit integer"
            )
    if companion["source_envelope_checksum"] != checksum(companion):
        raise raw_validator.EvidenceError(
            f"{label} source envelope does not authenticate source provenance"
        )
    return companion


def _require_sidecar_source_envelope(
    raw: Mapping[str, Any],
    sidecar: Any,
    *,
    expected_commit: str,
) -> Mapping[str, Any]:
    return _require_companion_source_envelope(
        raw,
        sidecar,
        expected_commit=expected_commit,
        label="same-run telemetry",
        checksum=telemetry_validator.compute_source_envelope_checksum,
    )


def _require_report_source_envelope(
    raw: Mapping[str, Any],
    report: Any,
    *,
    expected_commit: str,
) -> Mapping[str, Any]:
    return _require_companion_source_envelope(
        raw,
        report,
        expected_commit=expected_commit,
        label="report",
        checksum=report_validator.compute_report_source_envelope_checksum,
    )


def validate_join(
    raw: Any,
    sidecar: Any,
    report: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate one Session-v5 exact-cell three-way diagnostic publication join."""
    _require_publication_authorities()
    session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2(
        raw,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_development_scope(raw_document)
    _require_sidecar_source_envelope(
        raw_document,
        sidecar,
        expected_commit=expected_commit,
    )
    session_v5_telemetry.validate_confirmatory_h4096_session_v5_join(
        raw_document,
        sidecar,
        expected_commit=expected_commit,
    )
    _require_report_source_envelope(
        raw_document,
        report,
        expected_commit=expected_commit,
    )
    report_validator.validate_confirmatory_report_against_validated_raw(
        raw_document,
        report,
        expected_commit=expected_commit,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        _require_publication_authorities()
        raw = raw_validator.read_document(options.raw)
        session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_development_scope(raw_document)
        sidecar = telemetry_validator.read_document(options.same_run_telemetry)
        _require_sidecar_source_envelope(
            raw_document,
            sidecar,
            expected_commit=options.expected_commit,
        )
        session_v5_telemetry.validate_confirmatory_h4096_session_v5_join(
            raw_document,
            sidecar,
            expected_commit=options.expected_commit,
        )
        report = report_validator.read_report_document(options.report)
        _require_report_source_envelope(
            raw_document,
            report,
            expected_commit=options.expected_commit,
        )
        report_validator.validate_confirmatory_report_against_validated_raw(
            raw_document,
            report,
            expected_commit=options.expected_commit,
        )
    except raw_validator.EvidenceError as error:
        print(
            "Phase 4 confirmatory Session-v5 H=4096 same-run "
            f"Raw/telemetry/per-net report join validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print(
        "validated one Phase 4 confirmatory Session-v5 H=4096 "
        "same-run Raw/telemetry/per-net report join"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
