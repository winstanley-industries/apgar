"""Test-only entry point for fixed-source H=4096 Oracle launchers."""

from __future__ import annotations

import argparse
import os
import pathlib
import sys

from tools.phase4_exact_small_oracle_launcher_handshake import require_launcher


def main() -> int:
    launcher = pathlib.Path(f"/proc/{os.getppid()}/exe").resolve(strict=True)
    require_launcher(f"{launcher.name}_py")
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--raw", required=True)
    parser.add_argument("--same-run-telemetry", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--snapshot", required=True)
    parser.parse_args()
    print(
        "test-only fixed-source launcher is preflight-only and cannot access inputs or emit an "
        "Oracle artifact",
        file=sys.stderr,
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
