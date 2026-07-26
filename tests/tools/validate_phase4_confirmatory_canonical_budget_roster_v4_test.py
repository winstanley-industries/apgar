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
from tools import validate_phase4_confirmatory_canonical_budget_roster_v4 as roster_v4
from tools import validate_phase4_representative_manifest_v2 as authorities

_EXPECTED_V4_ROSTER_CHECKSUM = 12316700735749461907
_EXPECTED_V4_STDOUT_SHA256 = "1379050ceaf62bd5ff221827ab54565a9087b0c3fdbf04fa69fc81c7ea40e283"


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _live_roster(target: str) -> tuple[str, dict[tuple[int, int], int]]:
    run = subprocess.run(
        [str(_runfile(target))],
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
        case_id, pool, checksum = (int(field) for field in fields)
        if (case_id, pool) in result:
            raise AssertionError("live roster contains a duplicate cell")
        result[(case_id, pool)] = checksum
    return run.stdout, result


class Phase4ConfirmatoryCanonicalBudgetRosterV4Test(unittest.TestCase):
    def _write(self, value: object) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "roster.json"
        path.write_text(
            json.dumps(
                value,
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
            + "\n",
            encoding="utf-8",
        )
        return path

    def _rechecksum(self, value: dict[str, object]) -> pathlib.Path:
        value["roster_checksum"] = roster_v4.compute_roster_checksum(value)
        return self._write(value)

    def test_checked_in_roster_is_exact_and_directly_supersedes_v3(self) -> None:
        roster = roster_v4.validate_roster()
        predecessor = roster_v3.validate_roster()
        self.assertGreater(_EXPECTED_V4_ROSTER_CHECKSUM, 0)
        self.assertEqual(roster["schema_version"], 4)
        self.assertEqual(
            roster["authority"],
            "phase4_confirmatory_canonical_algorithm_budget_roster_v4",
        )
        self.assertEqual(
            roster["configuration_authority"],
            "phase4_confirmatory_corpus_v2_h4096_session_v5",
        )
        self.assertEqual(roster["cell_count"], 102)
        self.assertEqual(roster["roster_checksum"], _EXPECTED_V4_ROSTER_CHECKSUM)
        self.assertEqual(roster_v4._ROSTER_CHECKSUM, _EXPECTED_V4_ROSTER_CHECKSUM)
        self.assertEqual(
            roster["representative_manifest_checksum"],
            9613362670139358355,
        )
        self.assertEqual(
            roster["workload_roster_manifest_checksum"],
            14986327048461036142,
        )
        self.assertEqual(
            roster["supersedes"],
            {
                "schema_version": 3,
                "authority": "phase4_confirmatory_canonical_algorithm_budget_roster_v3",
                "roster_checksum": 18429170436700418962,
                "configuration_authority": "phase4_confirmatory_corpus_v2_h4096",
            },
        )
        self.assertEqual(
            roster["configuration"],
            {
                "present_step_per_overuse_unit": 1,
                "history_step_per_overuse_unit": 4096,
                "baseline_and_candidate_price_configs_equal": True,
                "superseded_candidate_allocation_session_schema_version": 4,
                "candidate_allocation_session_schema_version": 5,
                "superseded_targeted_regeneration_plan_schema_version": 2,
                "targeted_regeneration_plan_schema_version": 3,
                "superseded_targeted_regeneration_execution_schema_version": 5,
                "targeted_regeneration_execution_schema_version": 6,
                "all_non_session_canonical_algorithm_budget_fields_unchanged": True,
                "all_candidate_session_fields_except_schema_version_unchanged": True,
                "query_work_and_external_opportunity_unchanged": True,
            },
        )
        current = roster_v4.budget_map(roster)
        prior = roster_v3.budget_map(predecessor)
        self.assertEqual(len(current), 102)
        self.assertEqual(set(current), set(prior))
        self.assertTrue(all(current[cell] != prior[cell] for cell in prior))

    def test_live_v3_and_v4_preimages_are_deterministic_and_exact(self) -> None:
        expected_v3 = roster_v3.budget_map(roster_v3.validate_roster())
        expected_v4 = roster_v4.budget_map(roster_v4.validate_roster())
        v3_stdout, actual_v3 = _live_roster("phase4_v2_h4096_canonical_budget_roster")
        repeated_v3_stdout, repeated_v3 = _live_roster("phase4_v2_h4096_canonical_budget_roster")
        v4_stdout, actual_v4 = _live_roster("phase4_v2_h4096_session_v5_canonical_budget_roster")
        repeated_v4_stdout, repeated_v4 = _live_roster(
            "phase4_v2_h4096_session_v5_canonical_budget_roster"
        )
        self.assertEqual(actual_v3, expected_v3)
        self.assertEqual(repeated_v3, expected_v3)
        self.assertEqual(repeated_v3_stdout, v3_stdout)
        self.assertEqual(actual_v4, expected_v4)
        self.assertEqual(repeated_v4, expected_v4)
        self.assertEqual(repeated_v4_stdout, v4_stdout)
        self.assertEqual(
            hashlib.sha256(v3_stdout.encode()).hexdigest(),
            "81bd79b265e9022486876f443d4b911a25a6f8b9f0d08d9dc7dfe5080005ecef",
        )
        self.assertRegex(_EXPECTED_V4_STDOUT_SHA256, r"^[0-9a-f]{64}$")
        self.assertEqual(
            hashlib.sha256(v4_stdout.encode()).hexdigest(),
            _EXPECTED_V4_STDOUT_SHA256,
        )
        self.assertEqual(len(actual_v3), 102)
        self.assertEqual(len(actual_v4), 102)
        self.assertEqual(set(actual_v3), set(actual_v4))
        self.assertTrue(all(actual_v3[cell] != actual_v4[cell] for cell in actual_v3))
        self.assertTrue(v3_stdout)
        self.assertTrue(v4_stdout)

    def test_cross_version_authorities_are_rejected(self) -> None:
        with self.assertRaises(
            (
                roster_v4.BudgetRosterV4Error,
                roster_v3.BudgetRosterV3Error,
                authorities.AuthorityError,
            )
        ):
            roster_v4.validate_roster(roster_v3._ROSTER)
        with self.assertRaises((roster_v3.BudgetRosterV3Error, authorities.AuthorityError)):
            roster_v3.validate_roster(roster_v4._ROSTER)
        with self.assertRaises(
            (
                roster_v4.BudgetRosterV4Error,
                roster_v3.BudgetRosterV3Error,
                authorities.AuthorityError,
            )
        ):
            roster_v4.validate_roster(authorities._REPRESENTATIVE)

    def test_rechecksummed_ancestry_transition_and_predecessor_aliases_fail(self) -> None:
        predecessor = roster_v3.validate_roster()
        mutations = (
            (
                lambda value: value.__setitem__(
                    "representative_manifest_checksum",
                    value["representative_manifest_checksum"] ^ 1,
                ),
                "representative_manifest_checksum",
            ),
            (
                lambda value: value["supersedes"].__setitem__(
                    "authority",
                    "phase4_confirmatory_canonical_algorithm_budget_roster_v2",
                ),
                "supersedes",
            ),
            (
                lambda value: value["supersedes"].__setitem__(
                    "roster_checksum",
                    value["supersedes"]["roster_checksum"] ^ 1,
                ),
                "supersedes",
            ),
            (
                lambda value: value["supersedes"].__setitem__(
                    "configuration_authority",
                    "phase4_confirmatory_corpus_v2_h2250",
                ),
                "supersedes",
            ),
            (
                lambda value: value.__setitem__(
                    "configuration_authority",
                    "phase4_confirmatory_corpus_v2_h4096",
                ),
                "configuration_authority",
            ),
            (
                lambda value: value["configuration"].__setitem__(
                    "candidate_allocation_session_schema_version",
                    4,
                ),
                "configuration",
            ),
            (
                lambda value: value["configuration"].__setitem__(
                    "targeted_regeneration_plan_schema_version",
                    2,
                ),
                "configuration",
            ),
            (
                lambda value: value["configuration"].__setitem__(
                    "targeted_regeneration_execution_schema_version",
                    5,
                ),
                "configuration",
            ),
            (
                lambda value: value["configuration"].__setitem__(
                    "all_candidate_session_fields_except_schema_version_unchanged",
                    False,
                ),
                "configuration",
            ),
            (
                lambda value: value["canonical_algorithm_budgets"][0]["pool_checksums"][
                    0
                ].__setitem__(
                    "checksum",
                    predecessor["canonical_algorithm_budgets"][0]["pool_checksums"][0]["checksum"],
                ),
                "predecessor checksum",
            ),
        )
        for mutate, pattern in mutations:
            value = copy.deepcopy(roster_v4.validate_roster())
            mutate(value)
            with self.assertRaisesRegex(
                (
                    roster_v4.BudgetRosterV4Error,
                    roster_v3.BudgetRosterV3Error,
                    authorities.AuthorityError,
                ),
                pattern,
            ):
                roster_v4.validate_roster(self._rechecksum(value))

    def test_corruption_and_rechecksummed_forgery_both_fail(self) -> None:
        predecessor_checksum = roster_v3.validate_roster()["canonical_algorithm_budgets"][0][
            "pool_checksums"
        ][0]["checksum"]
        corrupted = copy.deepcopy(roster_v4.validate_roster())
        original_checksum = corrupted["canonical_algorithm_budgets"][0]["pool_checksums"][0][
            "checksum"
        ]
        replacement_checksum = next(
            checksum
            for checksum in (1, 2, 3)
            if checksum not in (original_checksum, predecessor_checksum)
        )
        corrupted["canonical_algorithm_budgets"][0]["pool_checksums"][0]["checksum"] = (
            replacement_checksum
        )
        with self.assertRaisesRegex(
            roster_v4.BudgetRosterV4Error,
            "does not authenticate its complete preimage",
        ):
            roster_v4.validate_roster(self._write(corrupted))

        forged = copy.deepcopy(roster_v4.validate_roster())
        forged["canonical_algorithm_budgets"][0]["pool_checksums"][0]["checksum"] = (
            replacement_checksum
        )
        with self.assertRaisesRegex(
            roster_v4.BudgetRosterV4Error,
            "differs from the frozen v4 authority",
        ):
            roster_v4.validate_roster(self._rechecksum(forged))

    def test_missing_duplicate_reordered_and_descriptor_only_cells_fail(self) -> None:
        mutations = []

        missing = copy.deepcopy(roster_v4.validate_roster())
        del missing["canonical_algorithm_budgets"][-2]
        mutations.append(missing)

        duplicate = copy.deepcopy(roster_v4.validate_roster())
        duplicate["canonical_algorithm_budgets"][-2] = copy.deepcopy(
            duplicate["canonical_algorithm_budgets"][-1]
        )
        mutations.append(duplicate)

        reordered = copy.deepcopy(roster_v4.validate_roster())
        reordered["canonical_algorithm_budgets"][0:2] = reversed(
            reordered["canonical_algorithm_budgets"][0:2]
        )
        mutations.append(reordered)

        reordered_pools = copy.deepcopy(roster_v4.validate_roster())
        reordered_pools["canonical_algorithm_budgets"][3]["pool_checksums"][0:2] = reversed(
            reordered_pools["canonical_algorithm_budgets"][3]["pool_checksums"][0:2]
        )
        mutations.append(reordered_pools)

        duplicate_pool = copy.deepcopy(roster_v4.validate_roster())
        duplicate_pool["canonical_algorithm_budgets"][3]["pool_checksums"][1] = copy.deepcopy(
            duplicate_pool["canonical_algorithm_budgets"][3]["pool_checksums"][0]
        )
        mutations.append(duplicate_pool)

        descriptor_only = copy.deepcopy(roster_v4.validate_roster())
        descriptor_only["canonical_algorithm_budgets"].append(
            {
                "case_id": 12000,
                "pool_checksums": [{"pool": 1024, "checksum": 1}],
            }
        )
        mutations.append(descriptor_only)

        for value in mutations:
            with self.assertRaises(
                (
                    roster_v4.BudgetRosterV4Error,
                    roster_v3.BudgetRosterV3Error,
                    authorities.AuthorityError,
                )
            ):
                roster_v4.validate_roster(self._rechecksum(value))

    def test_canonical_bytes_nested_order_types_and_bounds_are_strict(self) -> None:
        roster = roster_v4.validate_roster()
        canonical = json.dumps(roster, ensure_ascii=False, separators=(",", ":"))
        for text, pattern in (
            (canonical, "end in one LF"),
            (canonical + "\n\n", "end in one LF"),
            ("\ufeff" + canonical + "\n", "without BOM"),
            (json.dumps(roster, indent=2) + "\n", "canonical compact"),
            (
                canonical.replace(
                    '{"schema_version":4,',
                    '{"schema_version":4,"schema_version":4,',
                    1,
                )
                + "\n",
                "duplicate JSON key",
            ),
        ):
            path = self._write({})
            path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(authorities.AuthorityError, pattern):
                roster_v4.validate_roster(path)

        reordered_root = copy.deepcopy(roster)
        root_items = list(reordered_root.items())
        root_items[0:2] = reversed(root_items[0:2])
        with self.assertRaisesRegex(authorities.AuthorityError, "canonical order"):
            roster_v4.validate_roster(self._write(dict(root_items)))

        reordered_supersedes = copy.deepcopy(roster)
        supersedes_items = list(reordered_supersedes["supersedes"].items())
        supersedes_items[0:2] = reversed(supersedes_items[0:2])
        reordered_supersedes["supersedes"] = dict(supersedes_items)
        with self.assertRaisesRegex(authorities.AuthorityError, "canonical order"):
            roster_v4.validate_roster(self._write(reordered_supersedes))

        reordered_configuration = copy.deepcopy(roster)
        configuration_items = list(reordered_configuration["configuration"].items())
        configuration_items[0:2] = reversed(configuration_items[0:2])
        reordered_configuration["configuration"] = dict(configuration_items)
        with self.assertRaisesRegex(authorities.AuthorityError, "canonical order"):
            roster_v4.validate_roster(self._write(reordered_configuration))

        reordered_row = copy.deepcopy(roster)
        row_items = list(reordered_row["canonical_algorithm_budgets"][0].items())
        row_items[0:2] = reversed(row_items[0:2])
        reordered_row["canonical_algorithm_budgets"][0] = dict(row_items)
        with self.assertRaisesRegex(authorities.AuthorityError, "canonical order"):
            roster_v4.validate_roster(self._write(reordered_row))

        reordered_pool = copy.deepcopy(roster)
        pool_items = list(
            reordered_pool["canonical_algorithm_budgets"][0]["pool_checksums"][0].items()
        )
        pool_items[0:2] = reversed(pool_items[0:2])
        reordered_pool["canonical_algorithm_budgets"][0]["pool_checksums"][0] = dict(pool_items)
        with self.assertRaisesRegex(authorities.AuthorityError, "canonical order"):
            roster_v4.validate_roster(self._write(reordered_pool))

        aliases = []
        integer_authority = copy.deepcopy(roster)
        integer_authority["authority"] = 4
        aliases.append(integer_authority)

        bool_superseded_schema = copy.deepcopy(roster)
        bool_superseded_schema["supersedes"]["schema_version"] = True
        aliases.append(bool_superseded_schema)

        bool_cell_count = copy.deepcopy(roster)
        bool_cell_count["cell_count"] = True
        aliases.append(bool_cell_count)

        oversized_cell_count = copy.deepcopy(roster)
        oversized_cell_count["cell_count"] = 1 << 32
        aliases.append(oversized_cell_count)

        bool_present = copy.deepcopy(roster)
        bool_present["configuration"]["present_step_per_overuse_unit"] = True
        aliases.append(bool_present)

        integer_bool = copy.deepcopy(roster)
        integer_bool["configuration"]["baseline_and_candidate_price_configs_equal"] = 1
        aliases.append(integer_bool)

        oversized_schema = copy.deepcopy(roster)
        oversized_schema["configuration"]["candidate_allocation_session_schema_version"] = 1 << 32
        aliases.append(oversized_schema)

        negative_checksum = copy.deepcopy(roster)
        negative_checksum["canonical_algorithm_budgets"][0]["pool_checksums"][0]["checksum"] = -1
        aliases.append(negative_checksum)

        for value in aliases:
            with self.assertRaises(
                (
                    roster_v4.BudgetRosterV4Error,
                    roster_v3.BudgetRosterV3Error,
                    authorities.AuthorityError,
                )
            ):
                roster_v4.validate_roster(self._write(value))

    def test_generator_uses_only_the_fixed_private_session_v5_surface(self) -> None:
        builder = "BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5"
        source = _runfile("tools/phase4_v2_h4096_session_v5_canonical_budget_roster.cc").read_text(
            encoding="utf-8"
        )
        public_header = _runfile("include/apgar/benchmark/phase4_trial_harness.h").read_text(
            encoding="utf-8"
        )
        internal_header = _runfile(
            "src/benchmark/phase4_h4096_session_v5_canonical_budget_internal.h"
        ).read_text(encoding="utf-8")

        self.assertIn("int main()", source)
        self.assertIn(builder, source)
        self.assertNotIn("BuildPhase4CanonicalTrialSpecForCorpusV2H4096(", source)
        self.assertNotIn(builder, public_header)
        self.assertIn(builder, internal_header)
        self.assertIn(
            "BuildPhase4CanonicalTrialSpecForCorpusV2H4096(",
            internal_header,
        )
        self.assertIn(
            "kCpuCandidateAllocationSessionSchemaVersionV4",
            internal_header,
        )
        self.assertIn(
            "kCpuCandidateAllocationSessionSchemaVersionV5",
            internal_header,
        )
        self.assertNotRegex(
            internal_header,
            r"\bkCpuCandidateAllocationSessionSchemaVersion\b",
        )
        for forbidden in (
            "argc",
            "argv",
            "ExecutePhase4",
            "Phase4TrialExecutionAuthority",
            "BuildPhase4RepresentativeCase",
            "fixture",
            "CandidateStore",
            "CpuAStar",
        ):
            self.assertNotIn(forbidden, source)
            self.assertNotIn(forbidden, internal_header)


if __name__ == "__main__":
    unittest.main()
