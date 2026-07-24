"""Test-only unstamped entry point for confirmatory operational capture."""

from __future__ import annotations

from tools.capture_phase4_confirmatory_operational_measurement import main

if __name__ == "__main__":
    raise SystemExit(main(testing=True))
