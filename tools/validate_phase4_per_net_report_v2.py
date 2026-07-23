"""Strict Raw-v2/same-run/per-net diagnostic publication join for Phase 4."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = re.compile(r"[0-9a-f]{40}")

EvidenceError = raw_validator.EvidenceError


def validate_join(
    raw: Any,
    sidecar: Any,
    report: Any,
    *,
    expected_commit: str | None = None,
    allow_unstamped: bool = False,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    """Validate one complete Raw-v2 authority and its independent report."""
    if not allow_unstamped and (
        expected_commit is None or _COMMIT.fullmatch(expected_commit) is None
    ):
        raise EvidenceError("publication requires an independently supplied expected commit")
    sidecar_document = same_run_validator.validate_join(
        raw,
        sidecar,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
    )
    raw_document = raw
    report_document = report
    if not isinstance(raw_document, dict) or not isinstance(report_document, dict):
        raise EvidenceError("Raw-v2 and report inputs must be JSON objects")
    if report_document.get("raw_wire_schema_version") != 2:
        raise EvidenceError("per-net report v2 join requires an explicit Wire-v2 association")
    report_validator.validate_report_against_validated_raw(
        raw_document,
        report_document,
        expected_commit=raw_document["source_commit"] if allow_unstamped else expected_commit,
    )
    return sidecar_document, report_document


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
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
            raise EvidenceError("per-net report v2 join requires an explicit Wire-v2 association")
        report_validator.validate_report_against_validated_raw(
            raw,
            report,
            expected_commit=options.expected_commit,
        )
    except EvidenceError as error:
        print(f"Phase 4 Raw-v2/report join validation failed: {error}", file=sys.stderr)
        return 1
    print("validated one Phase 4 Raw-v2/same-run/per-net report publication join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
