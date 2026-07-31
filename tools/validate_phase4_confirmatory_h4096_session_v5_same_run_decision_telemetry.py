"""Validate a Session-v5 H=4096 confirmatory Raw/decision-telemetry join."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_h4096_session_v5_raw_evidence as session_v5_raw
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_SAME_RUN_TELEMETRY_AUTHORITY = "phase4_confirmatory_same_run_decision_telemetry_v3"


def _decision_cells() -> frozenset[tuple[int, int]]:
    # The Raw authority validation authenticates the complete Protocol-v3
    # artifact namespace, including the paired telemetry authority above.
    protocol, _, _ = session_v5_raw._validated_authorities()
    substitutions = protocol["artifact_authority_namespace"]["substitutions"]
    matches = [
        row
        for row in substitutions
        if row["purpose"] == "same_run_exact_rejection_guardrail"
        and row["authority"] == _SAME_RUN_TELEMETRY_AUTHORITY
    ]
    if len(matches) != 1:
        raise raw_validator.EvidenceError("Session-v5 H=4096 same-run telemetry authority drifted")
    return session_v5_raw.confirmatory_h4096_session_v5_initial_cells(same_run=True)


def validate_confirmatory_h4096_session_v5_join(
    raw: Any,
    sidecar: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> Mapping[str, Any]:
    """Validate fixed Protocol-v3 Session-v5 H=4096 Raw and telemetry authority."""
    return telemetry_validator._validate_join_with_authority(
        raw,
        sidecar,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        raw_document_validator=(
            session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2
        ),
        decision_cells_provider=_decision_cells,
        workload_roster_provider=(
            session_v5_raw.validated_confirmatory_h4096_session_v5_workload_roster
        ),
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
        session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2(
            raw,
            allow_unstamped=options.testing_allow_unstamped,
            expected_commit=options.expected_commit,
            expected_repetitions=options.testing_repetitions,
            expected_workers=options.testing_workers,
        )
        sidecar = telemetry_validator.read_document(options.sidecar)
        validate_confirmatory_h4096_session_v5_join(
            raw,
            sidecar,
            allow_unstamped=options.testing_allow_unstamped,
            expected_commit=options.expected_commit,
            expected_repetitions=options.testing_repetitions,
            expected_workers=options.testing_workers,
        )
    except raw_validator.EvidenceError as error:
        print(
            f"Phase 4 confirmatory Session-v5 H=4096 same-run telemetry validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    status = "passed" if telemetry_validator.exact_rejection_guardrail_passes(sidecar) else "failed"
    print(
        "validated Phase 4 confirmatory Session-v5 H=4096 same-run decision "
        f"telemetry (exact_validation_guardrail={status})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
