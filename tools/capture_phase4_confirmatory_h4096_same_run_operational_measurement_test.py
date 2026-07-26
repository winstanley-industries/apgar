"""Test-only unstamped H=4096 same-run operational capture entry point."""

from __future__ import annotations

from tools.capture_phase4_confirmatory_h4096_same_run_operational_measurement import main

if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_h4096_same_run_operational_capture_test_py")
    raise SystemExit(main(testing=True))
