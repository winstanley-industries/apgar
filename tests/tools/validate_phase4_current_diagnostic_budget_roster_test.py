from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import unittest

from tests.support import phase4_current_diagnostic_budget as diagnostic_budget
from tools import validate_phase4_raw_evidence as raw_validator


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _live_roster() -> tuple[str, dict[tuple[int, int], int]]:
    run = subprocess.run(
        [str(_runfile("phase4_current_diagnostic_budget_roster"))],
        check=True,
        text=True,
        capture_output=True,
    )
    if run.stderr:
        raise AssertionError(f"live roster wrote stderr: {run.stderr}")
    result: dict[tuple[int, int], int] = {}
    for line in run.stdout.splitlines():
        fields = line.split()
        if len(fields) != 3:
            raise AssertionError("live roster row does not have exactly three fields")
        case_id, pool_size, checksum = (int(field) for field in fields)
        key = (case_id, pool_size)
        if key in result:
            raise AssertionError("live roster contains a duplicate cell")
        result[key] = checksum
    return run.stdout, result


class Phase4CurrentDiagnosticBudgetRosterTest(unittest.TestCase):
    def test_checked_in_session_v5_roster_matches_live_preimages(self) -> None:
        expected = diagnostic_budget._read_live_diagnostic_budgets(
            raw_validator,
            _runfile("schemas/benchmark/phase4_current_v1_diagnostic_budget_roster_v2.json"),
        )
        first_stdout, first = _live_roster()
        second_stdout, second = _live_roster()

        self.assertEqual(first, expected)
        self.assertEqual(second, expected)
        self.assertEqual(second_stdout, first_stdout)
        self.assertEqual(
            hashlib.sha256(first_stdout.encode()).hexdigest(),
            "3cc341492101d8f8367b8cce7de9d19191637751db6a135f0fa7e462e055e339",
        )


if __name__ == "__main__":
    unittest.main()
