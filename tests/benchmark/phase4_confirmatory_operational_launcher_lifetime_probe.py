"""Test-only inner authority used to verify compiled-launcher lifetime coupling."""

from __future__ import annotations

import argparse
import os
import pathlib
import time

from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ready", required=True, type=pathlib.Path)
    parser.add_argument("--release", required=True, type=pathlib.Path)
    parser.add_argument("--canary", required=True, type=pathlib.Path)
    options = parser.parse_args()
    options.ready.write_text(f"{os.getpid()}\n", encoding="ascii")
    deadline = time.monotonic() + 30.0
    while not options.release.exists():
        if time.monotonic() >= deadline:
            return 2
        time.sleep(0.01)
    options.canary.touch(exist_ok=False)
    return 0


if __name__ == "__main__":
    require_launcher("phase4_confirmatory_operational_launcher_lifetime_probe_py")
    raise SystemExit(main())
