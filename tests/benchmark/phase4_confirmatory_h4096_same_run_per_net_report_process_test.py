"""Acquisition-free process tests for the H4096 same-run per-net join."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_h4096_per_net_report as h4096_ordinary_report
from tools import (
    validate_phase4_confirmatory_h4096_same_run_per_net_report as h4096_same_run_report,
)
from tools import validate_phase4_confirmatory_same_run_per_net_report as h2250_same_run_report
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def report_command(raw: dict[str, object], executable: str) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(runfile(executable)),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
        "--raw_wire_schema_version=2",
        f"--apgar_commit={raw['source_commit']}",
        f"--case_id={config['case_id']}",
        f"--pool_size={config['requested_pool_size']}",
        f"--workers={config['preparation_worker_count']}",
        f"--repetitions={config['repetitions']}",
        f"--setup_ns={config['maximum_setup_elapsed_nanoseconds']}",
        f"--prepared_ns={config['external_budget']['maximum_prepared_elapsed_nanoseconds']}",
        f"--cold_ns={config['external_budget']['maximum_cold_elapsed_nanoseconds']}",
        f"--address_space_bytes={config['external_budget']['maximum_address_space_bytes']}",
        f"--peak_host_bytes={config['external_budget']['maximum_peak_host_bytes']}",
        f"--maximum_nets={config['corpus_limits']['maximum_nets']}",
        f"--maximum_compiled_nodes={config['corpus_limits']['maximum_compiled_nodes']}",
        f"--maximum_compiled_host_bytes={config['corpus_limits']['maximum_compiled_host_bytes']}",
        f"--maximum_active_regions={config['corpus_limits']['maximum_active_regions']}",
        f"--maximum_board_entities={config['corpus_limits']['maximum_board_entities']}",
        f"--raw_cell_plan_checksum={raw['cell_plan_checksum']}",
        f"--raw_cell_artifact_checksum={raw['artifact_checksum']}",
        f"--raw_source_envelope_checksum={raw['source_envelope_checksum']}",
        f"--pair_attempt_checksum={attempt['attempt_checksum']}",
        f"--paired_semantic_checksum={paired['semantic_checksum']}",
        f"--paired_artifact_checksum={paired['artifact_checksum']}",
        f"--baseline_semantic_checksum={baseline['semantics']['semantic_checksum']}",
        f"--baseline_arm_artifact_checksum={baseline['artifact_checksum']}",
        f"--candidate_semantic_checksum={candidate['semantics']['semantic_checksum']}",
        f"--candidate_arm_artifact_checksum={candidate['artifact_checksum']}",
    ]


def with_exact_rejection(
    source: dict[str, object],
) -> tuple[dict[str, object], dict[str, object], dict[str, object]]:
    raw = copy.deepcopy(source)
    for pair in raw["attempts"]:
        candidate_attempt = pair["candidate"]
        candidate_record = candidate_attempt["record"]
        semantics = candidate_record["semantics"]
        semantics["actual"]["route_queries"] += 1
        semantics["actual"]["route_work_units"] += 1
        semantics["preparation_route_queries"] += 1
        semantics["preparation_route_work_units"] += 1
        semantics["requested_columns"] += 1
        semantics["rejected_columns"] += 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        observation = candidate_record["external_observation"]
        observation["associated_semantic_checksum"] = semantics["semantic_checksum"]
        observation["authority_checksum"] = raw_validator.compute_authority_checksum(observation)
        candidate_record["artifact_checksum"] = raw_validator.compute_record_checksum(
            candidate_record
        )
        candidate_attempt["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(
            candidate_attempt
        )
        paired = pair["result"]
        paired["candidate"] = copy.deepcopy(candidate_record)
        paired["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(paired)
        paired["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(paired)
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)

    sidecar = artifacts.make_sidecar(raw)
    for attempt in sidecar["attempts"]:
        candidate = attempt["candidate"]
        columns = candidate["telemetry"]["per_net"][0]["columns"]
        columns["requested_columns"] = 2
        columns["executed_route_queries"] = 2
        columns["exact_validation_rejections"] = 1
        candidate["telemetry"]["telemetry_checksum"] = (
            telemetry_validator.compute_telemetry_checksum(candidate["telemetry"])
        )
        candidate["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(candidate)
        attempt["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(attempt)
    sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
        sidecar
    )

    report = artifacts.make_report(raw)
    columns = report["arms"][1]["diagnostic"]["telemetry"]["per_net"][0]["columns"]
    columns["requested_columns"] = 2
    columns["executed_route_queries"] = 2
    columns["exact_validation_rejections"] = 1
    report["arms"][1]["diagnostic"]["telemetry"]["telemetry_checksum"] = (
        report_validator.compute_telemetry_checksum(report["arms"][1]["diagnostic"]["telemetry"])
    )
    report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(report)
    report["source_envelope_checksum"] = report_validator.compute_report_source_envelope_checksum(
        report
    )
    return raw, sidecar, report


class Phase4ConfirmatoryH4096SameRunPerNetReportProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        cls.raw = artifacts.make_raw(10100, 4, same_run=True, h4096=True, repetitions=20)
        artifacts.mark_raw_clean(cls.raw)
        cls.sidecar = artifacts.make_sidecar(cls.raw)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = root / "raw.json"
        cls.sidecar_path = root / "sidecar.json"
        cls.report_path = root / "report.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")
        cls.report_path.write_text(canonical(cls.report), encoding="utf-8")

        cls.h2250_raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
        )
        artifacts.mark_raw_clean(cls.h2250_raw)
        cls.h2250_raw_path = root / "h2250-raw.json"
        cls.h2250_raw_path.write_text(canonical(cls.h2250_raw), encoding="utf-8")

        cls.ordinary_raw = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
        )
        artifacts.mark_raw_clean(cls.ordinary_raw)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_synthetic_three_way_join_is_complete_and_diagnostic_only(self) -> None:
        h4096_same_run_report.validate_join(
            self.raw,
            self.sidecar,
            self.report,
            expected_commit=self.raw["source_commit"],
        )
        self.assertTrue(telemetry_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 2)
        self.assertEqual(len(self.report["arms"]), 2)
        for arm in self.report["arms"]:
            self.assertEqual(len(arm["diagnostic"]["telemetry"]["per_net"]), 6)
        encoded = canonical(self.report)
        for forbidden in (
            "case_build_elapsed_nanoseconds",
            "prepared_elapsed_nanoseconds",
            "cold_elapsed_nanoseconds",
            "outer_elapsed_nanoseconds",
            "process_lifetime_peak_host_bytes",
        ):
            self.assertNotIn(f'"{forbidden}"', encoded)

    def test_valid_failed_exact_rejection_guardrail_remains_joinable(self) -> None:
        raw, sidecar, report = with_exact_rejection(self.raw)
        h4096_same_run_report.validate_join(
            raw,
            sidecar,
            report,
            expected_commit=raw["source_commit"],
        )
        self.assertFalse(telemetry_validator.exact_rejection_guardrail_passes(sidecar))

    def test_cross_authority_and_rehashed_associations_are_rejected(self) -> None:
        with self.assertRaisesRegex(raw_validator.EvidenceError, "raw cell fields differ"):
            h4096_ordinary_report.validate_join(
                self.raw,
                self.report,
                expected_commit=self.raw["source_commit"],
            )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            h2250_same_run_report.validate_join(
                self.raw,
                self.sidecar,
                self.report,
                expected_commit=self.raw["source_commit"],
            )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "same-run"):
            h4096_same_run_report.validate_join(
                self.ordinary_raw,
                self.sidecar,
                self.report,
                expected_commit=self.ordinary_raw["source_commit"],
            )

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            h4096_same_run_report.validate_join(
                self.raw,
                foreign_sidecar,
                self.report,
                expected_commit=self.raw["source_commit"],
            )

        foreign_report = copy.deepcopy(self.report)
        foreign_report["raw_reference"]["pair_attempt_checksum"] ^= 1
        foreign_report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_report
        )
        foreign_report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_report)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "raw reference"):
            h4096_same_run_report.validate_join(
                self.raw,
                self.sidecar,
                foreign_report,
                expected_commit=self.raw["source_commit"],
            )

    def test_named_validator_opens_raw_then_sidecar_then_report(self) -> None:
        validator = str(runfile("phase4_confirmatory_h4096_same_run_per_net_report_validator"))
        base = [
            validator,
            f"--expected-commit={self.raw['source_commit']}",
            f"--raw={self.raw_path}",
            f"--same-run-telemetry={self.sidecar_path}",
            f"--report={self.report_path}",
        ]
        accepted = subprocess.run(
            base,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        root = pathlib.Path(self.temporary.name)
        raw_fifo = root / "raw.fifo"
        sidecar_fifo = root / "sidecar.fifo"
        report_fifo = root / "report.fifo"
        os.mkfifo(raw_fifo)
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)

        raw_first = subprocess.run(
            [
                validator,
                base[1],
                f"--raw={raw_fifo}",
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(raw_first.returncode, 1)
        self.assertIn("regular file", raw_first.stderr)

        h2250_first = subprocess.run(
            [
                validator,
                base[1],
                f"--raw={self.h2250_raw_path}",
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(h2250_first.returncode, 1)
        self.assertIn("another command or cell", h2250_first.stderr)
        self.assertNotIn("regular file", h2250_first.stderr)

        wrong_commit_first = subprocess.run(
            [
                validator,
                f"--expected-commit={'f' * 40}",
                f"--raw={self.raw_path}",
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(wrong_commit_first.returncode, 1)
        self.assertIn("independently supplied commit", wrong_commit_first.stderr)
        self.assertNotIn("regular file", wrong_commit_first.stderr)

        sidecar_first = subprocess.run(
            [
                *base[:3],
                f"--same-run-telemetry={sidecar_fifo}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(sidecar_first.returncode, 1)
        self.assertIn("regular file", sidecar_first.stderr)

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        foreign_sidecar_path = root / "foreign-sidecar.json"
        foreign_sidecar_path.write_text(canonical(foreign_sidecar), encoding="utf-8")
        foreign_first = subprocess.run(
            [
                *base[:3],
                f"--same-run-telemetry={foreign_sidecar_path}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(foreign_first.returncode, 1)
        self.assertIn("differs from Raw", foreign_first.stderr)
        self.assertNotIn("regular file", foreign_first.stderr)

        foreign_source_sidecar = copy.deepcopy(self.sidecar)
        foreign_source_sidecar["source_commit"] = "f" * 40
        foreign_source_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(foreign_source_sidecar)
        )
        foreign_source_sidecar_path = root / "foreign-source-sidecar.json"
        foreign_source_sidecar_path.write_text(
            canonical(foreign_source_sidecar),
            encoding="utf-8",
        )
        foreign_source_first = subprocess.run(
            [
                *base[:3],
                f"--same-run-telemetry={foreign_source_sidecar_path}",
                f"--report={report_fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(foreign_source_first.returncode, 1)
        self.assertIn("expected clean stamped commit", foreign_source_first.stderr)
        self.assertNotIn("regular file", foreign_source_first.stderr)

        report_last = subprocess.run(
            [*base[:4], f"--report={report_fifo}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(report_last.returncode, 1)
        self.assertIn("regular file", report_last.stderr)

        foreign_source_report = copy.deepcopy(self.report)
        foreign_source_report["source_commit"] = "f" * 40
        foreign_source_report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_source_report)
        )
        foreign_source_report_path = root / "foreign-source-report.json"
        foreign_source_report_path.write_text(
            canonical(foreign_source_report),
            encoding="utf-8",
        )
        foreign_report_last = subprocess.run(
            [*base[:4], f"--report={foreign_source_report_path}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(foreign_report_last.returncode, 1)
        self.assertIn("same clean independently expected commit", foreign_report_last.stderr)
        self.assertNotIn("regular file", foreign_report_last.stderr)

    def test_test_runner_is_preflight_only_and_scope_is_fixed(self) -> None:
        valid = report_command(
            self.raw,
            "phase4_confirmatory_h4096_same_run_per_net_report_test_runner",
        )
        completed = subprocess.run(
            valid,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertIn("preflight-only", completed.stderr)
        self.assertEqual(completed.stdout, "")

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in valid
            ]

        mutations = (
            replace("corpus_version", "1"),
            replace("raw_wire_schema_version", "1"),
            replace("pool_size", "8"),
            replace("case_id", "10101"),
            replace("case_id", "10200"),
            replace("case_id", "11000"),
            [argument for argument in valid if not argument.startswith("--corpus_version=")],
            valid + [valid[-1]],
            valid + ["--fixture_path=/tmp/forbidden"],
        )
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                rejected = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(rejected.returncode, 2, rejected.stderr)
                self.assertNotIn("preflight-only", rejected.stderr)
                self.assertEqual(rejected.stdout, "")

        ordinary = list(valid)
        ordinary[0] = str(runfile("phase4_confirmatory_h4096_per_net_report_test_runner"))
        rejected = subprocess.run(
            ordinary,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertNotIn("preflight-only", rejected.stderr)
        self.assertEqual(rejected.stdout, "")

        production = list(valid)
        production[0] = str(runfile("phase4_confirmatory_h4096_same_run_per_net_report_runner"))
        rejected = subprocess.run(
            production,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertNotIn("preflight-only", rejected.stderr)
        self.assertEqual(rejected.stdout, "")

    def test_forced_unpublishable_runner_rejects_before_diagnostics(self) -> None:
        command = report_command(
            self.raw,
            "phase4_confirmatory_h4096_same_run_per_net_report_unpublishable_source_test_runner",
        )
        command = [argument for argument in command if argument != "--testing_allow_unstamped=1"]
        dirty_raw = copy.deepcopy(self.raw)
        dirty_raw["source_stamped"] = False
        dirty_raw["source_tree_dirty"] = True
        dirty_raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            dirty_raw
        )
        prefix = "--raw_source_envelope_checksum="
        command = [
            f"{prefix}{dirty_raw['source_envelope_checksum']}"
            if argument.startswith(prefix)
            else argument
            for argument in command
        ]
        rejected = subprocess.run(
            command,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertEqual(rejected.stdout, "")


if __name__ == "__main__":
    unittest.main()
