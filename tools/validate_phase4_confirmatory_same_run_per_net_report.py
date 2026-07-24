"""Validate the frozen Corpus V2 same-run Raw/telemetry/per-net report join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError("confirmatory same-run Raw input must be a JSON object")
    return value


def _require_development_scope(raw: Mapping[str, Any]) -> None:
    config = raw["config"]
    if (
        raw["raw_evidence_schema_version"] != 2
        or raw["wire_schema_version"] != 2
        or config["case_id"] != 10100
        or config["requested_pool_size"] != 4
    ):
        raise raw_validator.EvidenceError(
            "confirmatory same-run per-net report authority is restricted to (10100,4)"
        )


def validate_join(
    raw: Any,
    sidecar: Any,
    report: Any,
    *,
    expected_commit: str,
) -> None:
    """Validate one complete confirmatory same-run diagnostic publication join."""
    raw_validator.validate_confirmatory_same_run_document_v2(
        raw,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_development_scope(raw_document)
    same_run_validator.validate_confirmatory_join(
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
        raw = raw_validator.read_document(options.raw)
        raw_validator.validate_confirmatory_same_run_document_v2(
            raw,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_development_scope(raw_document)
        sidecar = same_run_validator.read_document(options.same_run_telemetry)
        same_run_validator.validate_confirmatory_join(
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
            "Phase 4 confirmatory same-run Raw/telemetry/per-net report join validation "
            f"failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("validated one Phase 4 confirmatory same-run per-net report publication join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
