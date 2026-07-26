"""Validate the H=4096 same-run Raw/telemetry/per-net report publication join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import (
    validate_phase4_confirmatory_h4096_same_run_decision_telemetry as h4096_telemetry,
)
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_JOIN_AUTHORITY = {
    "purpose": "same_run_per_net_diagnostic_join",
    "supersedes": "phase4_confirmatory_same_run_per_net_report_publication_join_v1",
    "authority": "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
}


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory H=4096 same-run Raw input must be a JSON object"
        )
    return value


def _require_publication_authority() -> None:
    try:
        protocol = protocol_v2.read_protocol()
    except protocol_v2.ConfirmatoryProtocolV2Error as error:
        raise raw_validator.EvidenceError(
            f"cannot authenticate frozen H=4096 same-run report authority: {error}"
        ) from error
    rows = [
        row
        for row in protocol["artifact_authority_namespace"]["substitutions"]
        if row["purpose"] == _JOIN_AUTHORITY["purpose"]
    ]
    if rows != [_JOIN_AUTHORITY]:
        raise raw_validator.EvidenceError(
            "frozen H=4096 same-run per-net publication authority drifted"
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
            "confirmatory H=4096 same-run per-net report authority is restricted to (10100,4)"
        )


def validate_join(
    raw: Any,
    sidecar: Any,
    report: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate one complete H=4096 exact same-run diagnostic publication join."""
    _require_publication_authority()
    h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
        raw,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_development_scope(raw_document)
    h4096_telemetry.validate_confirmatory_h4096_join(
        raw_document,
        sidecar,
        expected_commit=expected_commit,
    )
    report_validator.validate_confirmatory_report_against_validated_raw(
        raw_document,
        report,
        expected_commit=expected_commit,
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        _require_publication_authority()
        raw = raw_validator.read_document(options.raw)
        h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_development_scope(raw_document)
        sidecar = telemetry_validator.read_document(options.same_run_telemetry)
        h4096_telemetry.validate_confirmatory_h4096_join(
            raw_document,
            sidecar,
            expected_commit=options.expected_commit,
        )
        report = report_validator.read_report_document(options.report)
        report_validator.validate_confirmatory_report_against_validated_raw(
            raw_document,
            report,
            expected_commit=options.expected_commit,
        )
    except raw_validator.EvidenceError as error:
        print(
            "Phase 4 confirmatory H=4096 same-run Raw/telemetry/per-net report join "
            f"validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("validated one Phase 4 confirmatory H=4096 same-run Raw/telemetry/per-net report join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
