"""Hermetic, multi-language lint orchestration for APGAR."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

Runner = Callable[..., subprocess.CompletedProcess[bytes]]


@dataclass(frozen=True)
class Invocation:
    """One pinned Bazel tool invocation for a language linter."""

    target: str
    check_args: tuple[str, ...]
    fix_args: tuple[str, ...]


@dataclass(frozen=True)
class Linter:
    """File matching and tool invocations for one source language."""

    name: str
    display_name: str
    suffixes: frozenset[str]
    filenames: frozenset[str]
    paths: frozenset[str]
    invocations: tuple[Invocation, ...]

    def matches(self, path: PurePosixPath) -> bool:
        return (
            str(path) in self.paths
            or path.name in self.filenames
            or path.suffix.lower() in self.suffixes
        )


LINTERS = {
    "cpp": Linter(
        name="cpp",
        display_name="C/C++",
        suffixes=frozenset(
            {".c", ".cc", ".cpp", ".cxx", ".cu", ".cuh", ".h", ".hh", ".hpp", ".hxx"}
        ),
        filenames=frozenset(),
        paths=frozenset(),
        invocations=(
            Invocation(
                target="@llvm_toolchain//:clang-format",
                check_args=("--dry-run", "--Werror"),
                fix_args=("-i",),
            ),
        ),
    ),
    "python": Linter(
        name="python",
        display_name="Python",
        suffixes=frozenset({".py", ".pyi"}),
        filenames=frozenset(),
        paths=frozenset(),
        invocations=(
            Invocation(
                target="@ruff",
                check_args=("check",),
                fix_args=("check", "--fix"),
            ),
            Invocation(
                target="@ruff",
                check_args=("format", "--check"),
                fix_args=("format",),
            ),
        ),
    ),
    "shell": Linter(
        name="shell",
        display_name="Bash/Shell",
        suffixes=frozenset({".bash", ".sh"}),
        filenames=frozenset(),
        paths=frozenset({"tools/bazel"}),
        invocations=(
            Invocation(
                target="@rules_shellcheck//:shellcheck",
                check_args=("--format=gcc",),
                fix_args=("--format=gcc",),
            ),
        ),
    ),
    "starlark": Linter(
        name="starlark",
        display_name="Bazel/Starlark",
        suffixes=frozenset({".bzl"}),
        filenames=frozenset(
            {"BUILD", "BUILD.bazel", "MODULE.bazel", "WORKSPACE", "WORKSPACE.bazel"}
        ),
        paths=frozenset(),
        invocations=(
            Invocation(
                target="@buildifier_prebuilt//:buildifier",
                check_args=("-mode=check", "-lint=warn"),
                fix_args=("-mode=fix", "-lint=fix"),
            ),
        ),
    ),
}

DEFAULT_LINTERS = tuple(LINTERS)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        prog="bazel lint",
        description="Run APGAR's Bazel-pinned repository linters.",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="rewrite files in place instead of checking them",
    )
    parser.add_argument(
        "--only",
        action="append",
        choices=tuple(LINTERS),
        metavar="LANGUAGE",
        help="run one language linter; may be repeated",
    )
    return parser.parse_args(argv)


def select_linters(only: Sequence[str] | None) -> tuple[Linter, ...]:
    names = only or DEFAULT_LINTERS
    return tuple(LINTERS[name] for name in dict.fromkeys(names))


def discover_files(repo_root: Path, runner: Runner = subprocess.run) -> tuple[PurePosixPath, ...]:
    result = runner(
        [
            "git",
            "-C",
            str(repo_root),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
            "-z",
        ],
        check=True,
        stdout=subprocess.PIPE,
    )
    files = (PurePosixPath(os.fsdecode(raw)) for raw in result.stdout.split(b"\0") if raw)
    existing_files = (path for path in files if (repo_root / str(path)).is_file())
    return tuple(sorted(existing_files, key=str))


def commands_for(
    linter: Linter,
    *,
    fix: bool,
    bazel_real: str,
    repo_root: Path,
    files: Sequence[PurePosixPath],
) -> tuple[tuple[str, ...], ...]:
    absolute_files = tuple(str(repo_root / str(path)) for path in files)
    commands = []
    for invocation in linter.invocations:
        tool_args = invocation.fix_args if fix else invocation.check_args
        commands.append((bazel_real, "run", invocation.target, "--", *tool_args, *absolute_files))
    return tuple(commands)


def run_linters(
    linters: Sequence[Linter],
    *,
    fix: bool,
    bazel_real: str,
    repo_root: Path,
    runner: Runner = subprocess.run,
) -> int:
    files = discover_files(repo_root, runner)
    mode = "fix" if fix else "check"

    for linter in linters:
        matched = tuple(path for path in files if linter.matches(path))
        print(f"==> Linting {linter.display_name} ({mode})", flush=True)
        if not matched:
            print(f"    no {linter.display_name} files", flush=True)
            continue

        for command in commands_for(
            linter,
            fix=fix,
            bazel_real=bazel_real,
            repo_root=repo_root,
            files=matched,
        ):
            result = runner(command, check=False, cwd=repo_root)
            if result.returncode != 0:
                return result.returncode

    return 0


def main(
    argv: Sequence[str] | None = None,
    *,
    environ: Mapping[str, str] = os.environ,
    runner: Runner = subprocess.run,
) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)

    bazel_real = environ.get("APGAR_BAZEL_REAL")
    repo_root = environ.get("APGAR_REPO_ROOT")
    if not bazel_real or not repo_root:
        print("bazel lint must be launched through APGAR's Bazelisk wrapper", file=sys.stderr)
        return 2

    try:
        return run_linters(
            select_linters(args.only),
            fix=args.fix,
            bazel_real=bazel_real,
            repo_root=Path(repo_root),
            runner=runner,
        )
    except subprocess.CalledProcessError as error:
        return error.returncode
