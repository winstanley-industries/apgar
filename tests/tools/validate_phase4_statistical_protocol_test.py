from __future__ import annotations

import copy
import json
import math
import pathlib
import tempfile
import unittest
from fractions import Fraction

from tools import validate_phase4_statistical_protocol as protocol


class Phase4StatisticalProtocolTest(unittest.TestCase):
    def test_checked_in_protocol_and_canonical_expansion(self) -> None:
        document = protocol.read_protocol()
        self.assertEqual(document, protocol.expected_protocol())
        cells = protocol.expanded_cells()
        self.assertEqual(len(cells), 104)
        self.assertEqual(len(set(cells)), 104)
        self.assertEqual(sum(row[3] == "raw_success" for row in cells), 100)
        self.assertEqual(sum(row[2] != "calibration" for row in cells), 86)
        self.assertEqual(
            sum(row[3] == "raw_success" and row[2] != "calibration" for row in cells), 82
        )
        self.assertIn((2000, 1024, "fixed_query", "descriptor_only_excluded"), cells)
        self.assertIn((2001, 1, "fixed_query", "descriptor_only_excluded"), cells)
        self.assertIn((3001, 4, "stress", "compiled_work_bound"), cells)
        self.assertIn((3002, 4, "stress", "compiled_work_bound"), cells)
        counts = {role: sum(row[2] == role for row in cells) for role in {row[2] for row in cells}}
        self.assertEqual(
            counts,
            {
                "exact": 3,
                "calibration": 18,
                "heldout": 72,
                "fixed_query": 5,
                "stress": 3,
                "imported": 3,
            },
        )
        groups = document["matrix"]["cell_groups"]
        self.assertEqual(protocol._expand_groups(groups), cells)
        self.assertIn("phase4_exact_small_oracle_v1", groups[0]["required_artifacts"])
        self.assertIn("phase4_same_run_decision_telemetry_v1", groups[2]["required_artifacts"])
        self.assertEqual(groups[3]["required_artifacts"], ["phase4_fixed_query_control_v1"])
        rejection_scope = set(document["guardrails"]["exact_rejection_scope_groups"])
        for group in groups:
            has_authority = "phase4_same_run_decision_telemetry_v1" in group["required_artifacts"]
            self.assertEqual(has_authority, group["group"] in rejection_scope)

    def test_literal_mutation_reorder_duplicate_and_checksum_fail(self) -> None:
        expected = protocol.expected_protocol()
        mutated = copy.deepcopy(expected)
        mutated["matrix"]["logical_cell_count"] = 105
        mutated["artifact_checksum"] = protocol._artifact_checksum(mutated)
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly reconstruct"):
            protocol.validate_document(mutated)

        reordered = {
            "protocol_state": expected["protocol_state"],
            "schema_version": expected["schema_version"],
            **{
                key: value
                for key, value in expected.items()
                if key not in {"protocol_state", "schema_version"}
            },
        }
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly reconstruct"):
            protocol.validate_document(reordered)

        raw = protocol._canonical(expected)
        duplicate = raw.replace(
            '{"schema_version":1,', '{"schema_version":1,"schema_version":1,', 1
        )
        self._write_and_reject(duplicate + "\n", "duplicate JSON key")

        wrong_checksum = copy.deepcopy(expected)
        wrong_checksum["artifact_checksum"] += 1
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly reconstruct"):
            protocol.validate_document(wrong_checksum)
        bool_alias = copy.deepcopy(expected)
        bool_alias["schema_version"] = True
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly reconstruct"):
            protocol.validate_document(bool_alias)

    def test_strict_wire_shape_and_bounds(self) -> None:
        canonical = protocol._canonical(protocol.expected_protocol())
        self._write_and_reject("\ufeff" + canonical + "\n", "without BOM")
        self._write_and_reject(
            json.dumps(protocol.expected_protocol(), indent=2) + "\n", "canonical compact"
        )
        self._write_and_reject(canonical, "end in one LF")
        self._write_and_reject(canonical + "\n\n", "end in one LF")
        self._write_and_reject(
            canonical.replace('"logical_cell_count":104', '"logical_cell_count":NaN', 1) + "\n",
            "non-finite",
        )
        deep: object = 0
        for _ in range(66):
            deep = [deep]
        value = protocol.expected_protocol()
        value["unexpected"] = deep
        with self.assertRaisesRegex(protocol.ProtocolError, "nesting"):
            protocol.validate_document(value)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "large.json"
            path.write_bytes(b" " * (256 * 1024 + 1))
            with self.assertRaisesRegex(protocol.ProtocolError, "256 KiB"):
                protocol.read_protocol(path)
        self._write_and_reject(
            '{"x":' + "[" * 2000 + "0" + "]" * 2000 + "}\n", "nesting|invalid protocol JSON"
        )

    def test_no_observed_result_fields(self) -> None:
        forbidden = {
            "winner",
            "winners",
            "observed_commit",
            "hardware",
            "pass",
            "passed",
            "observed_results",
            "confidence_interval",
        }

        def walk(value: object) -> None:
            if isinstance(value, dict):
                self.assertTrue(forbidden.isdisjoint(value))
                for child in value.values():
                    walk(child)
            elif isinstance(value, list):
                for child in value:
                    walk(child)

        document = protocol.read_protocol()
        walk(document)
        self.assertEqual(
            document["protocol_state"], {"protocol_frozen": True, "decision_not_evaluated": True}
        )

    def test_exact_lexicographic_precedence(self) -> None:
        baseline = self._make_outcome(10, 0, 100)
        self.assertEqual(protocol.compare_outcomes(self._make_outcome(11, 999, 999), baseline), 1)
        self.assertEqual(protocol.compare_outcomes(self._make_outcome(9, 0, 0), baseline), -1)
        self.assertEqual(protocol.compare_outcomes(self._make_outcome(10, 1, 0), baseline), -1)
        self.assertEqual(protocol.compare_outcomes(self._make_outcome(10, 0, 99), baseline), 1)
        self.assertEqual(protocol.compare_outcomes(baseline, baseline), 0)

    def test_sign_holm_golden_boundaries_ties_and_zero(self) -> None:
        self.assertEqual(protocol.exact_sign_p(8, 0), Fraction(1, 256))
        self.assertEqual(protocol.exact_sign_p(7, 0), Fraction(1, 128))
        self.assertEqual(protocol.exact_sign_p(6, 1), Fraction(1, 16))
        self.assertEqual(protocol.exact_sign_p(0, 0), Fraction(1, 1))
        with self.assertRaisesRegex(protocol.ProtocolError, "nonnegative"):
            protocol.exact_sign_p(True, 0)
        self.assertEqual(
            protocol.holm_qualifying_families({0: (8, 0, 0), 1: (8, 0, 0), 2: (6, 2, 0)}), (0, 1)
        )
        self.assertEqual(
            protocol.holm_qualifying_families({0: (8, 0, 0), 1: (7, 1, 0), 2: (7, 1, 0)}), (0,)
        )
        # Ties are published but discarded from the exact sign sample.
        self.assertEqual(
            protocol.holm_qualifying_families({0: (7, 0, 1), 1: (8, 0, 0), 2: (6, 2, 0)}), (0, 1)
        )
        self.assertEqual(
            protocol.holm_qualifying_families({0: (8, 0, 0), 1: (0, 0, 8), 2: (6, 2, 0)}), (0,)
        )
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly eight"):
            protocol.holm_qualifying_families({0: (9, 0, 0), 1: (8, 0, 0), 2: (6, 2, 0)})

    def test_clopper_pearson_is_outward_and_zero_sample_is_tagged(self) -> None:
        interval = protocol.clopper_pearson_ppb(8, 0)
        self.assertEqual(interval["status"], "available")
        self.assertLessEqual(interval["lower_ppb"], 630_583_352)
        self.assertEqual(interval["upper_ppb"], 1_000_000_000)
        mirror = protocol.clopper_pearson_ppb(0, 8)
        self.assertEqual(mirror["lower_ppb"], 0)
        self.assertGreaterEqual(mirror["upper_ppb"], 369_416_648)
        self.assertEqual(protocol.clopper_pearson_ppb(1, 0)["lower_ppb"], 25_000_000)
        self.assertEqual(protocol.clopper_pearson_ppb(0, 1)["upper_ppb"], 975_000_000)
        self.assertEqual(
            protocol.clopper_pearson_ppb(0, 0),
            {"status": "unavailable", "reason": "zero_non_ties"},
        )
        with self.assertRaisesRegex(protocol.ProtocolError, "nonnegative integers"):
            protocol.clopper_pearson_ppb(True, 0)

    def test_clopper_pearson_grid_bounds_are_tight_for_all_heldout_counts(self) -> None:
        scale = 1_000_000_000
        for non_ties in range(1, 9):
            denominator = scale**non_ties
            for wins in range(non_ties + 1):
                with self.subTest(non_ties=non_ties, wins=wins):
                    interval = protocol.clopper_pearson_ppb(wins, non_ties - wins)
                    lower = interval["lower_ppb"]
                    upper = interval["upper_ppb"]

                    def tail(q: int) -> int:
                        return sum(
                            math.comb(non_ties, k) * q**k * (scale - q) ** (non_ties - k)
                            for k in range(wins, non_ties + 1)
                        )

                    def cdf(q: int) -> int:
                        return sum(
                            math.comb(non_ties, k) * q**k * (scale - q) ** (non_ties - k)
                            for k in range(wins + 1)
                        )

                    if wins != 0:
                        self.assertLessEqual(40 * tail(lower), denominator)
                        self.assertGreater(40 * tail(lower + 1), denominator)
                    if wins != non_ties:
                        self.assertLessEqual(40 * cdf(upper), denominator)
                        self.assertGreater(40 * cdf(upper - 1), denominator)

    def test_tie_bounded_fraction_is_exact_and_inclusive(self) -> None:
        self.assertEqual(
            protocol.tie_bounded_win_fraction(5, 1, 2),
            (Fraction(5, 8), Fraction(7, 8)),
        )
        with self.assertRaisesRegex(protocol.ProtocolError, "exactly eight"):
            protocol.tie_bounded_win_fraction(5, 1, 3)

    def test_no_k_or_repetition_inflation_and_pin_is_required(self) -> None:
        inference = protocol.read_protocol()["inference"]
        self.assertEqual(inference["experimental_unit"], "one_of_eight_heldout_cases_per_family")
        self.assertFalse(inference["repetitions_are_samples"])
        self.assertFalse(inference["pool_strata_are_samples"])
        self.assertFalse(inference["sensitivity_can_rescue"])
        self.assertEqual(inference["primary_pool"], 8)
        self.assertEqual(inference["required_qualifying_family_id"], 1)
        local_only = protocol.holm_qualifying_families({0: (8, 0, 0), 1: (0, 8, 0), 2: (8, 0, 0)})
        self.assertEqual(local_only, (0, 2))
        self.assertNotIn(inference["required_qualifying_family_id"], local_only)

    def test_imported_and_per_cell_guardrails(self) -> None:
        baseline = self._make_outcome(2, 3, 100)
        self.assertTrue(
            protocol.guardrail_cell(self._make_outcome(3, 3, 165), baseline, imported=True)
        )
        self.assertFalse(
            protocol.guardrail_cell(self._make_outcome(2, 4, 1), baseline, imported=True)
        )
        self.assertFalse(
            protocol.guardrail_cell(self._make_outcome(3, 3, 166), baseline, imported=True)
        )
        self.assertFalse(
            protocol.guardrail_cell(self._make_outcome(1, 0, 1), baseline, imported=False)
        )
        # Equal selected and overuse cannot be offset by another cell.
        self.assertFalse(
            protocol.guardrail_cell(self._make_outcome(2, 3, 101), baseline, imported=False)
        )
        zero = self._make_outcome(0, 0, 0)
        self.assertTrue(protocol.guardrail_cell(zero, zero, imported=True))
        self.assertTrue(protocol.guardrail_cell(self._make_outcome(1, 0, 1), zero, imported=True))

    def test_timing_rounding_order_balance_ci_and_disagreement(self) -> None:
        rows = []
        for repetition in range(20):
            order = "AB" if repetition % 2 == 0 else "BA"
            candidate = 90 if order == "AB" else 110
            rows.append(
                {
                    "repetition": repetition,
                    "order": order,
                    "baseline_outer_ns": 100,
                    "candidate_outer_ns": candidate,
                }
            )
        summary = protocol.timing_summary(rows)
        self.assertEqual(summary["ab_median_ratio_ppm"], 900_000)
        self.assertEqual(summary["ba_median_ratio_ppm"], 1_100_000)
        self.assertFalse(summary["direction_claim_allowed"])
        self.assertEqual(summary["median_ci_ppm"], [900_000, 1_100_000])
        rows[0]["baseline_outer_ns"] = 0
        with self.assertRaisesRegex(protocol.ProtocolError, "invalid"):
            protocol.timing_summary(rows)
        rows[0]["baseline_outer_ns"] = 100
        rows[0]["order"] = "BA"
        with self.assertRaisesRegex(protocol.ProtocolError, "invalid"):
            protocol.timing_summary(rows)
        rows[0]["order"] = "AB"
        rows[0]["repetition"] = False
        with self.assertRaisesRegex(protocol.ProtocolError, "canonical"):
            protocol.timing_summary(rows)
        rows[0]["repetition"] = 0
        rows[0]["baseline_outer_ns"] = True
        with self.assertRaisesRegex(protocol.ProtocolError, "invalid"):
            protocol.timing_summary(rows)

    def test_timing_ci_coverage_literal_is_exactly_rounded(self) -> None:
        denominator = 1 << 20
        numerator = denominator - 2 * sum(math.comb(20, k) for k in range(6))
        rounded_ppm = (numerator * 1_000_000 + denominator // 2) // denominator
        self.assertEqual(rounded_ppm, 958_611)
        self.assertEqual(protocol.read_protocol()["timing"]["median_ci_coverage_ppm"], rounded_ppm)

    def test_roster_sidecar_authenticates_excluded_fixed_query_cases(self) -> None:
        roster = json.loads(protocol._ROSTER_MANIFEST.read_text(encoding="utf-8"))
        roster["excluded_cases"][0]["descriptor_fingerprint"] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            changed = pathlib.Path(directory) / "roster.json"
            changed.write_text(json.dumps(roster), encoding="utf-8")
            with self.assertRaisesRegex(protocol.ProtocolError, "workload-net roster"):
                protocol._cross_validate_manifest(roster_path=changed)

    def test_incomplete_is_not_a_loss_and_completion_inputs_are_explicit(self) -> None:
        document = protocol.read_protocol()
        self.assertEqual(
            document["inference"]["missing_or_failed_evidence"], "incomplete_never_loss"
        )
        self.assertEqual(
            document["guardrails"]["exact_rejection_authority"],
            "future_same_run_decision_telemetry_only",
        )
        self.assertFalse(document["guardrails"]["diagnostic_rerun_can_decide"])
        self.assertIn("exact_small_exhaustive_oracle", document["completion_requirements"])
        self.assertIn("complete_toolchain_hardware_provenance", document["completion_requirements"])

    def _write_and_reject(self, text: str, pattern: str) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "protocol.json"
            path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(protocol.ProtocolError, pattern):
                protocol.read_protocol(path)

    @staticmethod
    def _make_outcome(selected: int, overuse: int, cost: int) -> dict[str, int]:
        return {
            "selected_net_count": selected,
            "total_overuse_units": overuse,
            "total_intrinsic_cost": cost,
        }


if __name__ == "__main__":
    unittest.main()
