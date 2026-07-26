"""Validate Phase 4 confirmatory Raw evidence under fixed H=4096 authority."""

from __future__ import annotations

import argparse
import functools
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_raw_evidence as raw_validator

_PROTOCOL_CHECKSUM = 11520586171987743043
_BUDGET_ROSTER_CHECKSUM = 18429170436700418962
_CONFIGURATION_AUTHORITY = "phase4_confirmatory_corpus_v2_h4096"
_ORDINARY_RAW_AUTHORITY = "phase4_confirmatory_raw_evidence_v2"
_SAME_RUN_RAW_AUTHORITY = "phase4_confirmatory_same_run_raw_evidence_v2"
_INITIAL_DEVELOPMENT_CELLS = (
    {
        "case_id": 10100,
        "requested_pool_size": 4,
        "role": "exact",
        "carrier": "same_run",
        "raw_wire_schema_version": 2,
        "raw_authority": _SAME_RUN_RAW_AUTHORITY,
    },
    {
        "case_id": 10200,
        "requested_pool_size": 8,
        "role": "calibration",
        "carrier": "ordinary",
        "raw_wire_schema_version": 1,
        "raw_authority": _ORDINARY_RAW_AUTHORITY,
    },
)
_INITIAL_CELL_BUDGETS = {
    (10100, 4): 8829615204625848656,
    (10200, 8): 8230401457668518004,
}

EvidenceError = raw_validator.EvidenceError


@functools.cache
def _validated_authorities() -> tuple[
    Mapping[str, Any],
    Mapping[str, Any],
    Mapping[str, Any],
]:
    # Import lazily because both authority validators reuse the Raw validator's
    # stable hash implementation.
    from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
    from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
    from tools import validate_phase4_representative_manifest_v2 as representative_v2

    try:
        protocol = protocol_v2.read_protocol()
        roster = budget_v3.validate_roster()
        representative, _ = representative_v2.validate_authorities()
    except ValueError as error:
        raise EvidenceError(
            f"cannot authenticate frozen H=4096 confirmatory authorities: {error}"
        ) from error

    if (
        protocol["artifact_checksum"] != _PROTOCOL_CHECKSUM
        or roster["schema_version"] != 3
        or roster["roster_checksum"] != _BUDGET_ROSTER_CHECKSUM
        or roster["configuration_authority"] != _CONFIGURATION_AUTHORITY
        or protocol["campaign"]["configuration_authority"] != _CONFIGURATION_AUTHORITY
    ):
        raise EvidenceError("frozen H=4096 protocol or budget-roster identity drifted")

    protocol_budget = protocol["canonical_algorithm_budget_authority"]
    for field in (
        "authority",
        "schema_version",
        "corpus_version",
        "corpus_checksum",
        "representative_manifest_schema_version",
        "representative_manifest_checksum",
        "workload_roster_manifest_schema_version",
        "workload_roster_manifest_checksum",
        "configuration_authority",
        "cell_count",
        "roster_checksum",
    ):
        if protocol_budget[field] != roster[field]:
            raise EvidenceError(
                f"H=4096 protocol budget authority field {field} differs from roster v3"
            )
    if (
        protocol_budget["superseded_roster_schema_version"]
        != roster["supersedes"]["schema_version"]
        or protocol_budget["superseded_roster_checksum"] != roster["supersedes"]["roster_checksum"]
        or protocol_budget["configuration"] != roster["configuration"]
    ):
        raise EvidenceError("H=4096 protocol budget ancestry differs from roster v3")
    if (
        roster["corpus_version"] != representative["corpus_version"]
        or roster["corpus_checksum"] != representative["corpus_checksum"]
        or roster["representative_manifest_schema_version"] != representative["schema_version"]
        or roster["representative_manifest_checksum"] != representative["manifest_checksum"]
    ):
        raise EvidenceError("H=4096 budget roster differs from Representative Manifest v2")

    initial = protocol["development_execution_plan"]["initial_development_cells"]
    if initial != list(_INITIAL_DEVELOPMENT_CELLS):
        raise EvidenceError("frozen H=4096 initial development scope drifted")
    budgets = budget_v3.budget_map(roster)
    if any(budgets.get(cell) != checksum for cell, checksum in _INITIAL_CELL_BUDGETS.items()):
        raise EvidenceError("frozen H=4096 initial-cell budget checksum drifted")
    return protocol, representative, roster


def _authority_provider(
    corpus_version: int,
) -> tuple[
    int,
    Mapping[int, Mapping[str, Any]],
    Mapping[tuple[int, int], int],
]:
    if corpus_version != 2:
        raise EvidenceError("H=4096 confirmatory Raw authority requires Corpus v2")
    _, representative, roster = _validated_authorities()
    from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3

    cases = {
        raw_validator._u32(case["case_id"], "confirmatory H=4096 representative case_id"): case
        for case in raw_validator._array(
            representative["cases"], "confirmatory H=4096 representative cases"
        )
    }
    return (
        raw_validator._u64(
            representative["corpus_checksum"],
            "confirmatory H=4096 representative corpus_checksum",
        ),
        cases,
        budget_v3.budget_map(roster),
    )


