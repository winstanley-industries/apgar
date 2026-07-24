from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_confirmatory_decision_protocol as protocol
from tools import validate_phase4_representative_manifest_v2 as authorities
from tools import validate_phase4_statistical_protocol as protocol_v1


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryDecisionProtocolTest(unittest.TestCase):
    def _write(self, value: object) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "artifact.json"
        path.write_text(
            json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n",
            encoding="utf-8",
        )
        return path

    def test_checked_in_authorities_and_protocol_are_exact(self) -> None:
        representative, roster = authorities.validate_authorities()
        document = protocol.read_protocol()
        self.assertEqual(document, protocol.expected_protocol())
        self.assertEqual(representative["manifest_checksum"], 9613362670139358355)
        self.assertEqual(roster["manifest_checksum"], 14986327048461036142)
        self.assertEqual(
            authorities.budget_roster_checksum(representative),
            15913985307894145139,
        )
        self.assertEqual(document["artifact_checksum"], 7747512371013753061)
        self.assertEqual(len(protocol.expanded_cells()), 104)
        self.assertEqual(len(set(protocol.expanded_cells())), 104)
        self.assertEqual(
            sum(cell[3] == "confirmatory_raw_success" for cell in protocol.expanded_cells()),
            100,
        )

    def test_live_cpp_budget_preimages_match_all_102_cells(self) -> None:
        representative = authorities.validate_representative()
        expected = {
            (row["case_id"], item["pool"]): item["checksum"]
            for row in representative["canonical_algorithm_budgets"]
            for item in row["pool_checksums"]
        }
        run = subprocess.run(
            [str(_runfile("phase4_v2_canonical_budget_roster"))],
            check=True,
            text=True,
            capture_output=True,
        )
        actual: dict[tuple[int, int], int] = {}
        for line in run.stdout.splitlines():
            case_id, pool, checksum = (int(field) for field in line.split())
            self.assertNotIn((case_id, pool), actual)
            actual[(case_id, pool)] = checksum
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 102)

    def test_rechecksummed_manifest_and_roster_mutations_fail_frozen_authority(self) -> None:
        representative = copy.deepcopy(authorities.validate_representative())
        representative["cases"][0]["case_checksum"] ^= 1
        representative["manifest_checksum"] = authorities._payload_checksum(
            "APGAR-PHASE4-REPRESENTATIVE-MANIFEST-V2",
            representative,
            "manifest_checksum",
        )
        with self.assertRaisesRegex(authorities.AuthorityError, "frozen authority"):
            authorities.validate_representative(self._write(representative))

        roster = copy.deepcopy(authorities.validate_roster())
        roster["successful_cases"][0]["roster_checksum"] ^= 1
        roster["manifest_checksum"] = authorities._payload_checksum(
            "APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V2",
            roster,
            "manifest_checksum",
        )
        with self.assertRaisesRegex(authorities.AuthorityError, "frozen authority"):
            authorities.validate_roster(self._write(roster))

    def test_protocol_mutation_cannot_erase_firewall_or_reinterpret_v1(self) -> None:
        document = copy.deepcopy(protocol.read_protocol())
        document["protocol_state"]["heldout_outcomes_observed"] = True
        document["artifact_checksum"] = protocol._artifact_checksum(document)
        with self.assertRaisesRegex(protocol.ConfirmatoryProtocolError, "exactly reconstruct"):
            protocol.validate_document(document)

        document = copy.deepcopy(protocol.read_protocol())
        document["campaign"]["does_not_supersede_v1_decision"] = False
        document["artifact_checksum"] = protocol._artifact_checksum(document)
        with self.assertRaisesRegex(protocol.ConfirmatoryProtocolError, "exactly reconstruct"):
            protocol.validate_document(document)

    def test_campaign_is_disjoint_and_inherits_decision_rules_exactly(self) -> None:
        v1_ids = {case_id for case_id, _, _, _ in protocol.protocol_v4.expanded_cells()}
        v2_ids = {case_id for case_id, _, _, _ in protocol.expanded_cells()}
        self.assertTrue(v1_ids.isdisjoint(v2_ids))
        base = protocol_v1.expected_protocol()
        self.assertEqual(
            protocol.effective_decision_sections(),
            {
                key: base[key]
                for key in (
                    "outcome",
                    "inference",
                    "guardrails",
                    "timing",
                    "completion_requirements",
                )
            },
        )
        groups = {
            group["group"]: group for group in protocol.read_protocol()["matrix"]["cell_groups"]
        }
        for name in ("exact", "heldout", "imported"):
            self.assertIn(protocol._SAME_RUN_RAW, groups[name]["required_artifacts"])
            self.assertIn(protocol._SAME_RUN, groups[name]["required_artifacts"])
            self.assertIn(protocol._SAME_RUN_REPORT, groups[name]["required_artifacts"])
            self.assertIn(protocol._SAME_RUN_OPERATIONAL, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._RAW, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._REPORT, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._OPERATIONAL, groups[name]["required_artifacts"])
        for name in (
            "calibration",
            "fixed_query_raw",
            "stress_raw",
        ):
            self.assertIn(protocol._RAW, groups[name]["required_artifacts"])
            self.assertIn(protocol._REPORT, groups[name]["required_artifacts"])
            self.assertIn(protocol._OPERATIONAL, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._SAME_RUN_RAW, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._SAME_RUN, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._SAME_RUN_REPORT, groups[name]["required_artifacts"])
            self.assertNotIn(protocol._SAME_RUN_OPERATIONAL, groups[name]["required_artifacts"])
        for name in ("fixed_query_excluded", "stress_work_bound"):
            for artifact in (
                protocol._RAW,
                protocol._REPORT,
                protocol._OPERATIONAL,
                protocol._SAME_RUN_RAW,
                protocol._SAME_RUN,
                protocol._SAME_RUN_REPORT,
                protocol._SAME_RUN_OPERATIONAL,
            ):
                self.assertNotIn(artifact, groups[name]["required_artifacts"])

    def test_strict_canonical_bytes_duplicate_keys_types_and_bounds(self) -> None:
        canonical = protocol._canonical(protocol.expected_protocol())
        for text, pattern in (
            (canonical, "end in one LF"),
            (canonical + "\n\n", "end in one LF"),
            ("\ufeff" + canonical + "\n", "without BOM"),
            (json.dumps(protocol.expected_protocol(), indent=2) + "\n", "canonical compact"),
            (
                canonical.replace(
                    '{"schema_version":1,',
                    '{"schema_version":1,"schema_version":1,',
                    1,
                )
                + "\n",
                "duplicate JSON key",
            ),
        ):
            path = self._write({})
            path.write_text(text, encoding="utf-8")
            with self.assertRaisesRegex(protocol.ConfirmatoryProtocolError, pattern):
                protocol.read_protocol(path)

        bool_alias = copy.deepcopy(protocol.expected_protocol())
        bool_alias["schema_version"] = True
        with self.assertRaisesRegex(protocol.ConfirmatoryProtocolError, "exactly reconstruct"):
            protocol.validate_document(bool_alias)

        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "large.json"
            path.write_bytes(b" " * (protocol._MAX_BYTES + 1))
            with self.assertRaisesRegex(protocol.ConfirmatoryProtocolError, "256 KiB"):
                protocol.read_protocol(path)


if __name__ == "__main__":
    unittest.main()
