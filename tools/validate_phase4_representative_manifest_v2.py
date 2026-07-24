"""Strict validators for the frozen Phase 4 confirmatory corpus authorities."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_REPRESENTATIVE = _ROOT / "schemas/benchmark/phase4_representative_manifest_v2.json"
_ROSTER = _ROOT / "schemas/benchmark/phase4_workload_net_roster_manifest_v2.json"
_MAX_BYTES = 1 << 20
_MAX_DEPTH = 64
_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_CORPUS_VERSION = 2
_CORPUS_CHECKSUM = 4182833841936446798
_REPRESENTATIVE_MANIFEST_CHECKSUM = 9613362670139358355
_ROSTER_MANIFEST_CHECKSUM = 14986327048461036142
_BUDGET_ROSTER_CHECKSUM = 15913985307894145139
_IMPORTED_NETS = (
    (3033425279953999715, 0),
    (3033426379465627926, 0),
)
_CASE_ROWS = (
    (10100, (4,), 6),
    (10101, (4,), 6),
    (10102, (4,), 6),
    (10200, (4, 8, 16), 64),
    (10201, (4, 8, 16), 64),
    (10210, (4, 8, 16), 64),
    (10211, (4, 8, 16), 64),
    (10220, (4, 8, 16), 64),
    (10221, (4, 8, 16), 64),
    *((case_id, (4, 8, 16), 256) for case_id in range(11000, 11008)),
    *((case_id, (4, 8, 16), 256) for case_id in range(11100, 11108)),
    *((case_id, (4, 8, 16), 384) for case_id in range(11200, 11208)),
    (12002, (4,), 256),
    (12003, (8,), 128),
    (12004, (16,), 64),
    (13000, (4,), 1024),
    (13001, (4,), 2048),
    (13002, (4,), 4096),
    (14000, (4, 8, 16), 2),
)
_WORK_BOUND_IDS = (13001, 13002)
_EXCLUSIONS = (
    (12000, 10463951918282411440, "descriptor_only_unsupported_pool"),
    (12001, 18273953003518375579, "descriptor_only_unsupported_pool"),
    (13001, 8122399637670938771, "compiled_work_bound"),
    (13002, 15811805131329987573, "compiled_work_bound"),
)
_REPRESENTATIVE_ROOT_FIELDS = (
    "schema_version",
    "corpus_version",
    "corpus_checksum",
    "manifest_checksum",
    "cases",
    "canonical_algorithm_budgets",
)
_CASE_FIELDS = (
    "case_id",
    "requested_pool_sizes",
    "workload_net_count",
    "descriptor_fingerprint",
    "build_status",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "capacity_model_checksum",
    "required_compiled_nodes",
    "required_compiled_host_bytes",
    "required_active_regions",
    "required_board_entities",
)
_BUDGET_FIELDS = ("case_id", "pool_checksums")
_POOL_FIELDS = ("pool", "checksum")
_ROSTER_ROOT_FIELDS = (
    "schema_version",
    "corpus_version",
    "corpus_checksum",
    "representative_manifest_checksum",
    "manifest_checksum",
    "successful_cases",
    "excluded_cases",
)
_ROSTER_ROW_FIELDS = (
    "schema_version",
    "corpus_version",
    "corpus_checksum",
    "case_id",
    "descriptor_fingerprint",
    "case_checksum",
    "board_content_hash",
    "workload_checksum",
    "workload_net_count",
    "roster_checksum",
)
_EXCLUSION_FIELDS = ("case_id", "descriptor_fingerprint", "disposition")


class AuthorityError(ValueError):
    """Stable malformed-authority diagnostic."""


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise AuthorityError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise AuthorityError(f"non-finite JSON number: {value}")


def _check_depth(root: Any) -> None:
    pending = [(root, 1)]
    while pending:
        value, depth = pending.pop()
        if depth > _MAX_DEPTH:
            raise AuthorityError("JSON nesting exceeds 64 levels")
        if isinstance(value, dict):
            pending.extend((child, depth + 1) for child in value.values())
        elif isinstance(value, list):
            pending.extend((child, depth + 1) for child in value)


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _read(path: pathlib.Path, label: str) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            data = stream.read(_MAX_BYTES + 1)
    except OSError as error:
        raise AuthorityError(f"cannot read {label}: {error}") from error
    if len(data) > _MAX_BYTES:
        raise AuthorityError(f"{label} exceeds 1 MiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise AuthorityError(f"{label} must be UTF-8 without BOM and end in one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise AuthorityError(f"invalid {label} JSON: {error}") from error
    if not isinstance(value, dict):
        raise AuthorityError(f"{label} must be a JSON object")
    _check_depth(value)
    if text != _canonical(value) + "\n":
        raise AuthorityError(f"{label} is not canonical compact JSON")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value) != tuple(expected):
        raise AuthorityError(f"{label} fields are missing, extra, or outside canonical order")


def _array(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise AuthorityError(f"{label} must be an array")
    return value


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise AuthorityError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise AuthorityError(f"{label} must be a string")
    return value


def _payload_checksum(domain: str, value: Mapping[str, Any], omitted: str) -> int:
    payload = {key: item for key, item in value.items() if key != omitted}
    hashed = StableHashBuilder()
    hashed.string(domain)
    hashed.string(_canonical(payload))
    return hashed.finish()


def validate_representative(
    path: pathlib.Path = _REPRESENTATIVE,
) -> Mapping[str, Any]:
    document = _read(path, "representative manifest v2")
    _fields(document, _REPRESENTATIVE_ROOT_FIELDS, "representative manifest v2")
    if (
        _u32(document["schema_version"], "representative.schema_version") != 2
        or _u32(document["corpus_version"], "representative.corpus_version") != _CORPUS_VERSION
        or _u64(document["corpus_checksum"], "representative.corpus_checksum") != _CORPUS_CHECKSUM
    ):
        raise AuthorityError("representative manifest has the wrong schema or corpus authority")
    manifest_checksum = _u64(document["manifest_checksum"], "representative.manifest_checksum")
    if manifest_checksum != _REPRESENTATIVE_MANIFEST_CHECKSUM:
        raise AuthorityError("representative manifest checksum differs from the frozen authority")
    if manifest_checksum != _payload_checksum(
        "APGAR-PHASE4-REPRESENTATIVE-MANIFEST-V2", document, "manifest_checksum"
    ):
        raise AuthorityError("representative manifest checksum does not authenticate its payload")

    cases = _array(document["cases"], "representative.cases")
    if len(cases) != len(_CASE_ROWS):
        raise AuthorityError("representative manifest must contain exactly 40 paired rows")
    indexed: dict[int, Mapping[str, Any]] = {}
    for index, (raw_case, expected) in enumerate(zip(cases, _CASE_ROWS, strict=True)):
        label = f"representative.cases[{index}]"
        if not isinstance(raw_case, dict):
            raise AuthorityError(f"{label} must be an object")
        case = raw_case
        _fields(case, _CASE_FIELDS, label)
        case_id, pools, net_count = expected
        if (
            _u32(case["case_id"], f"{label}.case_id") != case_id
            or tuple(_array(case["requested_pool_sizes"], f"{label}.requested_pool_sizes")) != pools
            or _u32(case["workload_net_count"], f"{label}.workload_net_count") != net_count
        ):
            raise AuthorityError(f"{label} differs from the frozen case/pool/net roster")
        for pool in case["requested_pool_sizes"]:
            _u32(pool, f"{label}.requested_pool_sizes[]")
        fingerprint = _u64(case["descriptor_fingerprint"], f"{label}.descriptor_fingerprint")
        if fingerprint == 0:
            raise AuthorityError(f"{label} has a zero descriptor fingerprint")
        status = _string(case["build_status"], f"{label}.build_status")
        identities = [
            _u64(case[field], f"{label}.{field}")
            for field in (
                "case_checksum",
                "board_content_hash",
                "workload_checksum",
                "capacity_model_checksum",
            )
        ]
        work = [
            _u64(case[field], f"{label}.{field}")
            for field in (
                "required_compiled_nodes",
                "required_compiled_host_bytes",
                "required_active_regions",
                "required_board_entities",
            )
        ]
        if case_id in _WORK_BOUND_IDS:
            if (
                status != "compiled_work_bound"
                or any(identities)
                or not all(work[:2])
                or any(work[2:])
            ):
                raise AuthorityError(f"{label} is not the frozen compiled-work-bound disposition")
        elif status != "success" or not all(identities) or not all(work):
            raise AuthorityError(f"{label} is not a complete successful build witness")
        indexed[case_id] = case

    budgets = _array(
        document["canonical_algorithm_budgets"],
        "representative.canonical_algorithm_budgets",
    )
    if len(budgets) != len(_CASE_ROWS):
        raise AuthorityError("representative manifest must contain 40 budget rows")
    budget_map: dict[tuple[int, int], int] = {}
    for index, (raw_budget, expected) in enumerate(zip(budgets, _CASE_ROWS, strict=True)):
        label = f"representative.canonical_algorithm_budgets[{index}]"
        if not isinstance(raw_budget, dict):
            raise AuthorityError(f"{label} must be an object")
        budget = raw_budget
        _fields(budget, _BUDGET_FIELDS, label)
        case_id, pools, _ = expected
        if _u32(budget["case_id"], f"{label}.case_id") != case_id:
            raise AuthorityError(f"{label} has the wrong case")
        raw_pools = _array(budget["pool_checksums"], f"{label}.pool_checksums")
        if len(raw_pools) != len(pools):
            raise AuthorityError(f"{label} has the wrong pool count")
        for pool_index, (raw_pool, expected_pool) in enumerate(zip(raw_pools, pools, strict=True)):
            pool_label = f"{label}.pool_checksums[{pool_index}]"
            if not isinstance(raw_pool, dict):
                raise AuthorityError(f"{pool_label} must be an object")
            _fields(raw_pool, _POOL_FIELDS, pool_label)
            pool = _u32(raw_pool["pool"], f"{pool_label}.pool")
            checksum = _u64(raw_pool["checksum"], f"{pool_label}.checksum")
            if pool != expected_pool or checksum == 0:
                raise AuthorityError(f"{pool_label} differs from the frozen budget roster")
            budget_map[(case_id, pool)] = checksum
    if len(budget_map) != 102:
        raise AuthorityError("representative manifest does not contain 102 unique budget cells")
    return document


def _roster_for(case_id: int, net_count: int) -> tuple[tuple[int, int], ...]:
    if case_id == 14000:
        if net_count != len(_IMPORTED_NETS):
            raise AuthorityError("imported workload roster has the wrong net count")
        return _IMPORTED_NETS
    return tuple((1000 + index, 0) for index in range(net_count))


def _roster_checksum(row: Mapping[str, Any]) -> int:
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-WORKLOAD-NET-ROSTER-V2")
    hashed.u32(row["schema_version"])
    hashed.u32(row["corpus_version"])
    hashed.u64(row["corpus_checksum"])
    hashed.u32(row["case_id"])
    hashed.u64(row["descriptor_fingerprint"])
    hashed.u64(row["case_checksum"])
    hashed.u64(row["board_content_hash"])
    hashed.u64(row["workload_checksum"])
    hashed.u64(row["workload_net_count"])
    for net_id, generation in _roster_for(row["case_id"], row["workload_net_count"]):
        hashed.u64(net_id)
        hashed.u32(generation)
    return hashed.finish()


def validate_roster(
    path: pathlib.Path = _ROSTER,
    representative_path: pathlib.Path = _REPRESENTATIVE,
) -> Mapping[str, Any]:
    representative = validate_representative(representative_path)
    document = _read(path, "workload-net roster manifest v2")
    _fields(document, _ROSTER_ROOT_FIELDS, "workload-net roster manifest v2")
    if (
        _u32(document["schema_version"], "roster.schema_version") != 2
        or _u32(document["corpus_version"], "roster.corpus_version") != _CORPUS_VERSION
        or _u64(document["corpus_checksum"], "roster.corpus_checksum") != _CORPUS_CHECKSUM
        or _u64(
            document["representative_manifest_checksum"],
            "roster.representative_manifest_checksum",
        )
        != _REPRESENTATIVE_MANIFEST_CHECKSUM
    ):
        raise AuthorityError("workload-net roster has the wrong corpus or representative binding")
    manifest_checksum = _u64(document["manifest_checksum"], "roster.manifest_checksum")
    if manifest_checksum != _ROSTER_MANIFEST_CHECKSUM:
        raise AuthorityError("workload-net roster checksum differs from the frozen authority")
    if manifest_checksum != _payload_checksum(
        "APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V2",
        document,
        "manifest_checksum",
    ):
        raise AuthorityError("workload-net roster checksum does not authenticate its payload")

    expected_success = [
        case for case in representative["cases"] if case["build_status"] == "success"
    ]
    rows = _array(document["successful_cases"], "roster.successful_cases")
    if len(rows) != 38:
        raise AuthorityError("workload-net roster must contain 38 successful rows")
    for index, (raw_row, case) in enumerate(zip(rows, expected_success, strict=True)):
        label = f"roster.successful_cases[{index}]"
        if not isinstance(raw_row, dict):
            raise AuthorityError(f"{label} must be an object")
        row = raw_row
        _fields(row, _ROSTER_ROW_FIELDS, label)
        expected = {
            "schema_version": 2,
            "corpus_version": 2,
            "corpus_checksum": _CORPUS_CHECKSUM,
            "case_id": case["case_id"],
            "descriptor_fingerprint": case["descriptor_fingerprint"],
            "case_checksum": case["case_checksum"],
            "board_content_hash": case["board_content_hash"],
            "workload_checksum": case["workload_checksum"],
            "workload_net_count": case["workload_net_count"],
            "roster_checksum": row["roster_checksum"],
        }
        for field in _ROSTER_ROW_FIELDS:
            _u64(row[field], f"{label}.{field}")
        if row != expected or row["roster_checksum"] != _roster_checksum(row):
            raise AuthorityError(f"{label} differs from its representative row or full roster")

    exclusions = _array(document["excluded_cases"], "roster.excluded_cases")
    if len(exclusions) != len(_EXCLUSIONS):
        raise AuthorityError("workload-net roster must contain four exclusions")
    for index, (raw_exclusion, expected) in enumerate(zip(exclusions, _EXCLUSIONS, strict=True)):
        label = f"roster.excluded_cases[{index}]"
        if not isinstance(raw_exclusion, dict):
            raise AuthorityError(f"{label} must be an object")
        _fields(raw_exclusion, _EXCLUSION_FIELDS, label)
        actual = (
            _u32(raw_exclusion["case_id"], f"{label}.case_id"),
            _u64(raw_exclusion["descriptor_fingerprint"], f"{label}.descriptor_fingerprint"),
            _string(raw_exclusion["disposition"], f"{label}.disposition"),
        )
        if actual != expected:
            raise AuthorityError(f"{label} differs from the frozen exclusion")
    return document


def budget_roster_checksum(
    representative: Mapping[str, Any],
) -> int:
    entries = [
        (budget["case_id"], pool["pool"], pool["checksum"])
        for budget in representative["canonical_algorithm_budgets"]
        for pool in budget["pool_checksums"]
    ]
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V2")
    hashed.u32(2)
    hashed.u32(_CORPUS_VERSION)
    hashed.u64(_CORPUS_CHECKSUM)
    hashed.u64(_REPRESENTATIVE_MANIFEST_CHECKSUM)
    hashed.u64(len(entries))
    for case_id, pool, checksum in entries:
        hashed.u32(case_id)
        hashed.u32(pool)
        hashed.u64(checksum)
    return hashed.finish()


def validate_authorities(
    representative_path: pathlib.Path = _REPRESENTATIVE,
    roster_path: pathlib.Path = _ROSTER,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    representative = validate_representative(representative_path)
    roster = validate_roster(roster_path, representative_path)
    if budget_roster_checksum(representative) != _BUDGET_ROSTER_CHECKSUM:
        raise AuthorityError("canonical algorithm-budget roster checksum drifted")
    return representative, roster


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--representative", type=pathlib.Path, default=_REPRESENTATIVE)
    parser.add_argument("--roster", type=pathlib.Path, default=_ROSTER)
    args = parser.parse_args(argv)
    try:
        validate_authorities(args.representative, args.roster)
    except AuthorityError as error:
        print(f"phase4 V2 authority validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 V2 representative and workload-roster authorities validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
