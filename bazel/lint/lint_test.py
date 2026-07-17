"""Tests for APGAR's lint orchestration."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath

from bazel.lint import lint


class ToolchainTest(unittest.TestCase):
    def test_python_minor_version_is_pinned(self) -> None:
        self.assertEqual(sys.version_info[:2], (3, 13))


class LinterMatchingTest(unittest.TestCase):
    def test_cpp_matches_cuda_and_headers(self) -> None:
        self.assertTrue(lint.LINTERS["cpp"].matches(PurePosixPath("gpu/search.cu")))
        self.assertTrue(lint.LINTERS["cpp"].matches(PurePosixPath("include/apgar/route.hpp")))
        self.assertFalse(lint.LINTERS["cpp"].matches(PurePosixPath("README.md")))

    def test_starlark_matches_well_known_names_and_bzl(self) -> None:
        starlark = lint.LINTERS["starlark"]
        self.assertTrue(starlark.matches(PurePosixPath("MODULE.bazel")))
        self.assertTrue(starlark.matches(PurePosixPath("some/package/BUILD")))
        self.assertTrue(starlark.matches(PurePosixPath("bazel/rules/example.bzl")))

    def test_python_matches_sources_and_stubs(self) -> None:
        python = lint.LINTERS["python"]
        self.assertTrue(python.matches(PurePosixPath("tool.py")))
        self.assertTrue(python.matches(PurePosixPath("types.pyi")))

    def test_shell_matches_extensions_and_bazelisk_bootstrap(self) -> None:
        shell = lint.LINTERS["shell"]
        self.assertTrue(shell.matches(PurePosixPath("scripts/check.sh")))
        self.assertTrue(shell.matches(PurePosixPath("tools/bazel")))
        self.assertFalse(shell.matches(PurePosixPath("tools/other")))


class SelectionTest(unittest.TestCase):
    def test_default_selection_runs_every_linter(self) -> None:
        self.assertEqual(
            tuple(item.name for item in lint.select_linters(None)),
            lint.DEFAULT_LINTERS,
        )

    def test_explicit_selection_preserves_order_and_removes_duplicates(self) -> None:
        selected = lint.select_linters(["starlark", "cpp", "starlark"])
        self.assertEqual(tuple(item.name for item in selected), ("starlark", "cpp"))


class DiscoveryTest(unittest.TestCase):
    def test_discovers_existing_tracked_and_untracked_files_in_stable_order(self) -> None:
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            return subprocess.CompletedProcess(command, 0, stdout=b"z.py\0deleted.sh\0a.cc\0")

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            (repo_root / "z.py").touch()
            (repo_root / "a.cc").touch()
            files = lint.discover_files(repo_root, runner)

        self.assertEqual(files, (PurePosixPath("a.cc"), PurePosixPath("z.py")))
        self.assertEqual(
            calls[0][0],
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
        )


class CommandConstructionTest(unittest.TestCase):
    def test_python_fix_builds_ruff_check_and_format_commands(self) -> None:
        commands = lint.commands_for(
            lint.LINTERS["python"],
            fix=True,
            bazel_real="/tools/bazel-real",
            repo_root=Path("/repo"),
            files=[PurePosixPath("bazel/lint/lint.py")],
        )

        self.assertEqual(
            commands,
            (
                (
                    "/tools/bazel-real",
                    "run",
                    "@ruff",
                    "--",
                    "check",
                    "--fix",
                    "/repo/bazel/lint/lint.py",
                ),
                (
                    "/tools/bazel-real",
                    "run",
                    "@ruff",
                    "--",
                    "format",
                    "/repo/bazel/lint/lint.py",
                ),
            ),
        )

    def test_shell_fix_still_runs_shellcheck_as_a_gate(self) -> None:
        commands = lint.commands_for(
            lint.LINTERS["shell"],
            fix=True,
            bazel_real="/tools/bazel-real",
            repo_root=Path("/repo"),
            files=[PurePosixPath("tools/bazel")],
        )

        self.assertEqual(
            commands,
            (
                (
                    "/tools/bazel-real",
                    "run",
                    "@rules_shellcheck//:shellcheck",
                    "--",
                    "--format=gcc",
                    "/repo/tools/bazel",
                ),
            ),
        )


class ExecutionTest(unittest.TestCase):
    def test_runs_only_commands_for_matching_files(self) -> None:
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            if command[0] == "git":
                return subprocess.CompletedProcess(
                    command,
                    0,
                    stdout=b"bazel/lint/lint.py\0README.md\0",
                )
            return subprocess.CompletedProcess(command, 0)

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            (repo_root / "bazel/lint").mkdir(parents=True)
            (repo_root / "bazel/lint/lint.py").touch()
            (repo_root / "README.md").touch()
            result = lint.run_linters(
                [lint.LINTERS["python"]],
                fix=False,
                bazel_real="/tools/bazel-real",
                repo_root=repo_root,
                runner=runner,
            )

        self.assertEqual(result, 0)
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[1][0][2:5], ("@ruff", "--", "check"))
        self.assertEqual(calls[2][0][2:6], ("@ruff", "--", "format", "--check"))
        self.assertNotIn(str(repo_root / "README.md"), calls[1][0])

    def test_stops_after_first_failed_tool(self) -> None:
        tool_calls = []

        def runner(command, **kwargs):
            if command[0] == "git":
                return subprocess.CompletedProcess(command, 0, stdout=b"tool.py\0")
            tool_calls.append(command)
            return subprocess.CompletedProcess(command, 7)

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            (repo_root / "tool.py").touch()
            result = lint.run_linters(
                [lint.LINTERS["python"]],
                fix=False,
                bazel_real="/tools/bazel-real",
                repo_root=repo_root,
                runner=runner,
            )

        self.assertEqual(result, 7)
        self.assertEqual(len(tool_calls), 1)


if __name__ == "__main__":
    unittest.main()