def confirmatory_h4096_initial_cells(*, same_run: bool) -> frozenset[tuple[int, int]]:
    protocol, _, _ = _validated_authorities()
    carrier = "same_run" if same_run else "ordinary"
    wire_schema = 2 if same_run else 1
    authority = _SAME_RUN_RAW_AUTHORITY if same_run else _ORDINARY_RAW_AUTHORITY
    cells = frozenset(
        (cell["case_id"], cell["requested_pool_size"])
        for cell in protocol["development_execution_plan"]["initial_development_cells"]
        if cell["carrier"] == carrier
        and cell["raw_wire_schema_version"] == wire_schema
        and cell["raw_authority"] == authority
    )
    expected = frozenset({(10100, 4)} if same_run else {(10200, 8)})
    if cells != expected:
        raise EvidenceError("frozen H=4096 protocol-assigned Raw scope drifted")
    return cells


def validated_confirmatory_h4096_workload_roster(
    case_id: int,
) -> tuple[tuple[int, int], ...]:
    _validated_authorities()
    from tools import validate_phase4_representative_manifest_v2 as representative_v2

    try:
        _, roster = representative_v2.validated_successful_case_roster(case_id)
        return roster
    except representative_v2.AuthorityError as error:
        raise EvidenceError(
            f"cannot authenticate the H=4096 confirmatory workload roster: {error}"
        ) from error


def _validate_protocol_scope(value: Any, *, same_run: bool) -> None:
    document = raw_validator._object(value, "confirmatory H=4096 raw cell")
    config = raw_validator._object(document["config"], "confirmatory H=4096 raw cell.config")
    cell = (config["case_id"], config["requested_pool_size"])
    if cell not in confirmatory_h4096_initial_cells(same_run=same_run):
        authority = "same-run" if same_run else "ordinary"
        raise EvidenceError(
            f"confirmatory H=4096 cell is outside the Protocol-v2 initial {authority} Raw authority"
        )


def validate_confirmatory_h4096_document(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> None:
    """Validate ordinary Raw-v1/Wire-v1 under fixed H=4096 authority."""
    raw_validator._validate_document_with_authority(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_corpus_version=2,
        authority_provider=_authority_provider,
    )
    _validate_protocol_scope(value, same_run=False)


def validate_confirmatory_h4096_same_run_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> None:
    """Validate same-run Raw-v2/Wire-v2 under fixed H=4096 authority."""
    raw_validator._validate_document_with_authority(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=2,
        _expected_corpus_version=2,
        authority_provider=_authority_provider,
    )
    _validate_protocol_scope(value, same_run=True)


def validate_confirmatory_h4096_same_run_total_attempt_document_v2(
    value: Any,
    *,
    allow_unstamped: bool = False,
    expected_commit: str | None = None,
    expected_repetitions: int = 20,
    expected_workers: int = 4,
) -> bool:
    """Authenticate complete or incomplete H=4096 same-run Raw-v2 evidence."""
    raw_validator._validate_document_with_authority(
        value,
        allow_unstamped=allow_unstamped,
        expected_commit=expected_commit,
        expected_repetitions=expected_repetitions,
        expected_workers=expected_workers,
        _expected_raw_evidence_schema_version=2,
        _total_attempt_mode=True,
        _expected_corpus_version=2,
        authority_provider=_authority_provider,
    )
    _validate_protocol_scope(value, same_run=True)
    document = raw_validator._object(value, "confirmatory H=4096 total-attempt cell")
    attempts = raw_validator._array(
        document["attempts"], "confirmatory H=4096 total-attempt cell.attempts"
    )
    if not any(
        raw_validator._object(attempt, "confirmatory H=4096 total-attempt pair")[arm]["disposition"]
        == 0
        for attempt in attempts
        for arm in ("baseline", "candidate")
    ):
        raise EvidenceError(
            "H=4096 total-attempt Raw lacks a successfully authenticated H=4096 arm record"
        )
    return all(
        raw_validator._object(attempt, "confirmatory H=4096 total-attempt pair")["result"]
        is not None
        for attempt in attempts
    )


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--testing-repetitions", type=int, default=20)
    parser.add_argument("--testing-workers", type=int, default=4)
    parser.add_argument("--expected-commit")
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    options = parser.parse_args(argv)
    if not options.testing_allow_unstamped and (
        options.testing_repetitions != 20 or options.testing_workers != 4
    ):
        parser.error(
            "--testing-repetitions and --testing-workers require --testing-allow-unstamped"
        )
    try:
        for path in options.paths:
            document = raw_validator.read_document(path)
            validate_confirmatory_h4096_document(
                document,
                allow_unstamped=options.testing_allow_unstamped,
                expected_commit=options.expected_commit,
                expected_repetitions=options.testing_repetitions,
                expected_workers=options.testing_workers,
            )
    except EvidenceError as error:
        print(
            f"Phase 4 confirmatory H=4096 raw evidence validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print(f"validated {len(options.paths)} Phase 4 confirmatory H=4096 raw evidence artifact(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
