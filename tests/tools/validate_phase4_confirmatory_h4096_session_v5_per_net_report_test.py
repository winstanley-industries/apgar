"""Synthetic tests for the Session-v5 H=4096 ordinary per-net report join."""

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
from tools import validate_phase4_confirmatory_h4096_per_net_report as predecessor_report
from tools import (
    validate_phase4_confirmatory_h4096_session_v5_per_net_report as session_v5_report,
)
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_SOURCE_COMMIT = "a" * 40
_CANONICAL_ALGORITHM_BUDGET = 7657176792159702821
_PREDECESSOR_PAIRED_BUDGET = 12108149041077564710


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


def _write_json(path: pathlib.Path, value: object) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )


def _write_oversized(path: pathlib.Path, maximum_bytes: int) -> None:
    with path.open("wb") as stream:
        stream.seek(maximum_bytes)
        stream.write(b"x")


def _rehash_report(report: dict[str, Any]) -> None:
    report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(report)
    report["source_envelope_checksum"] = report_validator.compute_report_source_envelope_checksum(
        report
    )


def _rehash_raw(raw: dict[str, Any], *, rehash_plan: bool = False) -> None:
    if rehash_plan:
        raw["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
            raw,
            corpus_version=2,
        )
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _rehash_with_paired_budget(raw: dict[str, Any], budget_checksum: int) -> None:
    """Make one internally checksummed Raw artifact carry a foreign paired budget."""
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
    _rehash_raw(raw)


def _rehash_with_noncanonical_external_budget(raw: dict[str, Any]) -> None:
    """Make Raw self-consistent under a noncanonical paired-budget preimage."""
    external_budget = raw["config"]["external_budget"]
    external_budget["maximum_prepared_elapsed_nanoseconds"] -= 1
    _, cases, _ = raw_validator._frozen_confirmatory_representative_manifest()
    paired_budget = raw_validator.compute_canonical_budget_checksum(
        raw,
        cases[raw["config"]["case_id"]],
        _CANONICAL_ALGORITHM_BUDGET,
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
    _rehash_raw(raw, rehash_plan=True)


def _make_noncanonical_setup_deadline(raw: dict[str, Any]) -> None:
    raw["config"]["maximum_setup_elapsed_nanoseconds"] -= 1
    _rehash_raw(raw, rehash_plan=True)


def _make_all_failure(raw: dict[str, Any]) -> None:
    """Turn every pair into a checksummed pre-dispatch failure."""
    for pair in raw["attempts"]:
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
                    "controller_detail": (
                        "synthetic pre-dispatch launch failure" if failed else ""
                    ),
                    "record": None,
                    "child_failure": None,
                }
            )
            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        pair["result"] = None
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
    _rehash_raw(raw)


