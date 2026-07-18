#!/usr/bin/env python3
"""Capture and verify an exact APGAR working-tree review surface."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

MANIFEST_VERSION = 1


def git(root: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True,
        check=False,
        text=True,
    )
    if check and result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "git command failed")
    return result.stdout.rstrip("\n")


def repo_root() -> Path:
    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        check=False,
        text=True,
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "not inside a Git repository")
    return Path(result.stdout.strip()).resolve()


def resolve_base(root: Path, requested: str | None) -> tuple[str, str]:
    candidates = [requested] if requested else ["@{upstream}", "origin/main", "main", "HEAD^"]
    for candidate in candidates:
        if candidate is None:
            continue
        oid = git(root, "rev-parse", "--verify", f"{candidate}^{{commit}}", check=False)
        if oid:
            return candidate, oid
    raise RuntimeError("could not resolve a review base; pass --base REF")


def changed_paths(root: Path, base_ref: str) -> list[str]:
    commands = [
        ("diff", "--name-only", "--diff-filter=ACDMRTUXB", f"{base_ref}...HEAD"),
        ("diff", "--name-only", "--diff-filter=ACDMRTUXB", "HEAD"),
        ("ls-files", "--others", "--exclude-standard"),
    ]
    paths: set[str] = set()
    for command in commands:
        output = git(root, *command)
        paths.update(line for line in output.splitlines() if line)
    return sorted(paths)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_record(root: Path, relative: str) -> dict[str, Any]:
    path = root / relative
    if not path.exists() and not path.is_symlink():
        return {"path": relative, "kind": "missing", "size": None, "sha256": None}
    if path.is_symlink():
        target = os.readlink(path)
        return {
            "path": relative,
            "kind": "symlink",
            "size": len(target.encode()),
            "sha256": hashlib.sha256(target.encode()).hexdigest(),
            "target": target,
        }
    if not path.is_file():
        return {"path": relative, "kind": "other", "size": None, "sha256": None}
    return {"path": relative, "kind": "file", "size": path.stat().st_size, "sha256": sha256(path)}


def state(root: Path, base_ref: str, base_oid: str) -> dict[str, Any]:
    # Diff against the resolved OID, not the moving ref name, so the manifest
    # cannot silently combine an old recorded base with a newly advanced ref.
    paths = changed_paths(root, base_oid)
    return {
        "base_ref": base_ref,
        "base_oid": base_oid,
        "head_oid": git(root, "rev-parse", "HEAD"),
        "branch": git(root, "branch", "--show-current"),
        "status_porcelain_v2": git(
            root, "status", "--porcelain=v2", "--untracked-files=all"
        ).splitlines(),
        "files": [file_record(root, path) for path in paths],
    }


def stable_state(root: Path, base_ref: str, base_oid: str) -> dict[str, Any]:
    for _ in range(3):
        first = state(root, base_ref, base_oid)
        second = state(root, base_ref, base_oid)
        if first == second:
            return first
    raise RuntimeError("working tree changed repeatedly while capturing the review surface")


def manifest_path(root: Path, requested: str | None) -> Path:
    scratch = (root / ".agent-scratch").resolve()
    if requested:
        output = Path(requested)
        if not output.is_absolute():
            output = root / output
        output = output.resolve()
    else:
        stamp = datetime.now(UTC).strftime("%Y%m%dT%H%M%S%fZ")
        output = scratch / f"apgar-review-scope-{stamp}.json"
    if output != scratch and scratch not in output.parents:
        raise RuntimeError("review manifests must be written under .agent-scratch/")
    return output


def capture(args: argparse.Namespace) -> int:
    root = repo_root()
    base_ref, base_oid = resolve_base(root, args.base)
    payload = {
        "manifest_version": MANIFEST_VERSION,
        "repo_root": str(root),
        "captured_at": datetime.now(UTC).isoformat(),
        **stable_state(root, base_ref, base_oid),
    }
    output = manifest_path(root, args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(output.relative_to(root))
    return 0


def compare(expected: dict[str, Any], current: dict[str, Any]) -> dict[str, Any]:
    differences: dict[str, Any] = {}
    for key in ("base_oid", "head_oid", "branch", "status_porcelain_v2", "files"):
        if expected.get(key) != current.get(key):
            differences[key] = {"expected": expected.get(key), "current": current.get(key)}
    return differences


def verify(args: argparse.Namespace) -> int:
    root = repo_root()
    manifest = Path(args.manifest)
    if not manifest.is_absolute():
        manifest = root / manifest
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    if expected.get("manifest_version") != MANIFEST_VERSION:
        raise RuntimeError("unsupported review manifest version")
    if Path(expected.get("repo_root", "")).resolve() != root:
        raise RuntimeError("manifest belongs to a different repository")
    base_ref = expected["base_ref"]
    base_oid = git(root, "rev-parse", "--verify", f"{base_ref}^{{commit}}", check=False)
    if not base_oid:
        base_oid = expected["base_oid"]
    current = stable_state(root, base_ref, base_oid)
    differences = compare(expected, current)
    if differences:
        print(json.dumps({"status": "DRIFT", "differences": differences}, indent=2, sort_keys=True))
        return 1
    print(
        json.dumps(
            {
                "status": "MATCH",
                "head_oid": current["head_oid"],
                "file_count": len(current["files"]),
            },
            sort_keys=True,
        )
    )
    return 0


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)
    capture_parser = commands.add_parser(
        "capture", help="capture a stable working-tree review manifest"
    )
    capture_parser.add_argument(
        "--base", help="base ref; defaults to upstream, origin/main, main, then HEAD^"
    )
    capture_parser.add_argument("--output", help="manifest path under .agent-scratch/")
    capture_parser.set_defaults(function=capture)
    verify_parser = commands.add_parser("verify", help="compare the tree with a captured manifest")
    verify_parser.add_argument("manifest", help="captured manifest path")
    verify_parser.set_defaults(function=verify)
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        return args.function(args)
    except (OSError, RuntimeError, KeyError, json.JSONDecodeError) as error:
        print(f"review_scope.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
