"""Frozen nondecision Session-v5 budget authority for live V1 diagnostic tests."""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path
from unittest import mock

_ROSTER = "schemas/benchmark/phase4_current_v1_diagnostic_budget_roster_v2.json"
_DOMAIN = "APGAR-PHASE4-CURRENT-V1-DIAGNOSTIC-BUDGET-ROSTER-V2"
_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_EXPECTED_KEYS = ((100, 4), (101, 4), (102, 4), (200, 4), (4000, 4))


def _reject_duplicate_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise RuntimeError(f"current diagnostic budget roster repeats field {key!r}")
        result[key] = value
    return result


def _uint(value: object, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise RuntimeError(f"{label} is not a canonical unsigned integer")
    return value


def _read_live_diagnostic_budgets(
    raw_validator: object,
    path: Path,
) -> dict[tuple[int, int], int]:
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_duplicate_pairs,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"cannot read current diagnostic budget roster: {error}") from error
    if not isinstance(document, dict) or tuple(document) != (
        "schema_version",
        "session_schema_version",
        "decision_eligible",
        "entries",
        "artifact_checksum",
    ):
        raise RuntimeError("current diagnostic budget roster header is invalid")
    if (
        _uint(document["schema_version"], _U32_MAX, "schema_version") != 2
        or _uint(document["session_schema_version"], _U32_MAX, "session_schema_version") != 5
        or document["decision_eligible"] is not False
        or not isinstance(document["entries"], list)
        or not document["entries"]
    ):
        raise RuntimeError("current diagnostic budget roster authority is invalid")
    checksum = raw_validator.StableHashBuilder()
    checksum.string(_DOMAIN)
    checksum.u32(document["schema_version"])
    checksum.u32(document["session_schema_version"])
    checksum.u64(len(document["entries"]))
    current: dict[tuple[int, int], int] = {}
    previous_key = (0, 0)
    for index, raw_entry in enumerate(document["entries"]):
        if not isinstance(raw_entry, dict) or tuple(raw_entry) != (
            "case_id",
            "pool_size",
            "checksum",
        ):
            raise RuntimeError(f"current diagnostic budget roster row {index} is malformed")
        case_id = _uint(raw_entry["case_id"], _U32_MAX, f"entries[{index}].case_id")
        pool = _uint(raw_entry["pool_size"], _U32_MAX, f"entries[{index}].pool_size")
        budget_checksum = _uint(
            raw_entry["checksum"],
            _U64_MAX,
            f"entries[{index}].checksum",
        )
        key = (case_id, pool)
        if key <= previous_key or pool not in {4, 8, 16} or budget_checksum == 0:
            raise RuntimeError("current diagnostic budget roster rows are not canonical")
        previous_key = key
        current[key] = budget_checksum
        checksum.u32(case_id)
        checksum.u32(pool)
        checksum.u64(budget_checksum)
    if tuple(current) != _EXPECTED_KEYS:
        raise RuntimeError("current diagnostic budget roster does not match the frozen five cells")
    if checksum.finish() != _uint(
        document["artifact_checksum"],
        _U64_MAX,
        "artifact_checksum",
    ):
        raise RuntimeError("current diagnostic budget roster checksum is invalid")
    return current


def patch_live_diagnostic_budgets(raw_validator: object, runfile: Callable[[str], Path]) -> object:
    """Patch one test process without changing any production validator target."""
    current = _read_live_diagnostic_budgets(raw_validator, runfile(_ROSTER))
    original = raw_validator._representative_manifest

    def diagnostic_manifest() -> tuple[
        int,
        dict[int, dict[str, object]],
        dict[tuple[int, int], int],
    ]:
        corpus_checksum, cases, _ = original()
        return corpus_checksum, cases, current

    patcher = mock.patch.object(
        raw_validator,
        "_representative_manifest",
        diagnostic_manifest,
    )
    patcher.start()
    return patcher
