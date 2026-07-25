"""One-use exec handshake for confirmatory operational Python authorities."""

from __future__ import annotations

import os
import pathlib
import sys

_ENVIRONMENT = "APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD"
_PREFIX = b"APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-LAUNCH-V1\n"
_MAXIMUM_TOKEN_BYTES = 512
_MAXIMUM_FILE_DESCRIPTOR = (1 << 31) - 1


def require_launcher(expected_target: str) -> None:
    """Refuse an inner target that was not exec-delegated by its fixed launcher."""
    descriptor_text = os.environ.pop(_ENVIRONMENT, None)
    try:
        if (
            descriptor_text is None
            or not descriptor_text.isascii()
            or not descriptor_text.isdecimal()
        ):
            raise ValueError
        parsed_descriptor = int(descriptor_text)
        if parsed_descriptor < 3 or parsed_descriptor > _MAXIMUM_FILE_DESCRIPTOR:
            raise ValueError
        descriptor = parsed_descriptor
        os.set_blocking(descriptor, False)
        chunks: list[bytes] = []
        size = 0
        while True:
            chunk = os.read(descriptor, _MAXIMUM_TOKEN_BYTES + 1 - size)
            if not chunk:
                break
            chunks.append(chunk)
            size += len(chunk)
            if size > _MAXIMUM_TOKEN_BYTES:
                raise ValueError
        if b"".join(chunks) != _PREFIX + expected_target.encode("ascii") + b"\n":
            raise ValueError
        module = pathlib.Path(__file__).absolute()
        runfiles_main = next(
            (
                parent
                for parent in module.parents
                if parent.name == "_main" and parent.parent.name.endswith(".runfiles")
            ),
            None,
        )
        if runfiles_main is None:
            raise ValueError
        inner_output = (runfiles_main / expected_target).resolve(strict=True)
        expected_launcher = inner_output.parent / expected_target.removesuffix("_py")
        parent_executable = pathlib.Path(f"/proc/{os.getppid()}/exe")
        if not expected_launcher.is_file() or not os.path.samefile(
            expected_launcher, parent_executable
        ):
            raise ValueError
    except (OSError, OverflowError, UnicodeError, ValueError):
        print(
            "confirmatory operational inner authority requires its compiled launcher",
            file=sys.stderr,
        )
        raise SystemExit(2) from None
    finally:
        if "descriptor" in locals():
            try:
                os.close(descriptor)
            except (OSError, OverflowError):
                pass
