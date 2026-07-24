"""Test-only publisher for a confirmatory operational test capture."""

from __future__ import annotations

from tools.validate_phase4_confirmatory_operational_measurement import main

if __name__ == "__main__":
    raise SystemExit(main(testing=True))
