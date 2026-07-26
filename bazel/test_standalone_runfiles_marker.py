"""Create a marker after Bazel materializes one target-specific runfiles tree."""

from __future__ import annotations

import pathlib
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 4:
        return 2
    executable = pathlib.Path(sys.argv[1])
    marker = pathlib.Path(sys.argv[2])
    mode = sys.argv[3]
    runfiles_tree = pathlib.Path(f"{executable}.runfiles")
    if not runfiles_tree.is_dir():
        return 1
    if mode == "probe":
        completed = subprocess.run(
            [str(executable), "--help"],
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0 or "usage:" not in completed.stdout:
            return 1
    elif mode != "materialize":
        return 2
    marker.write_bytes(b"target-specific runfiles ready\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
