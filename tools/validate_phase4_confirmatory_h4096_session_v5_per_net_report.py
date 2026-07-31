"""Validate the Session-v5 H=4096 ordinary Raw/per-net report publication join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_decision_protocol_v3 as protocol_v3
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_raw_evidence as session_v5_raw,
)
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_JOIN_AUTHORITY = {
    "purpose": "ordinary_per_net_diagnostic_join",
    "supersedes": "phase4_confirmatory_per_net_report_publication_join_v2",
    "authority": "phase4_confirmatory_per_net_report_publication_join_v3",
}
_U64_MAX = (1 << 64) - 1


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory Session-v5 H=4096 ordinary Raw input must be a JSON object"
        )
    return value


def _require_publication_authority() -> None:
    try:
        protocol = protocol_v3.read_protocol()
    except ValueError as error:
        raise raw_validator.EvidenceError(
            f"cannot authenticate frozen Session-v5 H=4096 report authority: {error}"
        ) from error
    rows = [
        row
        for row in protocol["artifact_authority_namespace"]["substitutions"]
        if row["purpose"] == _JOIN_AUTHORITY["purpose"]
    ]
    if rows != [_JOIN_AUTHORITY]:
        raise raw_validator.EvidenceError(
            "frozen Session-v5 H=4096 ordinary per-net publication authority drifted"
        )


def _require_development_scope(raw: Mapping[str, Any]) -> None:
    config = raw["config"]
    if (
        raw["wire_schema_version"] != 1
        or config["case_id"] != 10200
        or config["requested_pool_size"] != 8
    ):
        raise raw_validator.EvidenceError(
            "confirmatory Session-v5 H=4096 per-net report authority is restricted to "
            "ordinary (10200,8)"
        )


def _require_report_source_envelope(
    raw: Mapping[str, Any],
    report: Any,
    *,
    expected_commit: str,
) -> Mapping[str, Any]:
    if not isinstance(report, dict):
        raise raw_validator.EvidenceError(
            "confirmatory Session-v5 H=4096 ordinary report input must be a JSON object"
        )
    for field in (
        "source_commit",
        "source_stamped",
        "source_tree_dirty",
        "source_envelope_checksum",
        "artifact_checksum",
    ):
        if field not in report:
            raise raw_validator.EvidenceError(
                f"Session-v5 H=4096 report source envelope is missing {field}"
            )
    source_commit = report["source_commit"]
    source_stamped = report["source_stamped"]
    source_dirty = report["source_tree_dirty"]
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
            "raw and report must name the same clean independently expected commit"
        )
    for field in ("artifact_checksum", "source_envelope_checksum"):
        value = report[field]
        if isinstance(value, bool) or not isinstance(value, int) or not 0 < value <= _U64_MAX:
            raise raw_validator.EvidenceError(
                f"report.{field} must be a nonzero unsigned 64-bit integer"
            )
    if report[
        "source_envelope_checksum"
    ] != report_validator.compute_report_source_envelope_checksum(report):
        raise raw_validator.EvidenceError(
            "report source envelope does not authenticate source provenance"
        )
    return report


def validate_join(
    raw: Any,
    report: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate one complete Session-v5 H=4096 ordinary diagnostic publication join."""
    _require_publication_authority()
    session_v5_raw.validate_confirmatory_h4096_session_v5_document(
        raw,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_development_scope(raw_document)
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
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        _require_publication_authority()
        raw = raw_validator.read_document(options.raw)
        session_v5_raw.validate_confirmatory_h4096_session_v5_document(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_development_scope(raw_document)
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
            "Phase 4 confirmatory Session-v5 H=4096 Raw/per-net report join "
            f"validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("validated one Phase 4 confirmatory Session-v5 H=4096 ordinary Raw/per-net report join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
