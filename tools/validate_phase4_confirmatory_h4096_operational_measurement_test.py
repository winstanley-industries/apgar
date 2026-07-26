"""Test-only publisher for an H=4096 ordinary operational capture."""

from __future__ import annotations

from tools.validate_phase4_confirmatory_h4096_operational_measurement import main

if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_h4096_operational_measurement_test_validator_py")
    raise SystemExit(main(testing=True))
