"""Bazel-test-only launcher for current-V1 diagnostic validator coverage."""

from __future__ import annotations

import argparse
from collections.abc import Sequence
from pathlib import Path

from tests.support.phase4_current_diagnostic_budget import patch_live_diagnostic_budgets
from tools import (
    project_phase4_operational_evidence_v2,
    validate_phase4_exact_small_oracle,
    validate_phase4_exact_small_oracle_v2,
    validate_phase4_operational_measurement,
    validate_phase4_per_net_report_v2,
)
from tools import validate_phase4_raw_evidence as raw_validator

_REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
_TOOLS = {
    "exact-small-oracle": validate_phase4_exact_small_oracle.main,
    "exact-small-oracle-v2": validate_phase4_exact_small_oracle_v2.main,
    "operational-measurement": validate_phase4_operational_measurement.main,
    "operational-projection-v2": project_phase4_operational_evidence_v2.main,
    "per-net-report-v2": validate_phase4_per_net_report_v2.main,
}


def _runfile(relative: str) -> Path:
    return _REPOSITORY_ROOT / relative


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--testing-tool", choices=tuple(_TOOLS), required=True)
    options, remainder = parser.parse_known_args(argv)
    patcher = patch_live_diagnostic_budgets(raw_validator, _runfile)
    try:
        return _TOOLS[options.testing_tool](remainder)
    finally:
        patcher.stop()


if __name__ == "__main__":
    raise SystemExit(main())