class Phase4ConfirmatoryH4096SessionV5PerNetReportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)

        cls.raw = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(cls.raw, _SOURCE_COMMIT)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = root / "session-v5-raw.json"
        cls.report_path = root / "session-v5-report.json"
        _write_json(cls.raw_path, cls.raw)
        _write_json(cls.report_path, cls.report)

        cls.predecessor_raw = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.predecessor_raw, _SOURCE_COMMIT)
        cls.predecessor_report = artifacts.make_report(cls.predecessor_raw)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def _run_named_validator(
        self,
        raw_path: pathlib.Path,
        report_path: pathlib.Path,
        *,
        expected_commit: str = _SOURCE_COMMIT,
    ) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                str(runfile("phase4_confirmatory_h4096_session_v5_per_net_report_validator")),
                f"--expected-commit={expected_commit}",
                f"--raw={raw_path}",
                f"--report={report_path}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )

    def test_canonical_synthetic_pair_validates_and_is_diagnostic_only(self) -> None:
        session_v5_report.validate_join(
            self.raw,
            self.report,
            expected_commit=_SOURCE_COMMIT,
        )
        self.assertFalse(self.report["decision_eligible"])
        self.assertEqual(self.report["raw_wire_schema_version"], 1)
        self.assertEqual([arm["arm"] for arm in self.report["arms"]], [0, 1])
        for arm in self.report["arms"]:
            self.assertEqual(len(arm["diagnostic"]["telemetry"]["per_net"]), 64)

        encoded = json.dumps(self.report, ensure_ascii=False, separators=(",", ":"))
        for forbidden in (
            "case_build_elapsed_nanoseconds",
            "prepared_elapsed_nanoseconds",
            "cold_elapsed_nanoseconds",
            "outer_elapsed_nanoseconds",
            "process_lifetime_peak_host_bytes",
        ):
            self.assertNotIn(f'"{forbidden}"', encoded)

    def test_named_validator_authenticates_raw_before_report_access(self) -> None:
        executable = str(runfile("phase4_confirmatory_h4096_session_v5_per_net_report_validator"))
        base = [
            executable,
            f"--expected-commit={_SOURCE_COMMIT}",
            f"--raw={self.raw_path}",
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

        foreign_sidecar = subprocess.run(
            [*base, "--telemetry=foreign.json"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(foreign_sidecar.returncode, 2)
        self.assertIn("unrecognized arguments", foreign_sidecar.stderr)

        fifo = pathlib.Path(self.temporary.name) / "poisoned-report.fifo"
        os.mkfifo(fifo)
        reached_report = subprocess.run(
            [*base[:3], f"--report={fifo}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(reached_report.returncode, 1)
        self.assertIn("regular file", reached_report.stderr)

        corrupt_raw = copy.deepcopy(self.raw)
        corrupt_raw["source_envelope_checksum"] ^= 1
        corrupt_path = pathlib.Path(self.temporary.name) / "corrupt-raw.json"
        _write_json(corrupt_path, corrupt_raw)
        rejected_before_report = subprocess.run(
            [
                executable,
                f"--expected-commit={_SOURCE_COMMIT}",
                f"--raw={corrupt_path}",
                f"--report={fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected_before_report.returncode, 1)
        self.assertIn("source_envelope_checksum", rejected_before_report.stderr)
        self.assertNotIn("regular file", rejected_before_report.stderr)

    def test_session_v5_postchecks_complete_before_report_access(self) -> None:
        root = pathlib.Path(self.temporary.name)
        fifo = root / "session-v5-postcheck-report.fifo"
        os.mkfifo(fifo)

        noncanonical_setup = copy.deepcopy(self.raw)
        _make_noncanonical_setup_deadline(noncanonical_setup)
        noncanonical_budget = copy.deepcopy(self.raw)
        _rehash_with_noncanonical_external_budget(noncanonical_budget)
        other_cell = artifacts.make_raw(
            10201,
            8,
            same_run=False,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(other_cell, _SOURCE_COMMIT)

        cases = (
            ("setup", noncanonical_setup, "setup deadline differs"),
            ("paired-preimage", noncanonical_budget, "paired semantic budget differs"),
            ("cell", other_cell, "outside the Protocol-v3 initial ordinary Raw authority"),
        )
        for label, raw, expected_error in cases:
            with self.subTest(label=label, surface="direct"):
                with self.assertRaisesRegex(raw_validator.EvidenceError, expected_error):
                    session_v5_report.validate_join(
                        raw,
                        self.report,
                        expected_commit=_SOURCE_COMMIT,
                    )

            raw_path = root / f"postcheck-{label}-raw.json"
            _write_json(raw_path, raw)
            with self.subTest(label=label, surface="named"):
                rejected = self._run_named_validator(raw_path, fifo)
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)
                self.assertNotIn("regular file", rejected.stderr)

    def test_named_raw_boundary_rejects_before_report_access(self) -> None:
        root = pathlib.Path(self.temporary.name)
        fifo = root / "invalid-raw-report.fifo"
        os.mkfifo(fifo)
        canonical = self.raw_path.read_text(encoding="utf-8")

        duplicate = root / "duplicate-raw.json"
        duplicate.write_text(
            canonical.replace(
                '{"schema_version":1,',
                '{"schema_version":1,"schema_version":1,',
                1,
            ),
            encoding="utf-8",
        )
        noncanonical = root / "noncanonical-raw.json"
        noncanonical.write_text(canonical + "\n", encoding="utf-8")
        corrupt = root / "corrupt-json-raw.json"
        corrupt.write_text("{\n", encoding="utf-8")
        deep = root / "deep-raw.json"
        deep.write_text("[" * 66 + "0" + "]" * 66 + "\n", encoding="utf-8")
        oversized = root / "oversized-raw.json"
        _write_oversized(oversized, raw_validator._MAXIMUM_RAW_JSON_BYTES)

        relaxed_cardinality = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=True,
            repetitions=1,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(relaxed_cardinality, _SOURCE_COMMIT)
        relaxed_cardinality_path = root / "relaxed-cardinality-raw.json"
        _write_json(relaxed_cardinality_path, relaxed_cardinality)

        dirty = copy.deepcopy(self.raw)
        dirty["source_stamped"] = False
        dirty["source_tree_dirty"] = True
        _rehash_raw(dirty)
        dirty_path = root / "dirty-raw.json"
        _write_json(dirty_path, dirty)

        cases = (
            ("duplicate", duplicate, _SOURCE_COMMIT, "duplicate JSON object key"),
            ("noncanonical", noncanonical, _SOURCE_COMMIT, "canonical one-line JSON"),
            ("corrupt", corrupt, _SOURCE_COMMIT, "cannot read raw cell"),
            ("depth", deep, _SOURCE_COMMIT, "nesting bound"),
            ("oversized", oversized, _SOURCE_COMMIT, "exceeds the"),
            (
                "cardinality",
                relaxed_cardinality_path,
                _SOURCE_COMMIT,
                "config.repetitions must be exactly 20",
            ),
            ("dirty-source", dirty_path, _SOURCE_COMMIT, "clean, stamped"),
            (
                "expected-commit",
                self.raw_path,
                "b" * 40,
                "source_commit does not match",
            ),
        )
        for label, raw_path, expected_commit, expected_error in cases:
            with self.subTest(label=label):
                rejected = self._run_named_validator(
                    raw_path,
                    fifo,
                    expected_commit=expected_commit,
                )
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)
                self.assertNotIn("regular file", rejected.stderr)

    def test_named_report_boundary_rejects_after_valid_raw(self) -> None:
        root = pathlib.Path(self.temporary.name)
        canonical = self.report_path.read_text(encoding="utf-8")

        duplicate = root / "duplicate-report.json"
        duplicate.write_text(
            canonical.replace(
                '{"schema_version":1,',
                '{"schema_version":1,"schema_version":1,',
                1,
            ),
            encoding="utf-8",
        )
        noncanonical = root / "noncanonical-report.json"
        noncanonical.write_text(canonical + "\n", encoding="utf-8")
        corrupt = root / "corrupt-json-report.json"
        corrupt.write_text("{\n", encoding="utf-8")
        deep = root / "deep-report.json"
        deep.write_text("[" * 66 + "0" + "]" * 66 + "\n", encoding="utf-8")
        oversized = root / "oversized-report.json"
        _write_oversized(oversized, report_validator._MAX_REPORT_BYTES)

        cross_commit = copy.deepcopy(self.report)
        cross_commit["source_commit"] = "b" * 40
        _rehash_report(cross_commit)
        cross_commit_path = root / "cross-commit-report.json"
        _write_json(cross_commit_path, cross_commit)

        dirty = copy.deepcopy(self.report)
        dirty["source_stamped"] = False
        dirty["source_tree_dirty"] = True
        _rehash_report(dirty)
        dirty_path = root / "dirty-report.json"
        _write_json(dirty_path, dirty)

        foreign_reference = copy.deepcopy(self.report)
        foreign_reference["raw_reference"]["pair_attempt_checksum"] ^= 1
        _rehash_report(foreign_reference)
        foreign_reference_path = root / "rehashed-alias-report.json"
        _write_json(foreign_reference_path, foreign_reference)

        forged_artifact = copy.deepcopy(self.report)
        forged_artifact["artifact_checksum"] ^= 1
        forged_artifact["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(forged_artifact)
        )
        forged_artifact_path = root / "forged-artifact-report.json"
        _write_json(forged_artifact_path, forged_artifact)

        forged_source_envelope = copy.deepcopy(self.report)
        forged_source_envelope["source_envelope_checksum"] ^= 1
        forged_source_envelope_path = root / "forged-source-envelope-report.json"
        _write_json(forged_source_envelope_path, forged_source_envelope)

        source_before_semantics = copy.deepcopy(self.report)
        source_before_semantics["source_envelope_checksum"] ^= 1
        arm = source_before_semantics["arms"][1]
        semantics = arm["diagnostic"]["semantics"]
        semantics["budget_checksum"] ^= 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        arm["raw_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry = arm["diagnostic"]["telemetry"]
        telemetry["associated_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        source_before_semantics_path = root / "source-before-semantics-report.json"
        _write_json(source_before_semantics_path, source_before_semantics)

        relaxed_cardinality = copy.deepcopy(self.report)
        telemetry = relaxed_cardinality["arms"][0]["diagnostic"]["telemetry"]
        telemetry["per_net"].pop()
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        _rehash_report(relaxed_cardinality)
        relaxed_cardinality_path = root / "relaxed-cardinality-report.json"
        _write_json(relaxed_cardinality_path, relaxed_cardinality)

        cases = (
            ("duplicate", duplicate, "duplicate JSON object key"),
            ("noncanonical", noncanonical, "canonical one-line JSON"),
            ("corrupt", corrupt, "cannot read per-net report"),
            ("depth", deep, "nesting bound"),
            ("oversized", oversized, "exceeds the"),
            ("cross-commit", cross_commit_path, "same clean independently expected commit"),
            ("dirty-source", dirty_path, "same clean independently expected commit"),
            ("rehashed-alias", foreign_reference_path, "raw reference"),
            ("artifact-checksum", forged_artifact_path, "artifact checksum"),
            (
                "source-envelope-checksum",
                forged_source_envelope_path,
                "source envelope does not authenticate",
            ),
            (
                "source-before-semantics",
                source_before_semantics_path,
                "source envelope does not authenticate",
            ),
            ("cardinality", relaxed_cardinality_path, "differs from workload count"),
        )
        for label, report_path, expected_error in cases:
            with self.subTest(label=label):
                rejected = self._run_named_validator(self.raw_path, report_path)
                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                self.assertIn(expected_error, rejected.stderr)

    def test_all_failure_raw_rejects_before_report_access(self) -> None:
        all_failure = copy.deepcopy(self.raw)
        _make_all_failure(all_failure)
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                all_failure,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

        root = pathlib.Path(self.temporary.name)
        all_failure_path = root / "all-failure-raw.json"
        _write_json(all_failure_path, all_failure)
        fifo = root / "all-failure-report.fifo"
        os.mkfifo(fifo)
        rejected = subprocess.run(
            [
                str(runfile("phase4_confirmatory_h4096_session_v5_per_net_report_validator")),
                f"--expected-commit={_SOURCE_COMMIT}",
                f"--raw={all_failure_path}",
                f"--report={fifo}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(rejected.returncode, 1)
        self.assertNotIn("regular file", rejected.stderr)

    def test_report_source_identity_precedes_semantic_join(self) -> None:
        foreign = copy.deepcopy(self.report)
        foreign["source_commit"] = "b" * 40
        foreign["raw_reference"]["pair_attempt_checksum"] ^= 1
        _rehash_report(foreign)
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "same clean independently expected commit",
        ):
            session_v5_report.validate_join(
                self.raw,
                foreign,
                expected_commit=_SOURCE_COMMIT,
            )

        corrupt_envelope_and_semantics = copy.deepcopy(self.report)
        corrupt_envelope_and_semantics["source_envelope_checksum"] ^= 1
        semantics = corrupt_envelope_and_semantics["arms"][1]["diagnostic"]["semantics"]
        semantics["budget_checksum"] ^= 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "source envelope does not authenticate",
        ):
            session_v5_report.validate_join(
                self.raw,
                corrupt_envelope_and_semantics,
                expected_commit=_SOURCE_COMMIT,
            )

    def test_predecessor_and_successor_substitutions_cross_reject(self) -> None:
        predecessor_report.validate_join(
            self.predecessor_raw,
            self.predecessor_report,
            expected_commit=_SOURCE_COMMIT,
        )
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                self.predecessor_raw,
                self.predecessor_report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            predecessor_report.validate_join(
                self.raw,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

    def test_isolated_wire_carrier_and_budget_substitutions_reject(self) -> None:
        foreign_wire = copy.deepcopy(self.raw)
        foreign_wire["wire_schema_version"] = 2
        _rehash_raw(foreign_wire, rehash_plan=True)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "wire_schema_version must be 1"):
            session_v5_report.validate_join(
                foreign_wire,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

        same_run_carrier = artifacts.make_raw(
            10200,
            8,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
            session_v5=True,
        )
        artifacts.mark_raw_clean(same_run_carrier, _SOURCE_COMMIT)
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                same_run_carrier,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

        for label, foreign_budget in (
            ("predecessor-paired", _PREDECESSOR_PAIRED_BUDGET),
            ("canonical-not-paired", _CANONICAL_ALGORITHM_BUDGET),
        ):
            forged = copy.deepcopy(self.raw)
            _rehash_with_paired_budget(forged, foreign_budget)
            with self.subTest(label=label):
                with self.assertRaisesRegex(
                    raw_validator.EvidenceError,
                    "another command or cell",
                ):
                    session_v5_report.validate_join(
                        forged,
                        artifacts.make_report(forged),
                        expected_commit=_SOURCE_COMMIT,
                    )

        # Each Raw authority remains live, but neither authority accepts the
        # other Session's report payload as its companion.
        with self.assertRaises(raw_validator.EvidenceError):
            session_v5_report.validate_join(
                self.raw,
                self.predecessor_report,
                expected_commit=_SOURCE_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            predecessor_report.validate_join(
                self.predecessor_raw,
                self.report,
                expected_commit=_SOURCE_COMMIT,
            )

    def test_rehashed_deep_join_mutations_reject(self) -> None:
        foreign_reference = copy.deepcopy(self.report)
        foreign_reference["raw_reference"]["pair_attempt_checksum"] ^= 1
        _rehash_report(foreign_reference)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "raw reference"):
            session_v5_report.validate_join(
                self.raw,
                foreign_reference,
                expected_commit=_SOURCE_COMMIT,
            )

        foreign_semantics = copy.deepcopy(self.report)
        arm = foreign_semantics["arms"][1]
        semantics = arm["diagnostic"]["semantics"]
        semantics["budget_checksum"] ^= 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        arm["raw_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry = arm["diagnostic"]["telemetry"]
        telemetry["associated_semantic_checksum"] = semantics["semantic_checksum"]
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        _rehash_report(foreign_semantics)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "complete semantics differ"):
            session_v5_report.validate_join(
                self.raw,
                foreign_semantics,
                expected_commit=_SOURCE_COMMIT,
            )

        foreign_roster = copy.deepcopy(self.report)
        telemetry = foreign_roster["arms"][0]["diagnostic"]["telemetry"]
        telemetry["per_net"][0]["net"]["id"] ^= 1
        telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
        _rehash_report(foreign_roster)
        with self.assertRaisesRegex(raw_validator.EvidenceError, "EntityRef roster"):
            session_v5_report.validate_join(
                self.raw,
                foreign_roster,
                expected_commit=_SOURCE_COMMIT,
            )


if __name__ == "__main__":
    unittest.main()
