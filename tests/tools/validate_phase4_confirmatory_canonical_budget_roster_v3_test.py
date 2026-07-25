from __future__ import annotations

import copy
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as roster_v3
from tools import validate_phase4_representative_manifest_v2 as authorities


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _live_roster(target: str) -> tuple[str, dict[tuple[int, int], int]]:
    run = subprocess.run(
        [str(_runfile(target))],
        check=True,
        text=True,
        capture_output=True,
    )
    result: dict[tuple[int, int], int] = {}
    for line in run.stdout.splitlines():
        case_id, pool, checksum = (int(field) for field in line.split())
        if (case_id, pool) in result:
            raise AssertionError("live roster contains a duplicate cell")
        result[(case_id, pool)] = checksum
    return run.stdout, result


class Phase4ConfirmatoryCanonicalBudgetRosterV3Test(unittest.TestCase):
    def _write(self, value: object) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "roster.json"
        path.write_text(
            json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        return path

    def _rechecksum(self, value: dict[str, object]) -> pathlib.Path:
        value["roster_checksum"] = roster_v3.compute_roster_checksum(value)
        return self._write(value)

    def test_checked_in_roster_is_exact_and_anchored_to_v2_authorities(self) -> None:
        roster = roster_v3.validate_roster()
        self.assertEqual(roster["authority"], roster_v3._AUTHORITY)
        self.assertEqual(
            roster["configuration_authority"],
            "phase4_confirmatory_corpus_v2_h4096",
        )
        self.assertEqual(roster["cell_count"], 102)
        self.assertEqual(roster["roster_checksum"], 18429170436700418962)
        self.assertEqual(
            roster["representative_manifest_checksum"],
            authorities._REPRESENTATIVE_MANIFEST_CHECKSUM,
        )
        self.assertEqual(
            roster["workload_roster_manifest_checksum"],
            authorities._ROSTER_MANIFEST_CHECKSUM,
        )
        self.assertEqual(
            roster["supersedes"]["roster_checksum"],
            authorities._BUDGET_ROSTER_CHECKSUM,
        )

    def test_live_h2250_and_h4096_preimages_match_their_complete_rosters(self) -> None:
        representative = authorities.validate_representative()
        expected_h2250 = {
            (row["case_id"], pool["pool"]): pool["checksum"]
            for row in representative["canonical_algorithm_budgets"]
            for pool in row["pool_checksums"]
        }
        expected_h4096 = roster_v3.budget_map(roster_v3.validate_roster())
        h2250_stdout, actual_h2250 = _live_roster("phase4_v2_canonical_budget_roster")
        h4096_stdout, actual_h4096 = _live_roster("phase4_v2_h4096_canonical_budget_roster")
        repeated_stdout, repeated_h4096 = _live_roster("phase4_v2_h4096_canonical_budget_roster")
        self.assertEqual(actual_h2250, expected_h2250)
        self.assertEqual(actual_h4096, expected_h4096)
        self.assertEqual(repeated_h4096, actual_h4096)
        self.assertEqual(repeated_stdout, h4096_stdout)
        self.assertEqual(
            hashlib.sha256(h2250_stdout.encode()).hexdigest(),
            "898de4dfaae3bdb7a1af1108ae5c4d80c6ac5e529fe1bd2947c6db7cdac7e06f",
        )
        self.assertEqual(
            hashlib.sha256(h4096_stdout.encode()).hexdigest(),
            "81bd79b265e9022486876f443d4b911a25a6f8b9f0d08d9dc7dfe5080005ecef",
        )
        self.assertEqual(len(actual_h2250), 102)
        self.assertEqual(len(actual_h4096), 102)
        self.assertEqual(set(actual_h2250), set(actual_h4096))
        self.assertTrue(all(actual_h2250[cell] != actual_h4096[cell] for cell in actual_h2250))
        self.assertTrue(h2250_stdout)

    def test_cross_version_authorities_are_rejected(self) -> None:
        with self.assertRaises((roster_v3.BudgetRosterV3Error, authorities.AuthorityError)):
            roster_v3.validate_roster(authorities._REPRESENTATIVE)
        with self.assertRaises(authorities.AuthorityError):
            authorities.validate_representative(roster_v3._ROSTER)

    def test_rechecksummed_ancestry_configuration_and_h2250_aliases_fail(self) -> None:
        for mutate, pattern in (
            (
                lambda value: value.__setitem__(
                    "representative_manifest_checksum",
                    value["representative_manifest_checksum"] ^ 1,
                ),
                "representative_manifest_checksum",
            ),
            (
                lambda value: value["configuration"].__setitem__(
                    "baseline_and_candidate_price_configs_equal",
                    False,
                ),
                "configuration",
            ),
            (
                lambda value: value["canonical_algorithm_budgets"][0]["pool_checksums"][
                    0
                ].__setitem__(
                    "checksum",
                    authorities.validate_representative()["canonical_algorithm_budgets"][0][
                        "pool_checksums"
                    ][0]["checksum"],
                ),
                "replace all 102",
            ),
        ):
            value = copy.deepcopy(roster_v3.validate_roster())
            mutate(value)
            with self.assertRaisesRegex(
                (roster_v3.BudgetRosterV3Error, authorities.AuthorityError),
                pattern,
            ):
                roster_v3.validate_roster(self._rechecksum(value))

    def test_missing_duplicate_reordered_and_descriptor_only_cells_fail(self) -> None:
        mutations = []

        missing = copy.deepcopy(roster_v3.validate_roster())
        del missing["canonical_algorithm_budgets"][-2]
        mutations.append(missing)

        duplicate = copy.deepcopy(roster_v3.validate_roster())
        duplicate["canonical_algorithm_budgets"][-2] = copy.deepcopy(
            duplicate["canonical_algorithm_budgets"][-1]
        )
        mutations.append(duplicate)

        reordered = copy.deepcopy(roster_v3.validate_roster())
        reordered["canonical_algorithm_budgets"][0:2] = reversed(
            reordered["canonical_algorithm_budgets"][0:2]
        )
        mutations.append(reordered)

        reordered_pools = copy.deepcopy(roster_v3.validate_roster())
        reordered_pools["canonical_algorithm_budgets"][3]["pool_checksums"][0:2] = reversed(
            reordered_pools["canonical_algorithm_budgets"][3]["pool_checksums"][0:2]
        )
        mutations.append(reordered_pools)

        duplicate_pool = copy.deepcopy(roster_v3.validate_roster())
        duplicate_pool["canonical_algorithm_budgets"][3]["pool_checksums"][1] = copy.deepcopy(
            duplicate_pool["canonical_algorithm_budgets"][3]["pool_checksums"][0]
        )
        mutations.append(duplicate_pool)

        descriptor_only = copy.deepcopy(roster_v3.validate_roster())
        descriptor_only["canonical_algorithm_budgets"].append(
            {
                "case_id": 12000,
                "pool_checksums": [{"pool": 1024, "checksum": 1}],
            }
        )
        mutations.append(descriptor_only)

        for value in mutations:
            with self.assertRaises((roster_v3.BudgetRosterV3Error, authorities.AuthorityError)):
                roster_v3.validate_roster(self._rechecksum(value))

    def test_canonical_bytes_duplicate_keys_types_and_bounds_are_strict(self) -> None:
        roster = roster_v3.validate_roster()
        canonical = json.dumps(roster, ensure_ascii=False, separators=(",", ":"))
        for text, pattern in (
            (canonical, "end in one LF"),
            (canonical + "\n\n", "end in one LF"),
            ("\ufeff" + canonical + "\n", "without BOM"),
            (json.dumps(roster, indent=2) + "\n", "canonical compact"),
            (
                canonical.replace(
                    '{"schema_version":3,',
                    '{"schema_version":3,"schema_version":3,',
                    1,
                )
                + "\n",
                "duplicate JSON key",
            ),
        ):
            path = self._write({})
            path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(authorities.AuthorityError, pattern):
                roster_v3.validate_roster(path)

        bool_alias = copy.deepcopy(roster)
        bool_alias["cell_count"] = True
        with self.assertRaises((roster_v3.BudgetRosterV3Error, authorities.AuthorityError)):
            roster_v3.validate_roster(self._write(bool_alias))

        for field, alias in (
            ("present_step_per_overuse_unit", True),
            ("baseline_and_candidate_price_configs_equal", 1),
        ):
            bool_alias = copy.deepcopy(roster)
            bool_alias["configuration"][field] = alias
            with self.assertRaises((roster_v3.BudgetRosterV3Error, authorities.AuthorityError)):
                roster_v3.validate_roster(self._rechecksum(bool_alias))

    def test_generator_has_no_runtime_or_caller_selected_execution_surface(self) -> None:
        source = _runfile("tools/phase4_v2_h4096_canonical_budget_roster.cc").read_text(
            encoding="utf-8"
        )
        public_header = _runfile("include/apgar/benchmark/phase4_trial_harness.h").read_text(
            encoding="utf-8"
        )
        internal_header = _runfile(
            "src/benchmark/phase4_h4096_canonical_budget_internal.h"
        ).read_text(encoding="utf-8")
        self.assertIn("int main()", source)
        self.assertIn("BuildPhase4CanonicalTrialSpecForCorpusV2H4096", source)
        self.assertNotIn("BuildPhase4CanonicalTrialSpecForCorpusV2H4096", public_header)
        self.assertIn("BuildPhase4CanonicalTrialSpecForCorpusV2H4096", internal_header)
        for forbidden in (
            "argc",
            "argv",
            "ExecutePhase4",
            "BuildPhase4RepresentativeCase",
            "fixture",
        ):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
