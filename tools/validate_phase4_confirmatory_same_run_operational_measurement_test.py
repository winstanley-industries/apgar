"""Test-only publisher for a confirmatory same-run operational test capture."""

from __future__ import annotations

from tools.validate_phase4_confirmatory_same_run_operational_measurement import main

if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_same_run_operational_measurement_test_validator_py")
    raise SystemExit(main(testing=True))
