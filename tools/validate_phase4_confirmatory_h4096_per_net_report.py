"""Validate the H=4096 ordinary Raw/per-net report publication join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_JOIN_AUTHORITY = {
    "purpose": "ordinary_per_net_diagnostic_join",
    "supersedes": "phase4_confirmatory_per_net_report_publication_join_v1",
    "authority": "phase4_confirmatory_per_net_report_publication_join_v2",
}


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory H=4096 ordinary Raw input must be a JSON object"
        )
    return value


def _require_publication_authority() -> None:
    try:
        protocol = protocol_v2.read_protocol()
    except protocol_v2.ConfirmatoryProtocolV2Error as error:
        raise raw_validator.EvidenceError(
            f"cannot authenticate frozen H=4096 report authority: {error}"
        ) from error
    rows = [
        row
        for row in protocol["artifact_authority_namespace"]["substitutions"]
        if row["purpose"] == _JOIN_AUTHORITY["purpose"]
    ]
    if rows != [_JOIN_AUTHORITY]:
        raise raw_validator.EvidenceError(
            "frozen H=4096 ordinary per-net publication authority drifted"
        )


def _require_development_scope(raw: Mapping[str, Any]) -> None:
    config = raw["config"]
    if (
        raw["wire_schema_version"] != 1
        or config["case_id"] != 10200
        or config["requested_pool_size"] != 8
    ):
        raise raw_validator.EvidenceError(
            "confirmatory H=4096 per-net report authority is restricted to ordinary (10200,8)"
        )


def validate_join(
    raw: Any,
    report: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate one complete H=4096 ordinary diagnostic publication join."""
    _require_publication_authority()
    h4096_raw.validate_confirmatory_h4096_document(
        raw,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_development_scope(raw_document)
    report_validator.validate_confirmatory_report_against_validated_raw(
        raw_document,
        report,
        expected_commit=expected_commit,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        _require_publication_authority()
        raw = raw_validator.read_document(options.raw)
        h4096_raw.validate_confirmatory_h4096_document(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_development_scope(raw_document)
        report = report_validator.read_report_document(options.report)
        report_validator.validate_confirmatory_report_against_validated_raw(
            raw_document,
            report,
            expected_commit=options.expected_commit,
        )
    except raw_validator.EvidenceError as error:
        print(
            f"Phase 4 confirmatory H=4096 Raw/per-net report join validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("validated one Phase 4 confirmatory H=4096 ordinary Raw/per-net report join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
