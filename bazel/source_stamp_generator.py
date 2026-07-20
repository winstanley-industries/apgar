"""Emits the Phase 3 C++ source stamp from Bazel stable workspace status."""

from __future__ import annotations

import pathlib
import re
import sys

_COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
_COMMIT_KEY = "STABLE_APGAR_GIT_COMMIT"
_DIRTY_KEY = "STABLE_APGAR_GIT_DIRTY"


def _read_status(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        key, separator, value = line.partition(" ")
        if not separator:
            raise ValueError(f"{path}:{line_number}: malformed workspace-status line")
        if key in values:
            raise ValueError(f"{path}:{line_number}: duplicate workspace-status key {key}")
        values[key] = value
    return values


def _source_identity(values: dict[str, str]) -> tuple[str, bool, bool]:
    commit = values.get(_COMMIT_KEY)
    dirty = values.get(_DIRTY_KEY)
    if commit is None and dirty is None:
        return "", False, True
    if commit is None or dirty is None:
        raise ValueError("APGAR workspace status must provide commit and dirty state together")
    if _COMMIT_PATTERN.fullmatch(commit) is None:
        raise ValueError("STABLE_APGAR_GIT_COMMIT must be 40 lowercase hexadecimal characters")
    if dirty not in ("0", "1"):
        raise ValueError("STABLE_APGAR_GIT_DIRTY must be 0 or 1")
    return commit, True, dirty == "1"


def _render(commit: str, stamped: bool, dirty: bool) -> str:
    return f"""#ifndef APGAR_BENCHMARK_PHASE3_SOURCE_STAMP_H_
#define APGAR_BENCHMARK_PHASE3_SOURCE_STAMP_H_

#include <string_view>

namespace apgar::benchmark {{

inline constexpr std::string_view kPhase3BuiltCommit = \"{commit}\";
inline constexpr bool kPhase3SourceStamped = {str(stamped).lower()};
inline constexpr bool kPhase3BuiltFromDirtyTree = {str(dirty).lower()};

}}  // namespace apgar::benchmark

#endif  // APGAR_BENCHMARK_PHASE3_SOURCE_STAMP_H_
"""


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("usage: source_stamp_generator.py STABLE_STATUS OUTPUT", file=sys.stderr)
        return 2
    try:
        values = _read_status(pathlib.Path(argv[1]))
        commit, stamped, dirty = _source_identity(values)
        pathlib.Path(argv[2]).write_text(_render(commit, stamped, dirty), encoding="utf-8")
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
