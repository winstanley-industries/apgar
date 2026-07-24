"""Validate the frozen Corpus V2 ordinary Raw/per-net report join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Sequence

from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        raw = raw_validator.read_document(options.raw)
        raw_validator.validate_confirmatory_document(raw, expected_commit=options.expected_commit)
        config = raw["config"]
        if (
            raw["wire_schema_version"] != 1
            or config["case_id"] != 10200
            or config["requested_pool_size"] != 4
        ):
            raise raw_validator.EvidenceError(
                "confirmatory per-net report authority is restricted to ordinary (10200,4)"
            )
        report = report_validator.read_report_document(options.report)
        report_validator.validate_confirmatory_join(
            raw,
            report,
            expected_commit=options.expected_commit,
        )
    except raw_validator.EvidenceError as error:
        print(
            f"Phase 4 confirmatory Raw/per-net report join validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("validated one Phase 4 confirmatory ordinary Raw/per-net report join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
