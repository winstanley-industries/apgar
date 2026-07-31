"""Synthetic tests for the Session-v5 H=4096 same-run per-net report join."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from typing import Any

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import (
    validate_phase4_confirmatory_h4096_same_run_per_net_report as predecessor_report,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_per_net_report as ordinary_session_v5_report,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_raw_evidence as session_v5_raw,
)
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_same_run_per_net_report as session_v5_report,
)
from tools import validate_phase4_confirmatory_same_run_per_net_report as h2250_report
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_SOURCE_COMMIT = "a" * 40


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(_canonical(value), encoding="utf-8")


def _write_oversized(path: pathlib.Path, maximum_bytes: int) -> None:
    with path.open("wb") as stream:
        stream.seek(maximum_bytes)
        stream.write(b"x")


def _rehash_raw(raw: dict[str, Any], *, rehash_plan: bool = False) -> None:
    if rehash_plan:
        raw["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
            raw,
            corpus_version=2,
        )
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _rehash_sidecar(sidecar: dict[str, Any]) -> None:
    for attempt in sidecar["attempts"]:
        for arm_name in ("baseline", "candidate"):
            arm = attempt[arm_name]
            telemetry = arm["telemetry"]
            telemetry["telemetry_checksum"] = telemetry_validator.compute_telemetry_checksum(
                telemetry
            )
            arm["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(arm)
        attempt["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(attempt)
    sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
        sidecar
    )


def _rehash_report(report: dict[str, Any]) -> None:
    for arm in report["arms"]:
        telemetry = arm["diagnostic"]["telemetry"]
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
    report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(report)
    report["source_envelope_checksum"] = report_validator.compute_report_source_envelope_checksum(
        report
    )


def _fail_pair_in_place(raw: dict[str, Any], index: int) -> None:
    pair = raw["attempts"][index]
    for arm_name in ("baseline", "candidate"):
        arm = pair[arm_name]
        failed = arm_name == "baseline"
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


def _with_all_failure(source: dict[str, Any]) -> dict[str, Any]:
    raw = copy.deepcopy(source)
    _fail_pair_in_place(raw, 0)
    for pair in raw["attempts"][1:]:
        for arm_name in ("baseline", "candidate"):
            arm = pair[arm_name]
            arm.update(
                {
                    "disposition": 10,
                    "dispatch_ordinal": 0,
                    "process_instance_identity": 0,
                    "outer_elapsed_nanoseconds": 0,
                    "process_lifetime_peak_host_bytes": 0,
                    "raw_wait_status": 0,
                    "process_exit_code": -1,
                    "terminating_signal": 0,
                    "watchdog_kill_sent": False,
                    "controller_invariant_id": "",
                    "controller_detail": "",
                    "record": None,
                    "child_failure": None,
                }
            )
            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        pair["result"] = None
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    _rehash_raw(raw)
    return raw


def _with_late_finalization_failure(
    source: dict[str, Any],
    index: int,
) -> dict[str, Any]:
    raw = copy.deepcopy(source)
    pair = raw["attempts"][index]
    candidate = pair["candidate"]
    candidate["disposition"] = 8
    candidate["controller_invariant_id"] = "P4PAIR-FINALIZE-005"
    candidate["controller_detail"] = "one or more external trial budgets were exceeded"
    candidate["record"] = None
    candidate["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(candidate)
    pair["result"] = None
    pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    _rehash_raw(raw)
    return raw


def _with_exact_rejection(
    source: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
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
    _rehash_raw(raw)

    sidecar = artifacts.make_sidecar(raw)
    for attempt in sidecar["attempts"]:
        candidate = attempt["candidate"]
        columns = candidate["telemetry"]["per_net"][0]["columns"]
        columns["requested_columns"] = 2
        columns["executed_route_queries"] = 2
        columns["exact_validation_rejections"] = 1
    _rehash_sidecar(sidecar)

    report = artifacts.make_report(raw)
    columns = report["arms"][1]["diagnostic"]["telemetry"]["per_net"][0]["columns"]
    columns["requested_columns"] = 2
    columns["executed_route_queries"] = 2
    columns["other_rejections"] = 1
    _rehash_report(report)
    return raw, sidecar, report


class Phase4ConfirmatoryH4096SessionV5SameRunPerNetReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)

        cls.raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(cls.raw, _SOURCE_COMMIT)
        cls.sidecar = artifacts.make_sidecar(cls.raw)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = root / "session-v5-raw.json"
        cls.sidecar_path = root / "session-v5-sidecar.json"
        cls.report_path = root / "session-v5-report.json"
        _write_json(cls.raw_path, cls.raw)
        _write_json(cls.sidecar_path, cls.sidecar)
        _write_json(cls.report_path, cls.report)

        cls.predecessor_raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.predecessor_raw, _SOURCE_COMMIT)
        cls.predecessor_sidecar = artifacts.make_sidecar(cls.predecessor_raw)
        cls.predecessor_report = artifacts.make_report(cls.predecessor_raw)

        cls.h2250_raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.h2250_raw, _SOURCE_COMMIT)
        cls.h2250_sidecar = artifacts.make_sidecar(cls.h2250_raw)
        cls.h2250_report = artifacts.make_report(cls.h2250_raw)

        cls.ordinary_raw = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(cls.ordinary_raw, _SOURCE_COMMIT)
        cls.ordinary_report = artifacts.make_report(cls.ordinary_raw)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def _run_named_validator(
        self,
        raw_path: pathlib.Path,
        sidecar_path: pathlib.Path,
        report_path: pathlib.Path,
        *,
        expected_commit: str = _SOURCE_COMMIT,
        extra: tuple[str, ...] = (),
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                str(
                    runfile(
                        "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_validator"
                    )
                ),
                f"--expected-commit={expected_commit}",
                f"--raw={raw_path}",
                f"--same-run-telemetry={sidecar_path}",
                f"--report={report_path}",
                *extra,
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )

    def test_canonical_and_failed_guardrail_triples_validate_without_decision_output(self) -> None:
        session_v5_report.validate_join(
            self.raw,
            self.sidecar,
            self.report,
            expected_commit=_SOURCE_COMMIT,
        )
        self.assertTrue(telemetry_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 2)
        self.assertEqual(len(self.raw["attempts"]), 20)
        self.assertEqual(
            sum(
                1
                for pair in self.raw["attempts"]
                for arm_name in ("baseline", "candidate")
                if pair[arm_name]["record"] is not None
            ),
            40,
        )
        self.assertEqual(len(self.sidecar["attempts"]), 20)
        self.assertEqual(
            sum(
                len(pair[arm_name]["telemetry"]["per_net"])
                for pair in self.sidecar["attempts"]
                for arm_name in ("baseline", "candidate")
            ),
            240,
        )
        self.assertEqual(
            sum(len(arm["diagnostic"]["telemetry"]["per_net"]) for arm in self.report["arms"]),
            12,
        )

        root = pathlib.Path(self.temporary.name)
        failed_raw, failed_sidecar, failed_report = _with_exact_rejection(self.raw)
        session_v5_report.validate_join(
            failed_raw,
            failed_sidecar,
            failed_report,
            expected_commit=_SOURCE_COMMIT,
        )
        self.assertFalse(telemetry_validator.exact_rejection_guardrail_passes(failed_sidecar))
        self.assertEqual(
            failed_report["arms"][1]["diagnostic"]["telemetry"]["per_net"][0]["columns"][
                "exact_validation_rejections"
            ],
            0,
        )
        self.assertEqual(
            failed_report["arms"][1]["diagnostic"]["telemetry"]["per_net"][0]["columns"][
                "other_rejections"
            ],
            1,
        )

        failed_raw_path = root / "failed-guardrail-raw.json"
        failed_sidecar_path = root / "failed-guardrail-sidecar.json"
        failed_report_path = root / "failed-guardrail-report.json"
        _write_json(failed_raw_path, failed_raw)
        _write_json(failed_sidecar_path, failed_sidecar)
        _write_json(failed_report_path, failed_report)
        passing = self._run_named_validator(self.raw_path, self.sidecar_path, self.report_path)
        failing_guardrail = self._run_named_validator(
            failed_raw_path,
            failed_sidecar_path,
            failed_report_path,
        )
        self.assertEqual(passing.returncode, 0, passing.stderr)
        self.assertEqual(failing_guardrail.returncode, 0, failing_guardrail.stderr)
        expected_stdout = (
            "validated one Phase 4 confirmatory Session-v5 H=4096 "
            "same-run Raw/telemetry/per-net report join\n"
        )
        self.assertEqual(passing.stdout, expected_stdout)
        self.assertEqual(failing_guardrail.stdout, expected_stdout)
        self.assertEqual(passing.stderr, "")
        self.assertEqual(failing_guardrail.stderr, "")

    def test_named_cli_is_strict_and_enforces_raw_sidecar_report_open_order(self) -> None:
        root = pathlib.Path(self.temporary.name)
        raw_fifo = root / "ordering-raw.fifo"
        sidecar_fifo = root / "ordering-sidecar.fifo"
        report_fifo = root / "ordering-report.fifo"
        os.mkfifo(raw_fifo)
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)

        accepted = self._run_named_validator(self.raw_path, self.sidecar_path, self.report_path)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)

        for option in (
            "--testing-allow-unstamped",
            "--testing-repetitions=1",
            "--same-run-t=foreign.json",
        ):
            with self.subTest(option=option):
                rejected = self._run_named_validator(
                    raw_fifo,
                    sidecar_fifo,
                    report_fifo,
                    extra=(option,),
                )
                self.assertEqual(rejected.returncode, 2)
                self.assertIn("unrecognized arguments", rejected.stderr)

        reached_raw = self._run_named_validator(raw_fifo, sidecar_fifo, report_fifo)
        self.assertEqual(reached_raw.returncode, 1)
        self.assertIn("regular file", reached_raw.stderr)

        corrupt_raw = copy.deepcopy(self.raw)
        corrupt_raw["source_envelope_checksum"] ^= 1
        corrupt_raw_path = root / "ordering-corrupt-raw.json"
        _write_json(corrupt_raw_path, corrupt_raw)
        rejected_raw = self._run_named_validator(corrupt_raw_path, sidecar_fifo, report_fifo)
        self.assertEqual(rejected_raw.returncode, 1)
        self.assertNotIn("regular file", rejected_raw.stderr)

        reached_sidecar = self._run_named_validator(self.raw_path, sidecar_fifo, report_fifo)
        self.assertEqual(reached_sidecar.returncode, 1)
        self.assertIn("regular file", reached_sidecar.stderr)

        corrupt_sidecar = copy.deepcopy(self.sidecar)
        corrupt_sidecar["source_envelope_checksum"] ^= 1
        corrupt_sidecar_path = root / "ordering-corrupt-sidecar.json"
        _write_json(corrupt_sidecar_path, corrupt_sidecar)
        rejected_sidecar = self._run_named_validator(
            self.raw_path,
            corrupt_sidecar_path,
            report_fifo,
        )
        self.assertEqual(rejected_sidecar.returncode, 1)
        self.assertNotIn("regular file", rejected_sidecar.stderr)

        reached_report = self._run_named_validator(
            self.raw_path,
            self.sidecar_path,
            report_fifo,
        )
        self.assertEqual(reached_report.returncode, 1)
        self.assertIn("regular file", reached_report.stderr)

    def test_complete_raw_api_rejects_partial_and_all_failure_before_sidecar(self) -> None:
        root = pathlib.Path(self.temporary.name)
        sidecar_fifo = root / "total-attempt-sidecar.fifo"
        report_fifo = root / "total-attempt-report.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)

        cases = (
            ("repetition-zero", _with_late_finalization_failure(self.raw, 0), False),
            ("later-repetition", _with_late_finalization_failure(self.raw, 7), False),
            ("all-failure", _with_all_failure(self.raw), None),
        )
        for label, raw, total_result in cases:
            raw_path = root / f"{label}-raw.json"
            _write_json(raw_path, raw)
            with self.subTest(label=label, surface="total-attempt"):
                if total_result is None:
                    with self.assertRaisesRegex(
                        raw_validator.EvidenceError,
                        "lacks a successfully authenticated arm",
                    ):
                        session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2(
                            raw,
                            expected_commit=_SOURCE_COMMIT,
                        )
                else:
                    self.assertEqual(
                        session_v5_raw.validate_confirmatory_h4096_session_v5_same_run_total_attempt_document_v2(
                            raw,
                            expected_commit=_SOURCE_COMMIT,
                        ),
                        total_result,
                    )
            with self.subTest(label=label, surface="publication"):
                rejected = self._run_named_validator(raw_path, sidecar_fifo, report_fifo)
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertNotIn("regular file", rejected.stderr)

    def test_named_raw_boundary_rejects_before_sidecar_access(self) -> None:
        root = pathlib.Path(self.temporary.name)
        sidecar_fifo = root / "invalid-raw-sidecar.fifo"
        report_fifo = root / "invalid-raw-report.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)
        canonical = self.raw_path.read_text(encoding="utf-8")

        duplicate = root / "duplicate-raw.json"
        duplicate.write_text(
            canonical.replace(
                '{"raw_evidence_schema_version":2,',
                '{"raw_evidence_schema_version":2,"raw_evidence_schema_version":2,',
                1,
            ),
            encoding="utf-8",
        )
        noncanonical = root / "noncanonical-raw.json"
        noncanonical.write_text(canonical + "\n", encoding="utf-8")
        corrupt = root / "corrupt-raw.json"
        corrupt.write_text("{\n", encoding="utf-8")
        deep = root / "deep-raw.json"
        deep.write_text("[" * 66 + "0" + "]" * 66 + "\n", encoding="utf-8")
        oversized = root / "oversized-raw.json"
        _write_oversized(oversized, raw_validator._MAXIMUM_RAW_JSON_BYTES)

        relaxed = copy.deepcopy(self.raw)
        relaxed["attempts"].pop()
        _rehash_raw(relaxed)
        relaxed_path = root / "relaxed-raw.json"
        _write_json(relaxed_path, relaxed)

        missing_arm = copy.deepcopy(self.raw)
        del missing_arm["attempts"][0]["candidate"]
        missing_arm_path = root / "missing-arm-raw.json"
        _write_json(missing_arm_path, missing_arm)

        forged_artifact = copy.deepcopy(self.raw)
        forged_artifact["artifact_checksum"] ^= 1
        forged_artifact["source_envelope_checksum"] = (
            raw_validator.compute_source_envelope_checksum(forged_artifact)
        )
        forged_artifact_path = root / "forged-artifact-raw.json"
        _write_json(forged_artifact_path, forged_artifact)

        dirty = copy.deepcopy(self.raw)
        dirty["source_stamped"] = False
        dirty["source_tree_dirty"] = True
        _rehash_raw(dirty)
        dirty_path = root / "dirty-raw.json"
        _write_json(dirty_path, dirty)

        cases = (
            ("duplicate", duplicate, _SOURCE_COMMIT, "duplicate"),
            ("noncanonical", noncanonical, _SOURCE_COMMIT, "canonical one-line JSON"),
            ("corrupt", corrupt, _SOURCE_COMMIT, "cannot read raw cell"),
            ("depth", deep, _SOURCE_COMMIT, "nesting bound"),
            ("oversized", oversized, _SOURCE_COMMIT, "exceeds"),
            ("cardinality", relaxed_path, _SOURCE_COMMIT, "exactly 20"),
            ("arm-cardinality", missing_arm_path, _SOURCE_COMMIT, "missing=['candidate']"),
            ("forged-artifact", forged_artifact_path, _SOURCE_COMMIT, "artifact_checksum"),
            ("dirty", dirty_path, _SOURCE_COMMIT, "clean, stamped"),
            ("expected-commit", self.raw_path, "b" * 40, "does not match"),
        )
        for label, raw_path, expected_commit, expected_error in cases:
            with self.subTest(label=label):
                rejected = self._run_named_validator(
                    raw_path,
                    sidecar_fifo,
                    report_fifo,
                    expected_commit=expected_commit,
                )
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)
                self.assertNotIn("regular file", rejected.stderr)

    def test_named_sidecar_boundary_rejects_before_report_access(self) -> None:
        root = pathlib.Path(self.temporary.name)
        report_fifo = root / "invalid-sidecar-report.fifo"
        os.mkfifo(report_fifo)
        canonical = self.sidecar_path.read_text(encoding="utf-8")

        duplicate = root / "duplicate-sidecar.json"
        duplicate.write_text(
            canonical.replace(
                '{"source_commit":',
                f'{{"source_commit":"{_SOURCE_COMMIT}","source_commit":',
                1,
            ),
            encoding="utf-8",
        )
        noncanonical = root / "noncanonical-sidecar.json"
        noncanonical.write_text(canonical + "\n", encoding="utf-8")
        corrupt = root / "corrupt-sidecar.json"
        corrupt.write_text("{\n", encoding="utf-8")
        deep = root / "deep-sidecar.json"
        deep.write_text("[" * 66 + "0" + "]" * 66 + "\n", encoding="utf-8")
        oversized = root / "oversized-sidecar.json"
        _write_oversized(oversized, telemetry_validator._MAX_BYTES)

        cross_commit = copy.deepcopy(self.sidecar)
        cross_commit["source_commit"] = "b" * 40
        cross_commit["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(cross_commit)
        )
        cross_commit_path = root / "cross-commit-sidecar.json"
        _write_json(cross_commit_path, cross_commit)

        dirty = copy.deepcopy(self.sidecar)
        dirty["source_stamped"] = False
        dirty["source_tree_dirty"] = True
        dirty["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
            dirty
        )
        dirty_path = root / "dirty-sidecar.json"
        _write_json(dirty_path, dirty)

        forged_source = copy.deepcopy(self.sidecar)
        forged_source["source_envelope_checksum"] ^= 1
        forged_source_path = root / "forged-source-sidecar.json"
        _write_json(forged_source_path, forged_source)

        forged_artifact = copy.deepcopy(self.sidecar)
        forged_artifact["artifact_checksum"] ^= 1
        forged_artifact["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(forged_artifact)
        )
        forged_artifact_path = root / "forged-artifact-sidecar.json"
        _write_json(forged_artifact_path, forged_artifact)

        relaxed_attempt = copy.deepcopy(self.sidecar)
        relaxed_attempt["attempts"].pop()
        _rehash_sidecar(relaxed_attempt)
        relaxed_attempt_path = root / "relaxed-attempt-sidecar.json"
        _write_json(relaxed_attempt_path, relaxed_attempt)

        missing_arm = copy.deepcopy(self.sidecar)
        del missing_arm["attempts"][0]["candidate"]
        missing_arm_path = root / "missing-arm-sidecar.json"
        _write_json(missing_arm_path, missing_arm)

        relaxed_row = copy.deepcopy(self.sidecar)
        relaxed_row["attempts"][0]["candidate"]["telemetry"]["per_net"].pop()
        _rehash_sidecar(relaxed_row)
        relaxed_row_path = root / "relaxed-row-sidecar.json"
        _write_json(relaxed_row_path, relaxed_row)

        reversed_arm = copy.deepcopy(self.sidecar)
        reversed_arm["attempts"][0]["candidate"]["arm"] = 0
        _rehash_sidecar(reversed_arm)
        reversed_arm_path = root / "reversed-arm-sidecar.json"
        _write_json(reversed_arm_path, reversed_arm)

        combined = copy.deepcopy(self.sidecar)
        combined["source_envelope_checksum"] ^= 1
        combined["attempts"][0]["candidate"]["telemetry"]["associated_semantic_checksum"] ^= 1
        combined_path = root / "combined-source-semantic-sidecar.json"
        _write_json(combined_path, combined)

        cases = (
            ("duplicate", duplicate, "duplicate"),
            ("noncanonical", noncanonical, "end in one LF"),
            ("corrupt", corrupt, "cannot read same-run telemetry"),
            ("depth", deep, "maximum JSON depth"),
            ("oversized", oversized, "exceeds"),
            ("cross-commit", cross_commit_path, "same clean"),
            ("dirty", dirty_path, "same clean"),
            ("forged-source", forged_source_path, "source envelope"),
            ("forged-artifact", forged_artifact_path, "artifact_checksum"),
            ("attempt-cardinality", relaxed_attempt_path, "every Raw repetition"),
            ("arm-cardinality", missing_arm_path, "missing=['candidate']"),
            ("row-cardinality", relaxed_row_path, "complete frozen roster"),
            ("arm", reversed_arm_path, "reversed or duplicated"),
            ("combined-source-first", combined_path, "source envelope"),
        )
        for label, sidecar_path, expected_error in cases:
            with self.subTest(label=label):
                rejected = self._run_named_validator(
                    self.raw_path,
                    sidecar_path,
                    report_fifo,
                )
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)
                self.assertNotIn("regular file", rejected.stderr)

    def test_named_report_boundary_and_source_preflight_reject(self) -> None:
        root = pathlib.Path(self.temporary.name)
        canonical = self.report_path.read_text(encoding="utf-8")

        duplicate = root / "duplicate-report.json"
        duplicate.write_text(
            canonical.replace(
                '{"source_commit":',
                f'{{"source_commit":"{_SOURCE_COMMIT}","source_commit":',
                1,
            ),
            encoding="utf-8",
        )
        noncanonical = root / "noncanonical-report.json"
        noncanonical.write_text(canonical + "\n", encoding="utf-8")
        corrupt = root / "corrupt-report.json"
        corrupt.write_text("{\n", encoding="utf-8")
        deep = root / "deep-report.json"
        deep.write_text("[" * 66 + "0" + "]" * 66 + "\n", encoding="utf-8")
        oversized = root / "oversized-report.json"
        _write_oversized(oversized, report_validator._MAX_REPORT_BYTES)

        cross_commit = copy.deepcopy(self.report)
        cross_commit["source_commit"] = "b" * 40
        cross_commit["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(cross_commit)
        )
        cross_commit_path = root / "cross-commit-report.json"
        _write_json(cross_commit_path, cross_commit)

        dirty = copy.deepcopy(self.report)
        dirty["source_stamped"] = False
        dirty["source_tree_dirty"] = True
        dirty["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(dirty)
        )
        dirty_path = root / "dirty-report.json"
        _write_json(dirty_path, dirty)

        forged_source = copy.deepcopy(self.report)
        forged_source["source_envelope_checksum"] ^= 1
        forged_source_path = root / "forged-source-report.json"
        _write_json(forged_source_path, forged_source)

        forged_artifact = copy.deepcopy(self.report)
        forged_artifact["artifact_checksum"] ^= 1
        forged_artifact["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(forged_artifact)
        )
        forged_artifact_path = root / "forged-artifact-report.json"
        _write_json(forged_artifact_path, forged_artifact)

        relaxed_arm = copy.deepcopy(self.report)
        relaxed_arm["arms"].pop()
        _rehash_report(relaxed_arm)
        relaxed_arm_path = root / "relaxed-arm-report.json"
        _write_json(relaxed_arm_path, relaxed_arm)

        relaxed_row = copy.deepcopy(self.report)
        relaxed_row["arms"][0]["diagnostic"]["telemetry"]["per_net"].pop()
        _rehash_report(relaxed_row)
        relaxed_row_path = root / "relaxed-row-report.json"
        _write_json(relaxed_row_path, relaxed_row)

        combined = copy.deepcopy(self.report)
        combined["source_envelope_checksum"] ^= 1
        combined["arms"][0]["raw_semantic_checksum"] ^= 1
        combined_path = root / "combined-source-semantic-report.json"
        _write_json(combined_path, combined)

        cases = (
            ("duplicate", duplicate, "duplicate"),
            ("noncanonical", noncanonical, "canonical one-line JSON"),
            ("corrupt", corrupt, "cannot read per-net report"),
            ("depth", deep, "64-level nesting bound"),
            ("oversized", oversized, "exceeds"),
            ("cross-commit", cross_commit_path, "same clean"),
            ("dirty", dirty_path, "same clean"),
            ("forged-source", forged_source_path, "source envelope"),
            ("forged-artifact", forged_artifact_path, "artifact checksum"),
            ("arm-cardinality", relaxed_arm_path, "exactly baseline then candidate"),
            ("row-cardinality", relaxed_row_path, "differs from workload count"),
            ("combined-source-first", combined_path, "source envelope"),
        )
        for label, report_path, expected_error in cases:
            with self.subTest(label=label):
                rejected = self._run_named_validator(
                    self.raw_path,
                    self.sidecar_path,
                    report_path,
                )
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)

    def test_rehashed_deep_sidecar_and_report_association_mutations_reject(self) -> None:
        sidecar_mutations = {
            "controller": lambda value: value.__setitem__(
                "raw_controller_identity",
                value["raw_controller_identity"] ^ 1,
            ),
            "environment": lambda value: value.__setitem__(
                "raw_environment_checksum",
                value["raw_environment_checksum"] ^ 1,
            ),
            "pair": lambda value: value["attempts"][0].__setitem__(
                "associated_raw_pair_attempt_checksum",
                value["attempts"][0]["associated_raw_pair_attempt_checksum"] ^ 1,
            ),
            "process": lambda value: value["attempts"][0]["candidate"].__setitem__(
                "process_instance_identity",
                value["attempts"][0]["candidate"]["process_instance_identity"] ^ 1,
            ),
            "dispatch": lambda value: value["attempts"][0]["candidate"].__setitem__(
                "dispatch_ordinal",
                value["attempts"][0]["candidate"]["dispatch_ordinal"] ^ 1,
            ),
            "outcome": lambda value: value["attempts"][0]["candidate"]["telemetry"][
                "outcome"
            ].__setitem__(
                "total_intrinsic_cost",
                value["attempts"][0]["candidate"]["telemetry"]["outcome"]["total_intrinsic_cost"]
                + 1,
            ),
            "column-partition": lambda value: value["attempts"][0]["candidate"]["telemetry"][
                "per_net"
            ][0]["columns"].__setitem__(
                "requested_columns",
                value["attempts"][0]["candidate"]["telemetry"]["per_net"][0]["columns"][
                    "requested_columns"
                ]
                + 1,
            ),
            "roster": lambda value: value["attempts"][0]["candidate"]["telemetry"]["per_net"][0][
                "net"
            ].__setitem__(
                "generation",
                value["attempts"][0]["candidate"]["telemetry"]["per_net"][0]["net"]["generation"]
                + 1,
            ),
        }
        for label, mutate in sidecar_mutations.items():
            with self.subTest(companion="sidecar", field=label):
                sidecar = copy.deepcopy(self.sidecar)
                mutate(sidecar)
                _rehash_sidecar(sidecar)
                with self.assertRaises(raw_validator.EvidenceError):
                    session_v5_report.validate_join(
                        self.raw,
                        sidecar,
                        self.report,
                        expected_commit=_SOURCE_COMMIT,
                    )

        report_mutations = {
            "cell-plan": lambda value: value.__setitem__(
                "raw_cell_plan_checksum",
                value["raw_cell_plan_checksum"] ^ 1,
            ),
            "raw-artifact": lambda value: value.__setitem__(
                "raw_cell_artifact_checksum",
                value["raw_cell_artifact_checksum"] ^ 1,
            ),
            "raw-source": lambda value: value.__setitem__(
                "raw_source_envelope_checksum",
                value["raw_source_envelope_checksum"] ^ 1,
            ),
            "pair": lambda value: value["raw_reference"].__setitem__(
                "pair_attempt_checksum",
                value["raw_reference"]["pair_attempt_checksum"] ^ 1,
            ),
            "arm-semantic": lambda value: value["arms"][0].__setitem__(
                "raw_semantic_checksum",
                value["arms"][0]["raw_semantic_checksum"] ^ 1,
            ),
            "roster": lambda value: value.__setitem__(
                "workload_net_roster_checksum",
                value["workload_net_roster_checksum"] ^ 1,
            ),
            "entity": lambda value: value["arms"][0]["diagnostic"]["telemetry"]["per_net"][0][
                "net"
            ].__setitem__(
                "generation",
                value["arms"][0]["diagnostic"]["telemetry"]["per_net"][0]["net"]["generation"] + 1,
            ),
        }
        for label, mutate in report_mutations.items():
            with self.subTest(companion="report", field=label):
                report = copy.deepcopy(self.report)
                mutate(report)
                _rehash_report(report)
                with self.assertRaises(raw_validator.EvidenceError):
                    session_v5_report.validate_join(
                        self.raw,
                        self.sidecar,
                        report,
                        expected_commit=_SOURCE_COMMIT,
                    )

    def test_predecessor_successor_and_carrier_authorities_cross_reject(self) -> None:
        predecessor_report.validate_join(
            self.predecessor_raw,
            self.predecessor_sidecar,
            self.predecessor_report,
            expected_commit=_SOURCE_COMMIT,
        )
        h2250_report.validate_join(
            self.h2250_raw,
            self.h2250_sidecar,
            self.h2250_report,
            expected_commit=_SOURCE_COMMIT,
        )
        ordinary_session_v5_report.validate_join(
            self.ordinary_raw,
            self.ordinary_report,
            expected_commit=_SOURCE_COMMIT,
        )

        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                self.predecessor_raw,
                self.predecessor_sidecar,
                self.predecessor_report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            predecessor_report.validate_join(
                self.raw,
                self.sidecar,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )
        mixed_predecessor_successor_joins = (
            (
                "v3-raw-v2-sidecar",
                session_v5_report.validate_join,
                self.raw,
                self.predecessor_sidecar,
                self.report,
            ),
            (
                "v3-raw-v2-report",
                session_v5_report.validate_join,
                self.raw,
                self.sidecar,
                self.predecessor_report,
            ),
            (
                "v2-raw-v3-sidecar",
                predecessor_report.validate_join,
                self.predecessor_raw,
                self.sidecar,
                self.predecessor_report,
            ),
            (
                "v2-raw-v3-report",
                predecessor_report.validate_join,
                self.predecessor_raw,
                self.predecessor_sidecar,
                self.report,
            ),
        )
        for label, validator, raw, sidecar, report in mixed_predecessor_successor_joins:
            with self.subTest(mixed_authority=label):
                with self.assertRaises(raw_validator.EvidenceError):
                    validator(
                        raw,
                        sidecar,
                        report,
                        expected_commit=_SOURCE_COMMIT,
                    )
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                self.h2250_raw,
                self.h2250_sidecar,
                self.h2250_report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            h2250_report.validate_join(
                self.raw,
                self.sidecar,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                self.ordinary_raw,
                self.sidecar,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            ordinary_session_v5_report.validate_join(
                self.raw,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

    def test_missing_fields_and_non_u64_source_checksums_reject_before_deep_join(self) -> None:
        for companion_name, source, validator in (
            (
                "sidecar",
                self.sidecar,
                lambda value: session_v5_report._require_sidecar_source_envelope(
                    self.raw,
                    value,
                    expected_commit=_SOURCE_COMMIT,
                ),
            ),
            (
                "report",
                self.report,
                lambda value: session_v5_report._require_report_source_envelope(
                    self.raw,
                    value,
                    expected_commit=_SOURCE_COMMIT,
                ),
            ),
        ):
            for field in (
                "source_commit",
                "source_stamped",
                "source_tree_dirty",
                "source_envelope_checksum",
                "artifact_checksum",
            ):
                with self.subTest(companion=companion_name, missing=field):
                    value = copy.deepcopy(source)
                    del value[field]
                    with self.assertRaisesRegex(raw_validator.EvidenceError, f"missing {field}"):
                        validator(value)
            for field in ("source_envelope_checksum", "artifact_checksum"):
                for invalid in (True, 0, -1, 1 << 64, "1"):
                    with self.subTest(companion=companion_name, field=field, invalid=invalid):
                        value = copy.deepcopy(source)
                        value[field] = invalid
                        with self.assertRaisesRegex(raw_validator.EvidenceError, "unsigned 64-bit"):
                            validator(value)


if __name__ == "__main__":
    unittest.main()
