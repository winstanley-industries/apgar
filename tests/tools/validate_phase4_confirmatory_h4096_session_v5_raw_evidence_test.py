"""Pure synthetic tests for Session-v5 H=4096 Raw and telemetry validators."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from collections.abc import Callable

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_h4096_raw_evidence as predecessor_raw
from tools import (
    validate_phase4_confirmatory_h4096_same_run_decision_telemetry as predecessor_telemetry,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_raw_evidence as v5_raw,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_same_run_decision_telemetry as v5_telemetry,
)
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_EXACT_CANONICAL_BUDGET = 13645569624513409309
_CALIBRATION_CANONICAL_BUDGET = 7657176792159702821
_EXACT_PAIRED_BUDGET = 12493092620111240227
_CALIBRATION_PAIRED_BUDGET = 13340538727848385478
_PREDECESSOR_EXACT_PAIRED_BUDGET = 5851813264366095594
_PREDECESSOR_CALIBRATION_PAIRED_BUDGET = 12108149041077564710
_SOURCE_COMMIT = "a" * 40


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


def _paired_budgets(raw: dict[str, object]) -> set[int]:
    return {
        arm["record"]["semantics"]["budget_checksum"]
        for attempt in raw["attempts"]
        for arm in (attempt["baseline"], attempt["candidate"])
    }


def _rehash_with_paired_budget(raw: dict[str, object], budget_checksum: int) -> None:
    """Make a self-consistent Raw forgery carrying a foreign paired budget."""
    for pair in raw["attempts"]:
        for name in ("baseline", "candidate"):
            arm = pair[name]
            record = arm["record"]
            semantics = record["semantics"]
            semantics["budget_checksum"] = budget_checksum
            semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
            observation = record["external_observation"]
            observation["associated_semantic_checksum"] = semantics["semantic_checksum"]
            observation["authority_checksum"] = raw_validator.compute_authority_checksum(
                observation
            )
            record["artifact_checksum"] = raw_validator.compute_record_checksum(record)
            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        result = pair["result"]
        result["baseline"] = copy.deepcopy(pair["baseline"]["record"])
        result["candidate"] = copy.deepcopy(pair["candidate"]["record"])
        result["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(result)
        result["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(result)
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _rehash_with_noncanonical_external_budget(
    raw: dict[str, object],
    canonical_algorithm_budget: int,
) -> None:
    """Make Raw internally consistent under a noncanonical paired preimage."""
    external_budget = raw["config"]["external_budget"]
    external_budget["maximum_prepared_elapsed_nanoseconds"] -= 1
    _, cases, _ = raw_validator._frozen_confirmatory_representative_manifest()
    paired_budget = raw_validator.compute_canonical_budget_checksum(
        raw,
        cases[raw["config"]["case_id"]],
        canonical_algorithm_budget,
        corpus_version=2,
    )
    for pair in raw["attempts"]:
        for name in ("baseline", "candidate"):
            arm = pair[name]
            record = arm["record"]
            semantics = record["semantics"]
            semantics["external_budget"] = copy.deepcopy(external_budget)
            semantics["budget_checksum"] = paired_budget
            semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
            observation = record["external_observation"]
            observation["associated_semantic_checksum"] = semantics["semantic_checksum"]
            observation["authority_checksum"] = raw_validator.compute_authority_checksum(
                observation
            )
            record["artifact_checksum"] = raw_validator.compute_record_checksum(record)
            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        result = pair["result"]
        result["baseline"] = copy.deepcopy(pair["baseline"]["record"])
        result["candidate"] = copy.deepcopy(pair["candidate"]["record"])
        result["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(result)
        result["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(result)
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    raw["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
        raw,
        corpus_version=2,
    )
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _rehash_with_noncanonical_setup_deadline(raw: dict[str, object]) -> None:
    """Make a self-consistent Raw forgery carrying a noncanonical setup cap."""
    raw["config"]["maximum_setup_elapsed_nanoseconds"] -= 1
    raw["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
        raw,
        corpus_version=2,
    )
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _make_all_failure_same_run(raw: dict[str, object]) -> None:
    """Turn one valid synthetic Raw cell into a checksummed all-failure attempt."""
    pair = raw["attempts"][0]
    for name in ("baseline", "candidate"):
        arm = pair[name]
        failed = name == "baseline"
        arm.update(
            {
                "disposition": 7 if failed else 10,
                "dispatch_ordinal": 0,
                "process_instance_identity": 0,
                "outer_elapsed_nanoseconds": 0,
                "process_lifetime_peak_host_bytes": 0,
                "raw_wait_status": 0,
                "process_exit_code": -1,
                "terminating_signal": 0,
                "watchdog_kill_sent": False,
                "controller_invariant_id": "P4HARNESS-LAUNCH-001" if failed else "",
                "controller_detail": "synthetic pre-dispatch launch failure" if failed else "",
                "record": None,
                "child_failure": None,
            }
        )
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
    pair["result"] = None
    pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


class Phase4ConfirmatoryH4096SessionV5RawEvidenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.ordinary = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        self.exact = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        self.sidecar = artifacts.make_sidecar(self.exact)

    def validate_ordinary(self, value: object) -> None:
        v5_raw.validate_confirmatory_h4096_session_v5_document(
            value,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )

    def validate_exact_raw(self, value: object) -> None:
        v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2(
            value,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )

    def validate_exact(self, raw: object, sidecar: object) -> None:
        v5_telemetry.validate_confirmatory_h4096_session_v5_join(
            raw,
            sidecar,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )

    def test_accepts_only_the_two_protocol_v3_development_authorities(self) -> None:
        self.validate_ordinary(self.ordinary)
        self.validate_exact_raw(self.exact)
        self.validate_exact(self.exact, self.sidecar)
        self.assertTrue(telemetry_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertEqual(
            v5_raw.confirmatory_h4096_session_v5_initial_cells(same_run=False),
            frozenset({(10200, 8)}),
        )
        self.assertEqual(
            v5_raw.confirmatory_h4096_session_v5_initial_cells(same_run=True),
            frozenset({(10100, 4)}),
        )

    def test_canonical_and_paired_budget_identities_are_independent(self) -> None:
        _, cases, budgets = v5_raw._authority_provider(2)
        self.assertEqual(budgets[(10100, 4)], _EXACT_CANONICAL_BUDGET)
        self.assertEqual(budgets[(10200, 8)], _CALIBRATION_CANONICAL_BUDGET)
        self.assertEqual(_paired_budgets(self.exact), {_EXACT_PAIRED_BUDGET})
        self.assertEqual(_paired_budgets(self.ordinary), {_CALIBRATION_PAIRED_BUDGET})
        self.assertEqual(
            raw_validator.compute_canonical_budget_checksum(
                self.exact,
                cases[10100],
                budgets[(10100, 4)],
                corpus_version=2,
            ),
            _EXACT_PAIRED_BUDGET,
        )
        self.assertEqual(
            raw_validator.compute_canonical_budget_checksum(
                self.ordinary,
                cases[10200],
                budgets[(10200, 8)],
                corpus_version=2,
            ),
            _CALIBRATION_PAIRED_BUDGET,
        )
        self.assertNotIn(_EXACT_CANONICAL_BUDGET, _paired_budgets(self.exact))
        self.assertNotIn(_CALIBRATION_CANONICAL_BUDGET, _paired_budgets(self.ordinary))

        predecessor_exact = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
        )
        predecessor_ordinary = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
        )
        self.assertEqual(_paired_budgets(predecessor_exact), {_PREDECESSOR_EXACT_PAIRED_BUDGET})
        self.assertEqual(
            _paired_budgets(predecessor_ordinary),
            {_PREDECESSOR_CALIBRATION_PAIRED_BUDGET},
        )

    def test_v3_v2_and_h2250_authorities_cross_reject_bidirectionally(self) -> None:
        predecessor_ordinary = artifacts.make_raw(
            10200, 8, same_run=False, h4096=True, repetitions=1
        )
        h2250_ordinary = artifacts.make_raw(10200, 8, same_run=False, h4096=False, repetitions=1)
        ordinary_authorities: tuple[tuple[dict[str, object], Callable[[object], None]], ...] = (
            (
                h2250_ordinary,
                lambda value: raw_validator.validate_confirmatory_document(
                    value,
                    allow_unstamped=True,
                    expected_repetitions=1,
                    expected_workers=4,
                ),
            ),
            (
                predecessor_ordinary,
                lambda value: predecessor_raw.validate_confirmatory_h4096_document(
                    value,
                    allow_unstamped=True,
                    expected_repetitions=1,
                    expected_workers=4,
                ),
            ),
            (self.ordinary, self.validate_ordinary),
        )
        for artifact_index, (artifact, _) in enumerate(ordinary_authorities):
            for validator_index, (_, validate) in enumerate(ordinary_authorities):
                with self.subTest(
                    carrier="ordinary",
                    artifact=artifact_index,
                    validator=validator_index,
                ):
                    if artifact_index == validator_index:
                        validate(artifact)
                    else:
                        with self.assertRaises(raw_validator.EvidenceError):
                            validate(artifact)

        predecessor_exact = artifacts.make_raw(10100, 4, same_run=True, h4096=True, repetitions=1)
        predecessor_sidecar = artifacts.make_sidecar(predecessor_exact)
        h2250_exact = artifacts.make_raw(10100, 4, same_run=True, h4096=False, repetitions=1)
        h2250_sidecar = artifacts.make_sidecar(h2250_exact)
        exact_authorities: tuple[
            tuple[dict[str, object], dict[str, object], Callable[[object, object], object]],
            ...,
        ] = (
            (
                h2250_exact,
                h2250_sidecar,
                lambda raw, sidecar: telemetry_validator.validate_confirmatory_join(
                    raw,
                    sidecar,
                    allow_unstamped=True,
                    expected_repetitions=1,
                    expected_workers=4,
                ),
            ),
            (
                predecessor_exact,
                predecessor_sidecar,
                lambda raw, sidecar: predecessor_telemetry.validate_confirmatory_h4096_join(
                    raw,
                    sidecar,
                    allow_unstamped=True,
                    expected_repetitions=1,
                    expected_workers=4,
                ),
            ),
            (self.exact, self.sidecar, self.validate_exact),
        )
        for artifact_index, (raw, sidecar, _) in enumerate(exact_authorities):
            for validator_index, (_, _, validate) in enumerate(exact_authorities):
                with self.subTest(
                    carrier="same_run",
                    artifact=artifact_index,
                    validator=validator_index,
                ):
                    if artifact_index == validator_index:
                        validate(raw, sidecar)
                    else:
                        with self.assertRaises(raw_validator.EvidenceError):
                            validate(raw, sidecar)

    def test_wrong_carrier_and_broader_development_scope_are_closed(self) -> None:
        wrong_carrier_exact = artifacts.make_raw(
            10100,
            4,
            same_run=False,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "ordinary Raw"):
            self.validate_ordinary(wrong_carrier_exact)

        wrong_carrier_calibration = artifacts.make_raw(
            10200,
            8,
            same_run=True,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "same-run Raw"):
            self.validate_exact_raw(wrong_carrier_calibration)

        other_exact = artifacts.make_raw(
            10101,
            4,
            same_run=True,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "same-run Raw"):
            self.validate_exact_raw(other_exact)

        other_calibration = artifacts.make_raw(
            10201,
            8,
            same_run=False,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "ordinary Raw"):
            self.validate_ordinary(other_calibration)

    def test_rehashed_foreign_budget_and_telemetry_associations_are_rejected(self) -> None:
        for artifact, foreign_budgets, validate in (
            (
                self.exact,
                (_PREDECESSOR_EXACT_PAIRED_BUDGET, _EXACT_CANONICAL_BUDGET),
                self.validate_exact_raw,
            ),
            (
                self.ordinary,
                (_PREDECESSOR_CALIBRATION_PAIRED_BUDGET, _CALIBRATION_CANONICAL_BUDGET),
                self.validate_ordinary,
            ),
        ):
            for foreign_budget in foreign_budgets:
                forged = copy.deepcopy(artifact)
                _rehash_with_paired_budget(forged, foreign_budget)
                with self.subTest(
                    case_id=forged["config"]["case_id"],
                    foreign_budget=foreign_budget,
                ):
                    with self.assertRaisesRegex(
                        raw_validator.EvidenceError, "another command or cell"
                    ):
                        validate(forged)

        foreign_sidecar = copy.deepcopy(self.sidecar)
        arm = foreign_sidecar["attempts"][0]["candidate"]
        arm["associated_semantic_checksum"] ^= 1
        arm["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(arm)
        pair = foreign_sidecar["attempts"][0]
        pair["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(pair)
        foreign_sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            self.validate_exact(self.exact, foreign_sidecar)

    def test_self_consistent_noncanonical_paired_preimage_is_rejected(self) -> None:
        for artifact, canonical_budget, validate in (
            (self.exact, _EXACT_CANONICAL_BUDGET, self.validate_exact_raw),
            (self.ordinary, _CALIBRATION_CANONICAL_BUDGET, self.validate_ordinary),
        ):
            forged = copy.deepcopy(artifact)
            _rehash_with_noncanonical_external_budget(forged, canonical_budget)
            with self.subTest(case_id=forged["config"]["case_id"]):
                with self.assertRaisesRegex(
                    raw_validator.EvidenceError,
                    "paired semantic budget differs",
                ):
                    validate(forged)

    def test_complete_canonical_setup_deadline_is_required_by_both_carriers(self) -> None:
        forged_ordinary = copy.deepcopy(self.ordinary)
        _rehash_with_noncanonical_setup_deadline(forged_ordinary)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "setup deadline differs"):
            self.validate_ordinary(forged_ordinary)

        forged_exact = copy.deepcopy(self.exact)
        _rehash_with_noncanonical_setup_deadline(forged_exact)
        forged_sidecar = artifacts.make_sidecar(forged_exact)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "setup deadline differs"):
            self.validate_exact_raw(forged_exact)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "setup deadline differs"):
            self.validate_exact(forged_exact, forged_sidecar)

    def test_publication_cardinality_overrides_require_testing_mode(self) -> None:
        clean_ordinary = copy.deepcopy(self.ordinary)
        artifacts.mark_raw_clean(clean_ordinary, _SOURCE_COMMIT)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "testing cardinality overrides"):
            v5_raw.validate_confirmatory_h4096_session_v5_document(
                clean_ordinary,
                expected_commit=_SOURCE_COMMIT,
                expected_repetitions=1,
            )

        clean_exact = copy.deepcopy(self.exact)
        artifacts.mark_raw_clean(clean_exact, _SOURCE_COMMIT)
        for validate in (
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2,
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2,
        ):
            with self.subTest(cardinality="repetitions", validator=validate.__name__):
                with self.assertRaisesRegex(
                    raw_validator.EvidenceError,
                    "testing cardinality overrides",
                ):
                    validate(
                        clean_exact,
                        expected_commit=_SOURCE_COMMIT,
                        expected_repetitions=1,
                    )

        clean_ordinary_workers = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            workers=3,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(clean_ordinary_workers, _SOURCE_COMMIT)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "testing cardinality overrides"):
            v5_raw.validate_confirmatory_h4096_session_v5_document(
                clean_ordinary_workers,
                expected_commit=_SOURCE_COMMIT,
                expected_workers=3,
            )

        clean_exact_workers = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            workers=3,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(clean_exact_workers, _SOURCE_COMMIT)
        for validate in (
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_document_v2,
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2,
        ):
            with self.subTest(cardinality="workers", validator=validate.__name__):
                with self.assertRaisesRegex(
                    raw_validator.EvidenceError,
                    "testing cardinality overrides",
                ):
                    validate(
                        clean_exact_workers,
                        expected_commit=_SOURCE_COMMIT,
                        expected_workers=3,
                    )

    def test_expected_clean_commit_is_independently_required(self) -> None:
        clean_ordinary = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(clean_ordinary, _SOURCE_COMMIT)
        v5_raw.validate_confirmatory_h4096_session_v5_document(
            clean_ordinary,
            expected_commit=_SOURCE_COMMIT,
            expected_workers=4,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "independently supplied commit"):
            v5_raw.validate_confirmatory_h4096_session_v5_document(
                clean_ordinary,
                expected_commit="b" * 40,
                expected_workers=4,
            )

        clean_exact = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(clean_exact, _SOURCE_COMMIT)
        clean_sidecar = artifacts.make_sidecar(clean_exact)
        v5_telemetry.validate_confirmatory_h4096_session_v5_join(
            clean_exact,
            clean_sidecar,
            expected_commit=_SOURCE_COMMIT,
            expected_workers=4,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "independently supplied commit"):
            v5_telemetry.validate_confirmatory_h4096_session_v5_join(
                clean_exact,
                clean_sidecar,
                expected_commit="b" * 40,
                expected_workers=4,
            )

    def test_total_attempt_requires_a_successful_session_v5_arm_witness(self) -> None:
        self.assertTrue(
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2(
                self.exact,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )
        )
        all_failure = copy.deepcopy(self.exact)
        _make_all_failure_same_run(all_failure)
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "successfully authenticated arm record",
        ):
            v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2(
                all_failure,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

    def test_named_clis_are_bounded_and_validate_raw_before_telemetry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            ordinary_path = root / "ordinary.json"
            exact_path = root / "exact.json"
            sidecar_path = root / "sidecar.json"
            ordinary_path.write_text(
                json.dumps(self.ordinary, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            exact_path.write_text(
                json.dumps(self.exact, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            sidecar_path.write_text(
                json.dumps(self.sidecar, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            ordinary_validator = str(
                runfile("phase4_confirmatory_h4096_session_v5_raw_evidence_validator")
            )
            telemetry_validator_path = str(
                runfile(
                    "phase4_confirmatory_h4096_session_v5_same_run_decision_telemetry_validator"
                )
            )
            common = [
                "--testing-allow-unstamped",
                "--testing-repetitions=1",
                "--testing-workers=4",
            ]
            ordinary = subprocess.run(
                [ordinary_validator, *common, str(ordinary_path)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(ordinary.returncode, 0, ordinary.stderr)
            removed_total_attempt_mode = subprocess.run(
                [
                    ordinary_validator,
                    "--same-run-total-attempt-v2",
                    *common,
                    str(exact_path),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(removed_total_attempt_mode.returncode, 2)
            self.assertIn(
                "unrecognized arguments: --same-run-total-attempt-v2",
                removed_total_attempt_mode.stderr,
            )
            joined = subprocess.run(
                [telemetry_validator_path, *common, str(exact_path), str(sidecar_path)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(joined.returncode, 0, joined.stderr)

            fifo = root / "must-not-block.fifo"
            os.mkfifo(fifo)
            rejected_fifo = subprocess.run(
                [ordinary_validator, *common, str(fifo)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(rejected_fifo.returncode, 1)
            self.assertIn("regular file", rejected_fifo.stderr)

            raw_first = subprocess.run(
                [telemetry_validator_path, *common, str(ordinary_path), str(fifo)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(raw_first.returncode, 1)
            self.assertIn("same-run raw cell fields differ", raw_first.stderr)
            self.assertNotIn("regular file", raw_first.stderr)

            invalid_exact = copy.deepcopy(self.exact)
            invalid_exact["source_envelope_checksum"] ^= 1
            invalid_exact_path = root / "invalid-exact.json"
            invalid_exact_path.write_text(
                json.dumps(invalid_exact, separators=(",", ":")) + "\n",
                encoding="utf-8",
            )
            deep_raw_first = subprocess.run(
                [telemetry_validator_path, *common, str(invalid_exact_path), str(fifo)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(deep_raw_first.returncode, 1)
            self.assertIn("source_envelope_checksum", deep_raw_first.stderr)
            self.assertNotIn("regular file", deep_raw_first.stderr)

    def test_frozen_protocol_v2_acceptance_is_unchanged(self) -> None:
        ordinary = artifacts.make_raw(10200, 8, same_run=False, h4096=True, repetitions=1)
        exact = artifacts.make_raw(10100, 4, same_run=True, h4096=True, repetitions=1)
        sidecar = artifacts.make_sidecar(exact)
        predecessor_raw.validate_confirmatory_h4096_document(
            ordinary,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )
        predecessor_raw.validate_confirmatory_h4096_same_run_document_v2(
            exact,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )
        predecessor_telemetry.validate_confirmatory_h4096_join(
            exact,
            sidecar,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )


if __name__ == "__main__":
    unittest.main()
