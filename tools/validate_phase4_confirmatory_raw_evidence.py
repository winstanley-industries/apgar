"""Validate one Phase 4 confirmatory raw cell against frozen Corpus V2 authority."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Sequence

from tools import validate_phase4_raw_evidence as raw_validator


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--testing-repetitions", type=int, default=20)
    parser.add_argument("--testing-workers", type=int, default=4)
    parser.add_argument("--expected-commit")
    parser.add_argument("--same-run-total-attempt-v2", action="store_true")
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    options = parser.parse_args(argv)
    if not options.testing_allow_unstamped and (
        options.testing_repetitions != 20 or options.testing_workers != 4
    ):
        parser.error(
            "--testing-repetitions and --testing-workers require --testing-allow-unstamped"
        )
    try:
        total_attempt_complete = True
        for path in options.paths:
            document = raw_validator.read_document(path)
            if options.same_run_total_attempt_v2:
                total_attempt_complete = (
                    raw_validator.validate_confirmatory_same_run_total_attempt_document_v2(
                        document,
                        allow_unstamped=options.testing_allow_unstamped,
                        expected_commit=options.expected_commit,
                        expected_repetitions=options.testing_repetitions,
                        expected_workers=options.testing_workers,
                    )
                    and total_attempt_complete
                )
            else:
                raw_validator.validate_confirmatory_document(
                    document,
                    allow_unstamped=options.testing_allow_unstamped,
                    expected_commit=options.expected_commit,
                    expected_repetitions=options.testing_repetitions,
                    expected_workers=options.testing_workers,
                )
    except raw_validator.EvidenceError as error:
        print(f"Phase 4 confirmatory raw evidence validation failed: {error}", file=sys.stderr)
        return 1
    if options.same_run_total_attempt_v2:
        status = "complete" if total_attempt_complete else "incomplete"
        print(
            "authenticated "
            f"{len(options.paths)} Phase 4 confirmatory same-run total-attempt artifact(s) "
            f"(status={status}, publication_eligible=false)"
        )
        return 3
    print(f"validated {len(options.paths)} Phase 4 confirmatory raw evidence artifact(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
