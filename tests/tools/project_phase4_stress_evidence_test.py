"""Adversarial tests for Phase 4 Stress Evidence v1."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.tools import project_phase4_fixed_query_controls_test as authority_test
from tools import project_phase4_operational_evidence as operational_projector
from tools import project_phase4_stress_evidence as projector

_COMMIT = "a" * 40
_DEFAULT_LIMITS = {
    "maximum_nets": 4096,
    "maximum_compiled_nodes": 100_000_000,
    "maximum_compiled_host_bytes": 8 * 1024 * 1024 * 1024,
    "maximum_active_regions": 250_000,
    "maximum_board_entities": 100_000,
}


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _probe_text() -> str:
    completed = subprocess.run(
        [
            str(_runfile("phase4_stress_work_bound_test_probe")),
            f"--runtime-commit={_COMMIT}",
            "--testing-allow-unstamped",
        ],
        check=False,
        text=True,
        capture_output=True,
        timeout=30,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr)
    if completed.stderr:
        raise RuntimeError(completed.stderr)
    return completed.stdout


def _authorities() -> tuple[dict[str, object], ...]:
    raw = authority_test._raw(3000, 1024, 4, corpus_limits=_DEFAULT_LIMITS)
    report = authority_test._report(raw)
    operational = operational_projector.project_document(raw)
    probe = json.loads(_probe_text())
    projector.validate_authorities(raw, report, operational, probe, expected_commit=_COMMIT)
    return raw, report, operational, probe


def _reauthenticate_probe(probe: dict[str, object]) -> None:
    probe["artifact_checksum"] = projector.compute_probe_artifact_checksum(probe)
    probe["source_envelope_checksum"] = projector.compute_probe_source_envelope_checksum(probe)


def _reauthenticate_artifact(artifact: dict[str, object]) -> None:
    artifact["artifact_checksum"] = projector.compute_artifact_checksum(artifact)
    artifact["source_envelope_checksum"] = projector.compute_source_envelope_checksum(artifact)


class Phase4StressEvidenceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw, cls.report, cls.operational, cls.probe = _authorities()
        cls.artifact = projector.project_document(cls.raw, cls.report, cls.operational, cls.probe)

    def test_accepts_full_ladder_without_overstating_bounded_tiers(self) -> None:
        projector.validate_artifact(
            self.raw,
            self.report,
            self.operational,
            self.probe,
            self.artifact,
            expected_commit=_COMMIT,
        )
        self.assertTrue(self.artifact["stress_ladder_coverage_complete"])
        self.assertFalse(self.artifact["standalone_decision_eligible"])
        self.assertFalse(self.artifact["phase4_complete"])
        self.assertFalse(self.artifact["declared_target_fully_supported"])
        self.assertEqual(self.artifact["maximum_full_raw_success_net_count"], 1024)
        first, second, third = self.artifact["cases"]
        self.assertTrue(first["full_workload_materialized"])
        self.assertTrue(first["candidate_preparation_reached"])
        self.assertTrue(first["allocator_reached"])
        self.assertEqual(len(first["execution_measurement"]["repetitions"]), 20)
        self.assertEqual(
            {row["scope"] for row in first["execution_measurement"]["process_lifetime_peaks"]},
            {"long_lived_worker_including_warmup_and_all_repetitions"},
        )
        for row, count, first_net in ((second, 739, 1739), (third, 369, 1369)):
            self.assertFalse(row["full_workload_materialized"])
            self.assertFalse(row["candidate_preparation_reached"])
            self.assertFalse(row["allocator_reached"])
            self.assertEqual(row["work_bound"]["maximum_preparable_net_count"], count)
            self.assertEqual(row["work_bound"]["first_unpreparable_net"]["id"], first_net)
            self.assertEqual(
                row["work_bound"]["prefix_interpretation"],
                "capacity_derived_not_materialized_or_achieved",
            )
            self.assertEqual(
                row["execution_measurement"]["peak_host_memory"]["status"], "unavailable"
            )
            self.assertEqual(
                row["work_bound"]["host_bytes_kind"],
                "logical_compiled_estimate_not_peak_rss",
            )
        self.assertEqual(third["work_bound"]["limiting_work_bound"], "compiled_nodes")

    def test_probe_exactly_reproduces_frozen_capacity_arithmetic(self) -> None:
        first, second = self.probe["cases"]
        self.assertEqual(
            (
                first["required_compiled_nodes"],
                first["required_compiled_host_bytes"],
                first["per_net_compiled_nodes"],
                first["per_net_compiled_host_bytes"],
            ),
            (276879360, 6941540352, 135195, 3389424),
        )
        self.assertEqual(
            (
                second["required_compiled_nodes"],
                second["required_compiled_host_bytes"],
                second["per_net_compiled_nodes"],
                second["per_net_compiled_host_bytes"],
            ),
            (1107087360, 27747614720, 270285, 6774320),
        )
        self.assertEqual(first["limiting_work_bound"], 1)
        self.assertEqual(second["limiting_work_bound"], 1)
        self.assertFalse(first["full_workload_materialized"])
        self.assertFalse(second["full_workload_materialized"])

    def test_rejects_rehashed_probe_semantic_and_source_drift(self) -> None:
        mutations = (
            lambda value: value["limits"].__setitem__("maximum_compiled_nodes", 100000001),
            lambda value: value["cases"][0].__setitem__("maximum_preparable_net_count", 740),
            lambda value: value["cases"][0]["first_unpreparable_net"].__setitem__("id", 1740),
            lambda value: value["cases"][1].__setitem__("limiting_work_bound", 3),
            lambda value: value["cases"][1].__setitem__("full_workload_materialized", True),
            lambda value: value["cases"][1].__setitem__("capacity_model_reached", True),
            lambda value: value["cases"][1].__setitem__("allocator_reached", True),
            lambda value: value.__setitem__("source_commit", "b" * 40),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(self.probe)
                mutate(changed)
                _reauthenticate_probe(changed)
                with self.assertRaises(projector.StressError):
                    projector.validate_probe(changed, expected_commit=_COMMIT)

    def test_rejects_rehashed_publication_overstatement_and_binding_drift(self) -> None:
        mutations = (
            lambda value: value.__setitem__("declared_target_fully_supported", True),
            lambda value: value.__setitem__("maximum_full_raw_success_net_count", 4096),
            lambda value: value["cases"][1].__setitem__("full_workload_materialized", True),
            lambda value: value["cases"][2].__setitem__("allocator_reached", True),
            lambda value: value["cases"][2]["execution_measurement"]["peak_host_memory"].update(
                {"status": "available", "reason": "0"}
            ),
            lambda value: value["cases"][1]["work_bound"].__setitem__(
                "prefix_interpretation", "achieved"
            ),
            lambda value: value["work_bound_probe_binding"].__setitem__("artifact_checksum", 1),
            lambda value: value["cases"][0]["source_binding"].__setitem__(
                "raw_artifact_checksum", 1
            ),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(self.artifact)
                mutate(changed)
                _reauthenticate_artifact(changed)
                with self.assertRaises(projector.StressError):
                    projector.validate_artifact(
                        self.raw,
                        self.report,
                        self.operational,
                        self.probe,
                        changed,
                        expected_commit=_COMMIT,
                    )

    def test_rejects_raw_probe_limit_or_source_mismatch(self) -> None:
        for field, value in (
            ("maximum_active_regions", 249999),
            ("maximum_board_entities", 99999),
        ):
            with self.subTest(field=field):
                changed = copy.deepcopy(self.probe)
                changed["limits"][field] = value
                _reauthenticate_probe(changed)
                with self.assertRaises(projector.StressError):
                    projector.project_document(self.raw, self.report, self.operational, changed)

    def test_strict_readers_reject_noncanonical_and_bounded_inputs(self) -> None:
        for label, value, reader in (
            (
                "probe",
                self.probe,
                lambda path: projector.read_probe(path, expected_commit=_COMMIT),
            ),
            ("artifact", self.artifact, projector.read_artifact),
        ):
            canonical = _canonical(value)
            malformed = {
                "duplicate": canonical.replace(
                    '{"schema_version":1', '{"schema_version":1,"schema_version":1', 1
                ),
                "bool": canonical.replace('"case_id":3001', '"case_id":true', 1),
                "float": canonical.replace('"case_id":3001', '"case_id":3001.0', 1),
                "nan": canonical.replace('"case_id":3001', '"case_id":NaN', 1),
                "bom": "\ufeff" + canonical,
                "missing_lf": canonical[:-1],
                "extra_lf": canonical + "\n",
            }
            reordered = copy.deepcopy(value)
            reordered["source_commit"] = reordered.pop("source_commit")
            malformed["order"] = _canonical(reordered)
            nested: object = 0
            for _ in range(70):
                nested = [nested]
            malformed["nesting"] = _canonical(nested)
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory)
                for name, text in malformed.items():
                    with self.subTest(label=label, name=name):
                        path = root / f"{name}.json"
                        path.write_text(text, encoding="utf-8")
                        with self.assertRaises(projector.StressError):
                            parsed = reader(path)
                            if label == "artifact":
                                projector.validate_artifact(
                                    self.raw,
                                    self.report,
                                    self.operational,
                                    self.probe,
                                    parsed,
                                    expected_commit=_COMMIT,
                                )
                maximum = (
                    projector._MAXIMUM_PROBE_BYTES
                    if label == "probe"
                    else projector._MAXIMUM_ARTIFACT_BYTES
                )
                oversized = root / "oversized.json"
                oversized.write_bytes(b" " * (maximum + 1))
                with self.assertRaisesRegex(projector.StressError, "input bound"):
                    reader(oversized)

    def test_cli_projects_deterministically_and_validates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            paths = {}
            for name, document in (
                ("raw-3000", self.raw),
                ("report-3000", self.report),
                ("operational-3000", self.operational),
                ("work-bound-probe", self.probe),
            ):
                path = root / f"{name}.json"
                path.write_text(_canonical(document), encoding="utf-8")
                paths[name] = path
            command = [
                str(_runfile("phase4_stress_evidence")),
                "--expected-commit",
                _COMMIT,
            ]
            for name, path in paths.items():
                command.extend((f"--{name}", str(path)))
            first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=30)
            second = subprocess.run(
                command, check=False, text=True, capture_output=True, timeout=30
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stderr, "")
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(first.stdout, _canonical(self.artifact))
            artifact_path = root / "stress.json"
            artifact_path.write_text(first.stdout, encoding="utf-8")
            validated = subprocess.run(
                [*command, "--validate", str(artifact_path)],
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(validated.returncode, 0, validated.stderr)
            self.assertIn("validated one", validated.stdout)


if __name__ == "__main__":
    unittest.main()
