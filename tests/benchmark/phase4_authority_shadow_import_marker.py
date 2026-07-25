"""Test-only import hook used to detect authority-boundary regressions."""

from __future__ import annotations

import os
import pathlib

marker = os.environ.get("APGAR_PHASE4_ENCLOSING_INIT_MARKER")
if marker:
    pathlib.Path(marker).touch()
