"""Descriptor-pinned bounded input for Phase 4 JSON validators."""

from __future__ import annotations

import os
import pathlib
import stat


class BoundedInputError(OSError):
    """A stable rejection for an unsafe validator input object."""


def read_regular_file(path: pathlib.Path, maximum_bytes: int, *, label: str) -> bytes:
    """Read at most maximum_bytes + 1 from one nonblocking regular-file descriptor."""
    flags = os.O_RDONLY | os.O_NONBLOCK | getattr(os, "O_CLOEXEC", 0)
    descriptor = os.open(path, flags)
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise BoundedInputError(f"{label} input must resolve to a regular file")
        chunks: list[bytes] = []
        remaining = maximum_bytes + 1
        while remaining:
            chunk = os.read(descriptor, remaining)
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)
    finally:
        os.close(descriptor)
