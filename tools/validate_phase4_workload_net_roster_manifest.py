"""Strict validator for the frozen Phase 4 workload-net roster manifest."""

from __future__ import annotations

import argparse
import json
import pathlib
import sys
from collections.abc import Iterator, Mapping, Sequence
from typing import Any

_U32_MAX = (1 << 32) - 1
_U64_MAX = (1 << 64) - 1
_CORPUS_VERSION = 1
_CORPUS_CHECKSUM = 7311872938254494931
_MANIFEST_CHECKSUM = 3143811343998575433
_MAX_REPRESENTATIVE_NETS = 4096
_MAX_JSON_BYTES = 1 << 20
_ROOT_FIELDS = (
    "schema_version",
    "corpus_version",
    "corpus_checksum",
    "manifest_checksum",
    "successful_cases",
    "excluded_cases",
)
_ROW_FIELDS = (
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
_REPRESENTATIVE_ROOT_FIELDS = (
    "schema_version",
    "corpus_checksum",
    "cases",
    "canonical_algorithm_budgets",
)
_REPRESENTATIVE_CASE_FIELDS = (
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
_EXCLUSIONS = (
    (2000, 8203613321943675931, "descriptor_only_unsupported_pool", 0),
    (2001, 11000562598404360444, "descriptor_only_unsupported_pool", 0),
    (3001, 5505549972392664092, "compiled_work_bound", 1),
    (3002, 6801270323093200014, "compiled_work_bound", 1),
)
_IMPORTED_NETS = (
    (3033425279953999715, 0),
    (3033426379465627926, 0),
)
_ROOT = pathlib.Path(__file__).resolve().parent.parent
_DEFAULT_MANIFEST = _ROOT / "schemas/benchmark/phase4_workload_net_roster_manifest_v1.json"
_DEFAULT_REPRESENTATIVE = _ROOT / "schemas/benchmark/phase4_representative_manifest_v1.json"


class ManifestError(ValueError):
    """Stable validation failure suitable for a Bazel test diagnostic."""


class StableHashBuilder:
    """Board IR v1 byte-stable FNV-1a, matching stable_hash.h."""

    def __init__(self) -> None:
        self._value = 14695981039346656037

    def byte(self, value: int) -> None:
        self._value ^= value & 0xFF
        self._value = (self._value * 1099511628211) & _U64_MAX

    def u32(self, value: int) -> None:
        for _ in range(4):
            self.byte(value)
            value >>= 8

    def u64(self, value: int) -> None:
        for _ in range(8):
            self.byte(value)
            value >>= 8

    def string(self, value: str) -> None:
        encoded = value.encode("utf-8")
        self.u64(len(encoded))
        for byte in encoded:
            self.byte(byte)

    def finish(self) -> int:
        return self._value


def _reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _read_json(path: pathlib.Path, label: str) -> Any:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(_MAX_JSON_BYTES + 1)
        if len(encoded) > _MAX_JSON_BYTES:
            raise ManifestError(f"{label} exceeds the {_MAX_JSON_BYTES}-byte input bound")
        return json.loads(encoded.decode("utf-8"), object_pairs_hook=_reject_duplicate_pairs)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read {label}: {error}") from error


def _object(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ManifestError(f"{label} must be an object")
    return value


def _array(value: Any, label: str) -> Sequence[Any]:
    if not isinstance(value, list):
        raise ManifestError(f"{label} must be an array")
    return value


def _fields(value: Mapping[str, Any], expected: Sequence[str], label: str) -> None:
    if tuple(value) != tuple(expected):
        raise ManifestError(f"{label} fields are missing, extra, or outside canonical order")


def _uint(value: Any, maximum: int, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise ManifestError(f"{label} must be an unsigned integer no greater than {maximum}")
    return value


def _u32(value: Any, label: str) -> int:
    return _uint(value, _U32_MAX, label)


def _u64(value: Any, label: str) -> int:
    return _uint(value, _U64_MAX, label)


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str):
        raise ManifestError(f"{label} must be a string")
    return value


def _roster_for(case_id: int, net_count: int) -> Iterator[tuple[int, int]]:
    if case_id == 4000:
        if net_count != len(_IMPORTED_NETS):
            raise ManifestError("imported case has the wrong net count")
        yield from _IMPORTED_NETS
        return
    for index in range(net_count):
        yield 1000 + index, 0


def _roster_checksum(row: Mapping[str, Any]) -> int:
    net_count = _uint(
        row["workload_net_count"], _MAX_REPRESENTATIVE_NETS, "roster.workload_net_count"
    )
    if net_count == 0:
        raise ManifestError("a successful roster must contain at least one net")
    builder = StableHashBuilder()
    builder.string("APGAR-PHASE4-WORKLOAD-NET-ROSTER-V1")
    builder.u32(row["schema_version"])
    builder.u32(row["corpus_version"])
    builder.u64(row["corpus_checksum"])
    builder.u32(row["case_id"])
    builder.u64(row["descriptor_fingerprint"])
    builder.u64(row["case_checksum"])
    builder.u64(row["board_content_hash"])
    builder.u64(row["workload_checksum"])
    builder.u64(net_count)
    for net_id, generation in _roster_for(row["case_id"], net_count):
        builder.u64(net_id)
        builder.u32(generation)
    return builder.finish()


def _manifest_checksum(
    schema_version: int,
    corpus_version: int,
    corpus_checksum: int,
    rows: Sequence[Mapping[str, Any]],
    exclusions: Sequence[Mapping[str, Any]],
) -> int:
    builder = StableHashBuilder()
    builder.string("APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V1")
    builder.u32(schema_version)
    builder.u32(corpus_version)
    builder.u64(corpus_checksum)
    builder.u64(len(rows))
    for row in rows:
        builder.u32(row["schema_version"])
        builder.u32(row["corpus_version"])
        builder.u64(row["corpus_checksum"])
        builder.u32(row["case_id"])
        builder.u64(row["descriptor_fingerprint"])
        builder.u64(row["case_checksum"])
        builder.u64(row["board_content_hash"])
        builder.u64(row["workload_checksum"])
        builder.u32(row["workload_net_count"])
        builder.u64(row["roster_checksum"])
    builder.u64(len(exclusions))
    disposition_codes = {
        "descriptor_only_unsupported_pool": 0,
        "compiled_work_bound": 1,
    }
    for exclusion in exclusions:
        builder.u32(exclusion["case_id"])
        builder.u64(exclusion["descriptor_fingerprint"])
        builder.byte(disposition_codes[exclusion["disposition"]])
    return builder.finish()


def _expected_rows(representative_path: pathlib.Path) -> tuple[int, list[dict[str, int]]]:
    document = _object(_read_json(representative_path, "representative manifest"), "representative")
    _fields(document, _REPRESENTATIVE_ROOT_FIELDS, "representative")
    if _u32(document["schema_version"], "representative.schema_version") != 1:
        raise ManifestError("representative schema version must be 1")
    corpus_checksum = _u64(document["corpus_checksum"], "representative.corpus_checksum")
    if corpus_checksum != _CORPUS_CHECKSUM:
        raise ManifestError("representative corpus checksum differs from the frozen v1 checksum")
    cases = _array(document["cases"], "representative.cases")
    if len(cases) != 40:
        raise ManifestError("representative manifest must contain exactly 40 paired rows")
    rows: list[dict[str, int]] = []
    previous_case_id = 0
    bound_ids: list[int] = []
    for index, raw_case in enumerate(cases):
        label = f"representative.cases[{index}]"
        case = _object(raw_case, label)
        _fields(case, _REPRESENTATIVE_CASE_FIELDS, label)
        case_id = _u32(case["case_id"], f"{label}.case_id")
        if case_id <= previous_case_id:
            raise ManifestError("representative case IDs must be strictly increasing")
        previous_case_id = case_id
        status = _string(case["build_status"], f"{label}.build_status")
        net_count = _uint(
            case["workload_net_count"],
            _MAX_REPRESENTATIVE_NETS,
            f"{label}.workload_net_count",
        )
        if net_count == 0:
            raise ManifestError(f"{label} workload must contain at least one net")
        for field in (
            "descriptor_fingerprint",
            "case_checksum",
            "board_content_hash",
            "workload_checksum",
        ):
            _u64(case[field], f"{label}.{field}")
        if status == "success":
            row = {
                "schema_version": 1,
                "corpus_version": _CORPUS_VERSION,
                "corpus_checksum": corpus_checksum,
                "case_id": case_id,
                "descriptor_fingerprint": case["descriptor_fingerprint"],
                "case_checksum": case["case_checksum"],
                "board_content_hash": case["board_content_hash"],
                "workload_checksum": case["workload_checksum"],
                "workload_net_count": net_count,
                "roster_checksum": 0,
            }
            row["roster_checksum"] = _roster_checksum(row)
            rows.append(row)
        elif status == "compiled_work_bound":
            bound_ids.append(case_id)
        else:
            raise ManifestError(f"{label} has an unknown build status")
    if len(rows) != 38 or bound_ids != [3001, 3002]:
        raise ManifestError("representative successful/bound disposition roster has drifted")
    return corpus_checksum, rows


def validate(manifest_path: pathlib.Path, representative_path: pathlib.Path) -> None:
    corpus_checksum, expected_rows = _expected_rows(representative_path)
    document = _object(_read_json(manifest_path, "workload-net roster manifest"), "manifest")
    _fields(document, _ROOT_FIELDS, "manifest")
    schema_version = _u32(document["schema_version"], "manifest.schema_version")
    corpus_version = _u32(document["corpus_version"], "manifest.corpus_version")
    actual_corpus_checksum = _u64(document["corpus_checksum"], "manifest.corpus_checksum")
    declared_manifest_checksum = _u64(document["manifest_checksum"], "manifest.manifest_checksum")
    if schema_version != 1 or corpus_version != _CORPUS_VERSION:
        raise ManifestError("manifest schema and corpus versions must be 1")
    if actual_corpus_checksum != _CORPUS_CHECKSUM or actual_corpus_checksum != corpus_checksum:
        raise ManifestError("manifest corpus checksum differs from the frozen representative v1")
    if declared_manifest_checksum != _MANIFEST_CHECKSUM:
        raise ManifestError("manifest checksum differs from the frozen semantic v1 checksum")

    raw_rows = _array(document["successful_cases"], "manifest.successful_cases")
    if len(raw_rows) != 38:
        raise ManifestError("manifest must contain exactly 38 successful rows")
    rows: list[Mapping[str, Any]] = []
    for index, (raw_row, expected) in enumerate(zip(raw_rows, expected_rows, strict=True)):
        label = f"manifest.successful_cases[{index}]"
        row = _object(raw_row, label)
        _fields(row, _ROW_FIELDS, label)
        for field in _ROW_FIELDS:
            maximum = (
                _U32_MAX
                if field
                in {
                    "schema_version",
                    "corpus_version",
                    "case_id",
                    "workload_net_count",
                }
                else _U64_MAX
            )
            _uint(row[field], maximum, f"{label}.{field}")
        if dict(row) != expected:
            raise ManifestError(f"{label} differs from the frozen representative row or roster")
        if row["roster_checksum"] != _roster_checksum(row):
            raise ManifestError(f"{label}.roster_checksum does not authenticate the full roster")
        rows.append(row)

    raw_exclusions = _array(document["excluded_cases"], "manifest.excluded_cases")
    if len(raw_exclusions) != len(_EXCLUSIONS):
        raise ManifestError("manifest must contain exactly four exclusions")
    exclusions: list[Mapping[str, Any]] = []
    for index, (raw_exclusion, expected) in enumerate(
        zip(raw_exclusions, _EXCLUSIONS, strict=True)
    ):
        label = f"manifest.excluded_cases[{index}]"
        exclusion = _object(raw_exclusion, label)
        _fields(exclusion, _EXCLUSION_FIELDS, label)
        actual = (
            _u32(exclusion["case_id"], f"{label}.case_id"),
            _u64(exclusion["descriptor_fingerprint"], f"{label}.descriptor_fingerprint"),
            _string(exclusion["disposition"], f"{label}.disposition"),
        )
        if actual != expected[:3]:
            raise ManifestError(f"{label} differs from the frozen exclusion")
        exclusions.append(exclusion)

    computed = _manifest_checksum(schema_version, corpus_version, corpus_checksum, rows, exclusions)
    if declared_manifest_checksum != computed:
        raise ManifestError("manifest checksum does not authenticate the semantic manifest")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=pathlib.Path, default=_DEFAULT_MANIFEST)
    parser.add_argument("--representative", type=pathlib.Path, default=_DEFAULT_REPRESENTATIVE)
    arguments = parser.parse_args()
    try:
        validate(arguments.manifest, arguments.representative)
    except ManifestError as error:
        print(f"phase4 roster manifest validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 workload-net roster manifest validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
