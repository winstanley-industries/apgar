"""Adversarial, acquisition-free tests for H4096 Raw authority validators."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import (
    validate_phase4_confirmatory_h4096_same_run_decision_telemetry as h4096_telemetry,
)
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4ConfirmatoryH4096RawEvidenceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.ordinary = artifacts.make_raw(10200, 8, same_run=False, h4096=True, repetitions=1)
        self.exact = artifacts.make_raw(10100, 4, same_run=True, h4096=True, repetitions=1)
        self.sidecar = artifacts.make_sidecar(self.exact)

    def validate_ordinary(self, value: object) -> None:
        h4096_raw.validate_confirmatory_h4096_document(
            value,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )

    def validate_exact(self, raw: object, sidecar: object) -> None:
        h4096_telemetry.validate_confirmatory_h4096_join(
            raw,
            sidecar,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )

    def test_accepts_only_the_two_synthetic_protocol_v2_development_authorities(self) -> None:
        self.validate_ordinary(self.ordinary)
        h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
            self.exact,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )
        self.validate_exact(self.exact, self.sidecar)
        self.assertTrue(telemetry_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertEqual(
            h4096_raw.confirmatory_h4096_initial_cells(same_run=False),
            frozenset({(10200, 8)}),
        )
        self.assertEqual(
            h4096_raw.confirmatory_h4096_initial_cells(same_run=True),
            frozenset({(10100, 4)}),
        )

    def test_h2250_and_h4096_budgets_cross_reject_in_both_directions(self) -> None:
        h2250_ordinary = artifacts.make_raw(10200, 8, same_run=False, h4096=False, repetitions=1)
        raw_validator.validate_confirmatory_document(
            h2250_ordinary,
            allow_unstamped=True,
            expected_repetitions=1,
            expected_workers=4,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            self.validate_ordinary(h2250_ordinary)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            raw_validator.validate_confirmatory_document(
                self.ordinary,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

        h2250_exact = artifacts.make_raw(10100, 4, same_run=True, h4096=False, repetitions=1)
        h2250_sidecar = artifacts.make_sidecar(h2250_exact)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            self.validate_exact(h2250_exact, h2250_sidecar)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            telemetry_validator.validate_confirmatory_join(
                self.exact,
                self.sidecar,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            self.validate_exact(self.exact, h2250_sidecar)

    def test_budget_authority_uses_exact_roster_v3_checksums(self) -> None:
        _, _, budgets = h4096_raw._authority_provider(2)
        self.assertEqual(budgets[(10100, 4)], 8829615204625848656)
        self.assertEqual(budgets[(10200, 8)], 8230401457668518004)
        _, _, h2250_budgets = raw_validator._frozen_confirmatory_representative_manifest()
        self.assertEqual(h2250_budgets[(10100, 4)], 15423003727971578168)
        self.assertEqual(h2250_budgets[(10200, 8)], 9780871533056160060)

    def test_wrong_carrier_and_broader_exact_or_calibration_scope_are_closed(self) -> None:
        wrong_carrier_exact = artifacts.make_raw(
            10100, 4, same_run=False, h4096=True, repetitions=1
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "initial ordinary Raw"):
            self.validate_ordinary(wrong_carrier_exact)

        wrong_carrier_calibration = artifacts.make_raw(
            10200, 8, same_run=True, h4096=True, repetitions=1
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "initial same-run Raw"):
            h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
                wrong_carrier_calibration,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

        other_exact = artifacts.make_raw(10101, 4, same_run=True, h4096=True, repetitions=1)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "initial same-run Raw"):
            h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
                other_exact,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

        other_calibration = artifacts.make_raw(10201, 8, same_run=False, h4096=True, repetitions=1)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "initial ordinary Raw"):
            self.validate_ordinary(other_calibration)

    def test_same_run_join_rejects_rehashed_foreign_association(self) -> None:
        foreign = copy.deepcopy(self.sidecar)
        arm = foreign["attempts"][0]["candidate"]
        arm["associated_semantic_checksum"] ^= 1
        arm["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(arm)
        pair = foreign["attempts"][0]
        pair["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(pair)
        foreign["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
            foreign
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            self.validate_exact(self.exact, foreign)

    def test_unstamped_synthetic_evidence_cannot_be_published(self) -> None:
        with self.assertRaisesRegex(raw_validator.EvidenceError, "publication requires"):
            h4096_raw.validate_confirmatory_h4096_document(
                self.ordinary,
                expected_commit="0" * 40,
                expected_repetitions=1,
                expected_workers=4,
            )
        self.validate_ordinary(self.ordinary)

    def test_complete_total_attempt_raw_authenticates_but_is_not_a_publication_cli_mode(
        self,
    ) -> None:
        self.assertTrue(
            h4096_raw.validate_confirmatory_h4096_same_run_total_attempt_document_v2(
                self.exact,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )
        )

    def test_total_attempt_requires_a_successful_h4096_arm_witness(self) -> None:
        h2250_failure = artifacts.make_h2250_zero_dispatch_same_run_raw()
        self.assertFalse(
            raw_validator.validate_confirmatory_same_run_total_attempt_document_v2(
                h2250_failure,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )
        )
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "successfully authenticated H=4096 arm record",
        ):
            h4096_raw.validate_confirmatory_h4096_same_run_total_attempt_document_v2(
                h2250_failure,
                allow_unstamped=True,
                expected_repetitions=1,
                expected_workers=4,
            )

    def test_named_validator_clis_accept_synthetic_authorities_and_reject_fifo(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            ordinary_path = root / "ordinary.json"
            exact_path = root / "exact.json"
            sidecar_path = root / "sidecar.json"
            ordinary_path.write_text(
                json.dumps(self.ordinary, separators=(",", ":")) + "\n", encoding="utf-8"
            )
            exact_path.write_text(
                json.dumps(self.exact, separators=(",", ":")) + "\n", encoding="utf-8"
            )
            sidecar_path.write_text(
                json.dumps(self.sidecar, separators=(",", ":")) + "\n", encoding="utf-8"
            )
            ordinary = subprocess.run(
                [
                    str(runfile("phase4_confirmatory_h4096_raw_evidence_validator")),
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
                    str(ordinary_path),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(ordinary.returncode, 0, ordinary.stderr)
            removed_total_attempt_mode = subprocess.run(
                [
                    str(runfile("phase4_confirmatory_h4096_raw_evidence_validator")),
                    "--same-run-total-attempt-v2",
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
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
                [
                    str(runfile("phase4_confirmatory_h4096_same_run_decision_telemetry_validator")),
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
                    str(exact_path),
                    str(sidecar_path),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(joined.returncode, 0, joined.stderr)

            fifo = root / "must-not-block.fifo"
            os.mkfifo(fifo)
            rejected = subprocess.run(
                [
                    str(runfile("phase4_confirmatory_h4096_raw_evidence_validator")),
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
                    str(fifo),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(rejected.returncode, 1)
            self.assertIn("regular file", rejected.stderr)

            raw_first = subprocess.run(
                [
                    str(runfile("phase4_confirmatory_h4096_same_run_decision_telemetry_validator")),
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
                    str(ordinary_path),
                    str(fifo),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(raw_first.returncode, 1)
            self.assertIn("same-run raw cell fields differ", raw_first.stderr)
            self.assertNotIn("regular file", raw_first.stderr)

            deep_invalid_path = root / "deep-invalid-exact.json"
            deep_invalid = copy.deepcopy(self.exact)
            deep_invalid["source_envelope_checksum"] ^= 1
            deep_invalid_path.write_text(
                json.dumps(deep_invalid, separators=(",", ":")) + "\n", encoding="utf-8"
            )
            deep_raw_first = subprocess.run(
                [
                    str(runfile("phase4_confirmatory_h4096_same_run_decision_telemetry_validator")),
                    "--testing-allow-unstamped",
                    "--testing-repetitions=1",
                    "--testing-workers=4",
                    str(deep_invalid_path),
                    str(fifo),
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(deep_raw_first.returncode, 1)
            self.assertIn(
                "source_envelope_checksum does not authenticate source provenance",
                deep_raw_first.stderr,
            )
            self.assertNotIn("regular file", deep_raw_first.stderr)


if __name__ == "__main__":
    unittest.main()
