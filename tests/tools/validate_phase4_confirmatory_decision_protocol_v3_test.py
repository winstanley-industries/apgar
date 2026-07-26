from __future__ import annotations

import copy
import hashlib
import json
import pathlib
import tempfile
import unittest
from collections.abc import Callable
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v4 as budget_v4
from tools import validate_phase4_confirmatory_decision_protocol as protocol_v1
from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_confirmatory_decision_protocol_v3 as protocol


class Phase4ConfirmatoryDecisionProtocolV3Test(unittest.TestCase):
    def _write_text(self, text: str) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "protocol.json"
        path.write_text(text, encoding="utf-8")
        return path

    def _write(self, value: object) -> pathlib.Path:
        return self._write_text(
            json.dumps(
                value,
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
            + "\n"
        )

    def _rechecksum(self, value: dict[str, Any]) -> dict[str, Any]:
        value["artifact_checksum"] = protocol._artifact_checksum(value)
        self.assertEqual(value["artifact_checksum"], protocol._artifact_checksum(value))
        return value

    def _assert_rechecksummed_mutations_rejected(
        self,
        mutations: list[tuple[str, Callable[[dict[str, Any]], None]]],
    ) -> None:
        for label, mutate in mutations:
            with self.subTest(label=label):
                value = copy.deepcopy(protocol.expected_protocol())
                mutate(value)
                self._rechecksum(value)
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV3Error,
                    "exactly reconstruct",
                ):
                    protocol.validate_document(value)

    def test_checked_in_protocol_is_exact_canonical_and_binds_v2_and_roster_v4(
        self,
    ) -> None:
        prior = protocol_v2.read_protocol()
        roster = budget_v4.validate_roster()
        document = protocol.read_protocol()

        self.assertEqual(document, protocol.expected_protocol())
        self.assertEqual(document["artifact_checksum"], 4963299999381388941)
        self.assertEqual(document["artifact_checksum"], protocol._ARTIFACT_CHECKSUM)
        self.assertEqual(document["artifact_checksum"], protocol._artifact_checksum(document))
        self.assertNotEqual(document["artifact_checksum"], prior["artifact_checksum"])
        self.assertEqual(
            document["supersedes"],
            {
                "schema_version": 2,
                "artifact_checksum": 11520586171987743043,
            },
        )
        self.assertEqual(document["supersedes"]["schema_version"], prior["schema_version"])
        self.assertEqual(
            document["supersedes"]["artifact_checksum"],
            prior["artifact_checksum"],
        )

        raw = protocol._PROTOCOL.read_bytes()
        self.assertEqual(len(raw), 6303)
        self.assertEqual(
            hashlib.sha256(raw).hexdigest(),
            "c3812719674fdf6958379272fbb3b2af4439ca12cdc2532b5dda230097dd473d",
        )
        self.assertEqual(raw, (protocol._canonical(document) + "\n").encode("utf-8"))

        authority = document["canonical_algorithm_budget_authority"]
        self.assertEqual(authority, protocol._budget_authority())
        for field in (
            "authority",
            "schema_version",
            "corpus_version",
            "corpus_checksum",
            "representative_manifest_schema_version",
            "representative_manifest_checksum",
            "workload_roster_manifest_schema_version",
            "workload_roster_manifest_checksum",
            "configuration_authority",
            "cell_count",
            "roster_checksum",
        ):
            self.assertEqual(authority[field], roster[field])
        self.assertEqual(authority["configuration"], roster["configuration"])
        self.assertEqual(
            authority["superseded_roster_schema_version"],
            roster["supersedes"]["schema_version"],
        )
        self.assertEqual(
            authority["superseded_roster_authority"],
            roster["supersedes"]["authority"],
        )
        self.assertEqual(
            authority["superseded_roster_checksum"],
            roster["supersedes"]["roster_checksum"],
        )
        self.assertEqual(
            authority["superseded_configuration_authority"],
            roster["supersedes"]["configuration_authority"],
        )
        self.assertEqual(
            authority["configuration"],
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

    def test_matrix_and_decision_rules_equal_v2_after_namespace_normalization(
        self,
    ) -> None:
        document = protocol.read_protocol()
        cells = protocol.expanded_cells()
        self.assertEqual(cells, protocol_v2.expanded_cells())
        self.assertEqual(len(cells), 104)
        self.assertEqual(len(set(cells)), 104)
        self.assertEqual(
            protocol.effective_decision_sections(),
            protocol_v2.effective_decision_sections(),
        )

        replacements = {
            authority: supersedes for _, supersedes, authority in protocol._ARTIFACT_SUBSTITUTIONS
        }
        normalized_groups = copy.deepcopy(protocol.effective_cell_groups())
        for group in normalized_groups:
            group["required_artifacts"] = [
                replacements.get(artifact, artifact) for artifact in group["required_artifacts"]
            ]
        self.assertEqual(normalized_groups, protocol_v2.effective_cell_groups())
        self.assertEqual(
            document["matrix_authority_counts"],
            protocol_v2.read_protocol()["matrix_authority_counts"],
        )
        self.assertEqual(
            document["unchanged_v1_sections"],
            protocol_v2.read_protocol()["unchanged_v1_sections"],
        )
        self.assertEqual(
            sum(disposition == "confirmatory_raw_success" for *_, disposition in cells),
            100,
        )
        self.assertEqual(sum(role != "calibration" for _, _, role, _ in cells), 86)

    def test_artifact_namespace_is_exact_one_to_one_and_advances_v2(self) -> None:
        prior = protocol_v2.read_protocol()
        namespace = protocol.read_protocol()["artifact_authority_namespace"]
        substitutions = namespace["substitutions"]
        self.assertEqual(substitutions, protocol._substitution_rows())
        self.assertEqual(len(substitutions), 10)

        purposes = {row["purpose"] for row in substitutions}
        superseded = {row["supersedes"] for row in substitutions}
        authorities = {row["authority"] for row in substitutions}
        self.assertEqual(len(purposes), 10)
        self.assertEqual(len(superseded), 10)
        self.assertEqual(len(authorities), 10)
        self.assertTrue(all(value.endswith("_v2") for value in superseded))
        self.assertTrue(all(value.endswith("_v3") for value in authorities))
        for predecessor, successor in zip(
            prior["artifact_authority_namespace"]["substitutions"],
            substitutions,
            strict=True,
        ):
            self.assertEqual(successor["purpose"], predecessor["purpose"])
            self.assertEqual(successor["supersedes"], predecessor["authority"])

        self.assertEqual(
            namespace["matrix_decision_publication_supersedes"],
            prior["artifact_authority_namespace"]["matrix_decision_publication"],
        )
        self.assertEqual(
            namespace["matrix_decision_publication"],
            "phase4_confirmatory_matrix_decision_publication_v3",
        )
        self.assertNotIn(namespace["matrix_decision_publication"], authorities)

        effective_artifacts = {
            artifact
            for group in protocol.effective_cell_groups()
            for artifact in group["required_artifacts"]
        }
        self.assertTrue(superseded.isdisjoint(effective_artifacts))
        self.assertTrue(authorities.issubset(effective_artifacts))
        self.assertNotIn(namespace["matrix_decision_publication"], effective_artifacts)

    def test_campaign_and_protocol_state_preserve_history_without_opening_authority(
        self,
    ) -> None:
        document = protocol.read_protocol()
        self.assertEqual(
            document["campaign"],
            {
                "campaign_id": "phase4_confirmatory_corpus_v2",
                "configuration_authority": ("phase4_confirmatory_corpus_v2_h4096_session_v5"),
                "preserves_v1_negative_matrix": True,
                "preserves_v2_development_observations": True,
                "does_not_supersede_v1_decision": True,
            },
        )
        self.assertEqual(
            document["protocol_state"],
            {
                "protocol_frozen": True,
                "decision_not_evaluated": True,
                "heldout_outcomes_observed": False,
                "protocol_v2_development_outcomes_observed": True,
                "session_v5_h4096_allocation_outcomes_observed": False,
                "publishable_session_v5_h4096_evidence_created": False,
                "authority_only_supersession": True,
                "protocol_alone_authorizes_execution": False,
                "protocol_alone_authorizes_acquisition": False,
            },
        )

    def test_initial_cells_and_observation_firewall_are_exactly_closed(self) -> None:
        document = protocol.read_protocol()
        self.assertEqual(
            document["development_execution_plan"],
            {
                "protocol_freeze_includes_entrypoints": False,
                "separately_reviewed_session_v5_h4096_entrypoints_required": True,
                "initial_development_cells_are_complete_scope": True,
                "initial_development_cells": [
                    {
                        "case_id": 10100,
                        "requested_pool_size": 4,
                        "role": "exact",
                        "carrier": "same_run",
                        "raw_wire_schema_version": 2,
                        "raw_authority": "phase4_confirmatory_same_run_raw_evidence_v3",
                    },
                    {
                        "case_id": 10200,
                        "requested_pool_size": 8,
                        "role": "calibration",
                        "carrier": "ordinary",
                        "raw_wire_schema_version": 1,
                        "raw_authority": "phase4_confirmatory_raw_evidence_v3",
                    },
                ],
                "closed_execution_roles": [
                    "heldout",
                    "imported",
                    "fixed_query",
                    "stress",
                    "matrix",
                    "decision",
                ],
            },
        )
        self.assertEqual(
            document["observation_firewall"],
            {
                "development_case_ids": [
                    10100,
                    10101,
                    10102,
                    10200,
                    10201,
                    10210,
                    10211,
                    10220,
                    10221,
                ],
                "heldout_case_ids": (
                    list(range(11000, 11008))
                    + list(range(11100, 11108))
                    + list(range(11200, 11208))
                ),
                "heldout_execution_closed": True,
                "future_campaign_acquisition_authority_required": True,
                "future_authority_must_bind_clean_stamped_source_commit": True,
                "future_authority_must_bind_protocol_v3_and_roster_v4": True,
                "future_authority_must_bind_complete_execution_and_publication_chain": True,
                "future_publication_source_commit_must_equal_acquisition_commit": True,
                "protocol_and_roster_commit_alone_sufficient_for_heldout": False,
                "cross_configuration_evidence_relabeling_forbidden": True,
                "protocol_v2_evidence_cannot_satisfy_protocol_v3": True,
                "premature_session_v5_h4096_heldout_observation_invalidates_roster": True,
            },
        )

    def test_rechecksummed_ancestry_and_configuration_forgeries_are_rejected(
        self,
    ) -> None:
        self._assert_rechecksummed_mutations_rejected(
            [
                (
                    "superseded protocol schema",
                    lambda value: value["supersedes"].__setitem__("schema_version", 1),
                ),
                (
                    "superseded protocol checksum",
                    lambda value: value["supersedes"].__setitem__(
                        "artifact_checksum",
                        value["supersedes"]["artifact_checksum"] ^ 1,
                    ),
                ),
                (
                    "campaign configuration",
                    lambda value: value["campaign"].__setitem__(
                        "configuration_authority",
                        "phase4_confirmatory_corpus_v2_h4096",
                    ),
                ),
                (
                    "roster authority",
                    lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                        "authority",
                        "phase4_confirmatory_canonical_algorithm_budget_roster_v3",
                    ),
                ),
                (
                    "roster checksum",
                    lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                        "roster_checksum",
                        18429170436700418962,
                    ),
                ),
                (
                    "superseded roster authority",
                    lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                        "superseded_roster_authority",
                        "phase4_confirmatory_canonical_algorithm_budget_roster_v2",
                    ),
                ),
                (
                    "superseded configuration",
                    lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                        "superseded_configuration_authority",
                        "phase4_confirmatory_corpus_v2_h2250",
                    ),
                ),
                (
                    "Session v5 changed",
                    lambda value: value["canonical_algorithm_budget_authority"][
                        "configuration"
                    ].__setitem__("candidate_allocation_session_schema_version", 4),
                ),
                (
                    "Plan v3 changed",
                    lambda value: value["canonical_algorithm_budget_authority"][
                        "configuration"
                    ].__setitem__("targeted_regeneration_plan_schema_version", 2),
                ),
                (
                    "Execution v6 changed",
                    lambda value: value["canonical_algorithm_budget_authority"][
                        "configuration"
                    ].__setitem__("targeted_regeneration_execution_schema_version", 5),
                ),
                (
                    "candidate session equality relaxed",
                    lambda value: value["canonical_algorithm_budget_authority"][
                        "configuration"
                    ].__setitem__(
                        "all_candidate_session_fields_except_schema_version_unchanged",
                        False,
                    ),
                ),
            ]
        )

    def test_rechecksummed_namespace_matrix_state_scope_and_firewall_forgeries_are_rejected(
        self,
    ) -> None:
        self._assert_rechecksummed_mutations_rejected(
            [
                (
                    "v2 authority retained",
                    lambda value: value["artifact_authority_namespace"]["substitutions"][
                        0
                    ].__setitem__("authority", "phase4_confirmatory_raw_evidence_v2"),
                ),
                (
                    "wrong predecessor authority",
                    lambda value: value["artifact_authority_namespace"]["substitutions"][
                        0
                    ].__setitem__("supersedes", "phase4_confirmatory_raw_evidence_v1"),
                ),
                (
                    "substitution purpose changed",
                    lambda value: value["artifact_authority_namespace"]["substitutions"][
                        0
                    ].__setitem__("purpose", "raw"),
                ),
                (
                    "matrix predecessor changed",
                    lambda value: value["artifact_authority_namespace"].__setitem__(
                        "matrix_decision_publication_supersedes",
                        "phase4_confirmatory_matrix_decision_publication_v1",
                    ),
                ),
                (
                    "matrix count changed",
                    lambda value: value["matrix_authority_counts"].__setitem__(
                        "logical_cell_count",
                        103,
                    ),
                ),
                (
                    "historical observations erased",
                    lambda value: value["protocol_state"].__setitem__(
                        "protocol_v2_development_outcomes_observed",
                        False,
                    ),
                ),
                (
                    "Session-v5 outcome claimed",
                    lambda value: value["protocol_state"].__setitem__(
                        "session_v5_h4096_allocation_outcomes_observed",
                        True,
                    ),
                ),
                (
                    "publishable evidence claimed",
                    lambda value: value["protocol_state"].__setitem__(
                        "publishable_session_v5_h4096_evidence_created",
                        True,
                    ),
                ),
                (
                    "execution authorized",
                    lambda value: value["protocol_state"].__setitem__(
                        "protocol_alone_authorizes_execution",
                        True,
                    ),
                ),
                (
                    "exact pool changed",
                    lambda value: value["development_execution_plan"]["initial_development_cells"][
                        0
                    ].__setitem__("requested_pool_size", 8),
                ),
                (
                    "heldout execution opened",
                    lambda value: value["observation_firewall"].__setitem__(
                        "heldout_execution_closed",
                        False,
                    ),
                ),
                (
                    "v2 evidence promotion allowed",
                    lambda value: value["observation_firewall"].__setitem__(
                        "protocol_v2_evidence_cannot_satisfy_protocol_v3",
                        False,
                    ),
                ),
                (
                    "heldout roster changed",
                    lambda value: value["observation_firewall"]["heldout_case_ids"].append(11208),
                ),
            ]
        )

    def test_protocol_versions_cross_reject(self) -> None:
        with self.assertRaises(protocol_v1.ConfirmatoryProtocolError):
            protocol_v1.validate_document(protocol.expected_protocol())
        with self.assertRaises(protocol_v2.ConfirmatoryProtocolV2Error):
            protocol_v2.validate_document(protocol.expected_protocol())
        with self.assertRaises(protocol.ConfirmatoryProtocolV3Error):
            protocol.validate_document(protocol_v1.expected_protocol())
        with self.assertRaises(protocol.ConfirmatoryProtocolV3Error):
            protocol.validate_document(protocol_v2.expected_protocol())

    def test_canonical_bytes_types_duplicates_nonfinite_and_bounds_are_strict(
        self,
    ) -> None:
        expected = protocol.expected_protocol()
        canonical = protocol._canonical(expected)
        malformed = (
            (canonical, "end in one LF"),
            (canonical + "\n\n", "end in one LF"),
            ("\ufeff" + canonical + "\n", "without BOM"),
            (json.dumps(expected, indent=2) + "\n", "canonical compact"),
            (
                canonical.replace(
                    '{"schema_version":3,',
                    '{"schema_version":3,"schema_version":3,',
                    1,
                )
                + "\n",
                "duplicate JSON key",
            ),
            (
                canonical.replace('"schema_version":3', '"schema_version":NaN', 1) + "\n",
                "non-finite JSON number",
            ),
        )
        for text, pattern in malformed:
            with self.subTest(pattern=pattern):
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV3Error,
                    pattern,
                ):
                    protocol.read_protocol(self._write_text(text))

        for field, alias in (
            ("schema_version", True),
            ("artifact_checksum", False),
        ):
            with self.subTest(field=field):
                bool_alias = copy.deepcopy(expected)
                bool_alias[field] = alias
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV3Error,
                    "exactly reconstruct",
                ):
                    protocol.validate_document(bool_alias)

        nested_bool_alias = copy.deepcopy(expected)
        nested_bool_alias["canonical_algorithm_budget_authority"]["configuration"][
            "history_step_per_overuse_unit"
        ] = True
        self._rechecksum(nested_bool_alias)
        with self.assertRaisesRegex(
            protocol.ConfirmatoryProtocolV3Error,
            "exactly reconstruct",
        ):
            protocol.validate_document(nested_bool_alias)

        reordered = copy.deepcopy(expected)
        campaign = reordered["campaign"]
        campaign_id = campaign.pop("campaign_id")
        campaign["campaign_id"] = campaign_id
        self._rechecksum(reordered)
        with self.assertRaisesRegex(
            protocol.ConfirmatoryProtocolV3Error,
            "exactly reconstruct",
        ):
            protocol.validate_document(reordered)

        unknown = copy.deepcopy(expected)
        unknown["unknown"] = False
        self._rechecksum(unknown)
        with self.assertRaisesRegex(
            protocol.ConfirmatoryProtocolV3Error,
            "exactly reconstruct",
        ):
            protocol.validate_document(unknown)

        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "large.json"
            path.write_bytes(b" " * (protocol._MAX_BYTES + 1))
            with self.assertRaisesRegex(
                protocol.ConfirmatoryProtocolV3Error,
                "64 KiB",
            ):
                protocol.read_protocol(path)


if __name__ == "__main__":
    unittest.main()
