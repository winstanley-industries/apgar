"""Capture the frozen H=4096 ordinary Corpus-v2 operational development cell."""

from __future__ import annotations

from collections.abc import Sequence

from tools import capture_phase4_confirmatory_operational_measurement as implementation
from tools import phase4_confirmatory_h4096_operational_authority as authority


def main(argv: Sequence[str] | None = None, *, testing: bool = False) -> int:
    return implementation.main(
        argv,
        testing=testing,
        authority_module=authority,
        authority_label="H=4096 ordinary",
    )


if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_h4096_operational_capture_py")
    raise SystemExit(main())
