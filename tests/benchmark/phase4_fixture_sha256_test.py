import hashlib
import os
from pathlib import Path

EXPECTED_BYTES = 1_781
EXPECTED_SHA256 = "8c34f22a88debc213118b70d799e484d5a316104df0e0f68147f7f074f196075"


def main() -> None:
    fixture = (
        Path(os.environ["TEST_SRCDIR"])
        / os.environ["TEST_WORKSPACE"]
        / "tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"
    ).read_bytes()
    assert len(fixture) == EXPECTED_BYTES
    assert hashlib.sha256(fixture).hexdigest() == EXPECTED_SHA256


if __name__ == "__main__":
    main()
