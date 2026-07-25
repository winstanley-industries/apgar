from __future__ import annotations

import copy
import json
import pathlib
import tempfile
import unittest

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
from tools import validate_phase4_confirmatory_decision_protocol as protocol_v1
from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol


class Phase4ConfirmatoryDecisionProtocolV2Test(unittest.TestCase):
    def _write(self, value: object) -> pathlib.Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = pathlib.Path(directory.name) / "protocol.json"
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

    def _rechecksum(self, value: dict[str, object]) -> dict[str, object]:
        value["artifact_checksum"] = protocol._artifact_checksum(value)
        self.assertEqual(value["artifact_checksum"], protocol._artifact_checksum(value))
        return value

    def test_checked_in_protocol_is_exact_and_binds_v1_and_roster_v3(self) -> None:
        prior = protocol_v1.read_protocol()
        roster = budget_v3.validate_roster()
        document = protocol.read_protocol()

        self.assertEqual(document, protocol.expected_protocol())
        self.assertEqual(document["artifact_checksum"], 11520586171987743043)
        self.assertEqual(document["artifact_checksum"], protocol._artifact_checksum(document))
        self.assertNotEqual(document["artifact_checksum"], prior["artifact_checksum"])
        self.assertEqual(
            document["supersedes"],
            {
                "schema_version": 1,
                "artifact_checksum": 7747512371013753061,
            },
        )
        self.assertEqual(document["supersedes"]["schema_version"], prior["schema_version"])
        self.assertEqual(
            document["supersedes"]["artifact_checksum"],
            prior["artifact_checksum"],
        )

        authority = document["canonical_algorithm_budget_authority"]
        self.assertEqual(
            authority,
            {
                "authority": "phase4_confirmatory_canonical_algorithm_budget_roster_v3",
                "schema_version": 3,
                "corpus_version": 2,
                "corpus_checksum": 4182833841936446798,
                "representative_manifest_schema_version": 2,
                "representative_manifest_checksum": 9613362670139358355,
                "workload_roster_manifest_schema_version": 2,
                "workload_roster_manifest_checksum": 14986327048461036142,
                "superseded_roster_schema_version": 2,
                "superseded_roster_checksum": 15913985307894145139,
                "configuration_authority": "phase4_confirmatory_corpus_v2_h4096",
                "configuration": {
                    "present_step_per_overuse_unit": 1,
                    "superseded_history_step_per_overuse_unit": 2250,
                    "history_step_per_overuse_unit": 4096,
                    "baseline_and_candidate_price_configs_equal": True,
                    "all_other_canonical_algorithm_budget_fields_unchanged": True,
                    "query_work_and_external_opportunity_unchanged": True,
                },
                "cell_count": 102,
                "roster_checksum": 18429170436700418962,
            },
        )
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
            authority["superseded_roster_checksum"],
            roster["supersedes"]["roster_checksum"],
        )

    def test_matrix_and_decision_rules_equal_v1_after_namespace_normalization(
        self,
    ) -> None:
        document = protocol.read_protocol()
        self.assertEqual(protocol.expanded_cells(), protocol_v1.expanded_cells())
        self.assertEqual(len(protocol.expanded_cells()), 104)
        self.assertEqual(len(set(protocol.expanded_cells())), 104)
        self.assertEqual(
            protocol.effective_decision_sections(),
            protocol_v1.effective_decision_sections(),
        )

        replacements = {
            authority: supersedes for _, supersedes, authority in protocol._ARTIFACT_SUBSTITUTIONS
        }
        normalized_groups = copy.deepcopy(protocol.effective_cell_groups())
        for group in normalized_groups:
            group["required_artifacts"] = [
                replacements.get(artifact, artifact) for artifact in group["required_artifacts"]
            ]
        self.assertEqual(normalized_groups, protocol_v1._cell_groups())

        cells = protocol.expanded_cells()
        counts = document["matrix_authority_counts"]
        self.assertEqual(
            counts,
            {
                "logical_cell_count": 104,
                "confirmatory_raw_success_cell_count": 100,
                "noncalibration_closure_cell_count": 86,
                "noncalibration_confirmatory_raw_success_cell_count": 82,
                "ordinary_raw_success_cell_count": 22,
                "same_run_raw_success_cell_count": 78,
                "same_run_guardrail_cell_count": 78,
            },
        )
        self.assertEqual(
            sum(disposition == "confirmatory_raw_success" for *_, disposition in cells),
            100,
        )
        self.assertEqual(sum(role != "calibration" for _, _, role, _ in cells), 86)

    def test_artifact_namespace_is_exact_unique_and_has_no_effective_v1_alias(
        self,
    ) -> None:
        namespace = protocol.read_protocol()["artifact_authority_namespace"]
        expected_substitutions = [
            {
                "purpose": "ordinary_raw_outcome_and_timing",
                "supersedes": "phase4_confirmatory_raw_evidence_v1",
                "authority": "phase4_confirmatory_raw_evidence_v2",
            },
            {
                "purpose": "same_run_raw_outcome_and_timing",
                "supersedes": "phase4_confirmatory_same_run_raw_evidence_v1",
                "authority": "phase4_confirmatory_same_run_raw_evidence_v2",
            },
            {
                "purpose": "same_run_exact_rejection_guardrail",
                "supersedes": "phase4_confirmatory_same_run_decision_telemetry_v1",
                "authority": "phase4_confirmatory_same_run_decision_telemetry_v2",
            },
            {
                "purpose": "ordinary_per_net_diagnostic_join",
                "supersedes": "phase4_confirmatory_per_net_report_publication_join_v1",
                "authority": "phase4_confirmatory_per_net_report_publication_join_v2",
            },
            {
                "purpose": "same_run_per_net_diagnostic_join",
                "supersedes": ("phase4_confirmatory_same_run_per_net_report_publication_join_v1"),
                "authority": ("phase4_confirmatory_same_run_per_net_report_publication_join_v2"),
            },
            {
                "purpose": "ordinary_operational_measurement",
                "supersedes": ("phase4_confirmatory_operational_measurement_publication_v1"),
                "authority": ("phase4_confirmatory_operational_measurement_publication_v2"),
            },
            {
                "purpose": "same_run_operational_measurement",
                "supersedes": (
                    "phase4_confirmatory_same_run_operational_measurement_publication_v1"
                ),
                "authority": (
                    "phase4_confirmatory_same_run_operational_measurement_publication_v2"
                ),
            },
            {
                "purpose": "exact_small_oracle",
                "supersedes": "phase4_confirmatory_exact_small_oracle_v1",
                "authority": "phase4_confirmatory_exact_small_oracle_v2",
            },
            {
                "purpose": "fixed_query_control",
                "supersedes": "phase4_confirmatory_fixed_query_control_v1",
                "authority": "phase4_confirmatory_fixed_query_control_v2",
            },
            {
                "purpose": "stress_evidence",
                "supersedes": "phase4_confirmatory_stress_evidence_v1",
                "authority": "phase4_confirmatory_stress_evidence_v2",
            },
        ]
        self.assertEqual(namespace["substitutions"], expected_substitutions)
        self.assertEqual(len(expected_substitutions), 10)
        purposes = {row["purpose"] for row in expected_substitutions}
        superseded = {row["supersedes"] for row in expected_substitutions}
        authorities = {row["authority"] for row in expected_substitutions}
        self.assertEqual(len(purposes), 10)
        self.assertEqual(len(superseded), 10)
        self.assertEqual(len(authorities), 10)
        self.assertTrue(all(value.endswith("_v1") for value in superseded))
        self.assertTrue(all(value.endswith("_v2") for value in authorities))
        self.assertEqual(
            namespace["matrix_decision_publication"],
            "phase4_confirmatory_matrix_decision_publication_v2",
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

    def test_initial_cells_and_observation_firewall_are_exactly_closed(self) -> None:
        document = protocol.read_protocol()
        self.assertEqual(
            document["protocol_state"],
            {
                "protocol_frozen": True,
                "decision_not_evaluated": True,
                "heldout_outcomes_observed": False,
                "h4096_allocation_outcomes_observed": False,
                "publishable_h4096_evidence_created": False,
                "authority_only_supersession": True,
                "protocol_alone_authorizes_execution": False,
                "protocol_alone_authorizes_acquisition": False,
            },
        )
        self.assertEqual(
            document["development_execution_plan"],
            {
                "protocol_freeze_includes_entrypoints": False,
                "separately_reviewed_h4096_entrypoints_required": True,
                "initial_development_cells_are_complete_scope": True,
                "initial_development_cells": [
                    {
                        "case_id": 10100,
                        "requested_pool_size": 4,
                        "role": "exact",
                        "carrier": "same_run",
                        "raw_wire_schema_version": 2,
                        "raw_authority": "phase4_confirmatory_same_run_raw_evidence_v2",
                    },
                    {
                        "case_id": 10200,
                        "requested_pool_size": 8,
                        "role": "calibration",
                        "carrier": "ordinary",
                        "raw_wire_schema_version": 1,
                        "raw_authority": "phase4_confirmatory_raw_evidence_v2",
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
                "future_authority_must_bind_protocol_v2_and_roster_v3": True,
                "future_authority_must_bind_complete_execution_and_publication_chain": True,
                "future_publication_source_commit_must_equal_acquisition_commit": True,
                "protocol_and_roster_commit_alone_sufficient_for_heldout": False,
                "cross_configuration_evidence_relabeling_forbidden": True,
                "premature_heldout_observation_invalidates_roster": True,
            },
        )

    def test_rechecksummed_ancestry_budget_and_authority_mutations_are_rejected(
        self,
    ) -> None:
        mutations = [
            (
                "superseded protocol schema",
                lambda value: value["supersedes"].__setitem__("schema_version", 2),
            ),
            (
                "superseded protocol checksum",
                lambda value: value["supersedes"].__setitem__(
                    "artifact_checksum",
                    value["supersedes"]["artifact_checksum"] ^ 1,
                ),
            ),
            (
                "roster authority",
                lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                    "authority",
                    "phase4_confirmatory_canonical_algorithm_budget_roster_v2",
                ),
            ),
            (
                "configuration authority",
                lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                    "configuration_authority",
                    "phase4_confirmatory_corpus_v2",
                ),
            ),
            (
                "H=4096 history price",
                lambda value: value["canonical_algorithm_budget_authority"][
                    "configuration"
                ].__setitem__("history_step_per_overuse_unit", 2250),
            ),
            (
                "equal-arm configuration",
                lambda value: value["canonical_algorithm_budget_authority"][
                    "configuration"
                ].__setitem__("baseline_and_candidate_price_configs_equal", False),
            ),
            (
                "roster checksum",
                lambda value: value["canonical_algorithm_budget_authority"].__setitem__(
                    "roster_checksum",
                    15913985307894145139,
                ),
            ),
        ]
        for label, mutate in mutations:
            with self.subTest(label=label):
                value = copy.deepcopy(protocol.expected_protocol())
                mutate(value)
                self._rechecksum(value)
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV2Error,
                    "exactly reconstruct",
                ):
                    protocol.validate_document(value)

    def test_rechecksummed_namespace_scope_and_firewall_mutations_are_rejected(
        self,
    ) -> None:
        mutations = [
            (
                "v1 artifact retained",
                lambda value: value["artifact_authority_namespace"]["substitutions"][0].__setitem__(
                    "authority",
                    "phase4_confirmatory_raw_evidence_v1",
                ),
            ),
            (
                "matrix namespace changed",
                lambda value: value["artifact_authority_namespace"].__setitem__(
                    "matrix_decision_publication",
                    "phase4_confirmatory_matrix_decision_publication_v1",
                ),
            ),
            (
                "initial exact pool changed",
                lambda value: value["development_execution_plan"]["initial_development_cells"][
                    0
                ].__setitem__("requested_pool_size", 8),
            ),
            (
                "initial calibration pool changed",
                lambda value: value["development_execution_plan"]["initial_development_cells"][
                    1
                ].__setitem__("requested_pool_size", 4),
            ),
            (
                "heldout cell added",
                lambda value: value["development_execution_plan"][
                    "initial_development_cells"
                ].append(
                    {
                        "case_id": 11000,
                        "requested_pool_size": 4,
                        "role": "heldout",
                        "carrier": "same_run",
                        "raw_wire_schema_version": 2,
                        "raw_authority": ("phase4_confirmatory_same_run_raw_evidence_v2"),
                    }
                ),
            ),
            (
                "heldout execution opened",
                lambda value: value["observation_firewall"].__setitem__(
                    "heldout_execution_closed",
                    False,
                ),
            ),
            (
                "publication source lock relaxed",
                lambda value: value["observation_firewall"].__setitem__(
                    "future_publication_source_commit_must_equal_acquisition_commit",
                    False,
                ),
            ),
            (
                "cross-configuration relabeling allowed",
                lambda value: value["observation_firewall"].__setitem__(
                    "cross_configuration_evidence_relabeling_forbidden",
                    False,
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
                "acquisition authorized",
                lambda value: value["protocol_state"].__setitem__(
                    "protocol_alone_authorizes_acquisition",
                    True,
                ),
            ),
            (
                "heldout outcomes observed",
                lambda value: value["protocol_state"].__setitem__(
                    "heldout_outcomes_observed",
                    True,
                ),
            ),
            (
                "H=4096 outcomes observed",
                lambda value: value["protocol_state"].__setitem__(
                    "h4096_allocation_outcomes_observed",
                    True,
                ),
            ),
            (
                "publishable H=4096 evidence created",
                lambda value: value["protocol_state"].__setitem__(
                    "publishable_h4096_evidence_created",
                    True,
                ),
            ),
        ]
        for label, mutate in mutations:
            with self.subTest(label=label):
                value = copy.deepcopy(protocol.expected_protocol())
                mutate(value)
                self._rechecksum(value)
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV2Error,
                    "exactly reconstruct",
                ):
                    protocol.validate_document(value)

    def test_protocol_versions_cross_reject(self) -> None:
        with self.assertRaises(protocol_v1.ConfirmatoryProtocolError):
            protocol_v1.validate_document(protocol.expected_protocol())
        with self.assertRaises(protocol.ConfirmatoryProtocolV2Error):
            protocol.validate_document(protocol_v1.expected_protocol())

    def test_canonical_bytes_duplicate_keys_nonfinite_types_and_bounds_are_strict(
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
                    '{"schema_version":2,',
                    '{"schema_version":2,"schema_version":2,',
                    1,
                )
                + "\n",
                "duplicate JSON key",
            ),
            (
                canonical.replace('"schema_version":2', '"schema_version":NaN', 1) + "\n",
                "non-finite JSON number",
            ),
        )
        for text, pattern in malformed:
            with self.subTest(pattern=pattern):
                path = self._write({})
                path.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV2Error,
                    pattern,
                ):
                    protocol.read_protocol(path)

        for field, alias in (
            ("schema_version", True),
            ("artifact_checksum", False),
        ):
            with self.subTest(field=field):
                bool_alias = copy.deepcopy(expected)
                bool_alias[field] = alias
                with self.assertRaisesRegex(
                    protocol.ConfirmatoryProtocolV2Error,
                    "exactly reconstruct",
                ):
                    protocol.validate_document(bool_alias)

        nested_bool_alias = copy.deepcopy(expected)
        nested_bool_alias["canonical_algorithm_budget_authority"]["configuration"][
            "history_step_per_overuse_unit"
        ] = True
        self._rechecksum(nested_bool_alias)
        with self.assertRaisesRegex(
            protocol.ConfirmatoryProtocolV2Error,
            "exactly reconstruct",
        ):
            protocol.validate_document(nested_bool_alias)

        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "large.json"
            path.write_bytes(b" " * (protocol._MAX_BYTES + 1))
            with self.assertRaisesRegex(
                protocol.ConfirmatoryProtocolV2Error,
                "64 KiB",
            ):
                protocol.read_protocol(path)


if __name__ == "__main__":
    unittest.main()
