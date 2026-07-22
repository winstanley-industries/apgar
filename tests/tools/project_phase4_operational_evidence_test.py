"""Adversarial tests for Phase 4 Operational Projection v1."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.tools import validate_phase4_raw_evidence_test as raw_test
from tools import project_phase4_operational_evidence as projector
from tools import validate_phase4_raw_evidence as raw_validator

_COMMIT = "a" * 40


def _raw() -> dict[str, object]:
    result = raw_test._artifact(20)
    raw_validator.validate_document(result, expected_commit=_COMMIT)
    return result


def _reauthenticate(document: dict[str, object]) -> None:
    document["artifact_checksum"] = projector.compute_artifact_checksum(document)
    document["source_envelope_checksum"] = projector.compute_source_envelope_checksum(document)


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


class Phase4OperationalProjectionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.raw = _raw()
        cls.projection = projector.project_document(cls.raw)

    def test_accepts_exact_projection_and_exposes_only_truthful_fields(self) -> None:
        projector.validate_projection(self.raw, self.projection)
        self.assertTrue(self.projection["eligible_input_to_statistics"])
        self.assertFalse(self.projection["standalone_decision_eligible"])
        self.assertFalse(self.projection["coverage_complete"])
        self.assertFalse(self.projection["phase4_telemetry_complete"])
        self.assertEqual(len(self.projection["repetitions"]), 20)
        self.assertEqual(
            [row["execution_order"] for row in self.projection["repetitions"]].count(0), 10
        )
        self.assertEqual(
            [row["execution_order"] for row in self.projection["repetitions"]].count(1), 10
        )
        self.assertNotIn("totals", self.projection)
        self.assertFalse(
            any(
                value is True and "decision_eligible" in key
                for key, value in self.projection.items()
            )
        )
        availability = self.projection["measurement_availability"]
        self.assertEqual(availability["cpu_utilization"]["status"], "unavailable")
        self.assertEqual(availability["gpu_utilization"]["status"], "not_applicable")
        self.assertEqual(availability["device_memory"]["status"], "not_applicable")
        self.assertEqual(availability["batch_fill"]["status"], "unavailable")
        self.assertEqual(availability["prepared_view_cache_measurement"]["status"], "unavailable")
        self.assertEqual(
            availability["complete_toolchain_hardware_provenance"]["status"], "unavailable"
        )
        self.assertEqual(
            self.projection["execution_model"]["prepared_view_execution"],
            "precompiled_direct_per_net_no_runtime_cache",
        )
        self.assertEqual(
            {process["peak_measurement_scope"] for process in self.projection["processes"]},
            {"long_lived_worker_including_warmup_and_all_repetitions"},
        )
        self.assertEqual(
            sum(
                "process_lifetime_peak_host_bytes" in process
                for process in self.projection["processes"]
            ),
            2,
        )
        self.assertFalse(
            any(
                "process_lifetime_peak_host_bytes" in row[arm]
                for row in self.projection["repetitions"]
                for arm in ("baseline", "candidate")
            )
        )

    def test_binds_every_pair_and_arm_authentication_layer_after_rehash(self) -> None:
        for repetition in range(20):
            for field in (
                "pair_attempt_checksum",
                "paired_semantic_checksum",
                "paired_artifact_checksum",
            ):
                with self.subTest(repetition=repetition, field=field):
                    changed = copy.deepcopy(self.projection)
                    changed["repetitions"][repetition][field] ^= 1
                    _reauthenticate(changed)
                    with self.assertRaises(projector.ProjectionError):
                        projector.validate_projection(self.raw, changed)
            for arm in ("baseline", "candidate"):
                for field in (
                    "attempt_checksum",
                    "semantic_checksum",
                    "record_artifact_checksum",
                    "authority_checksum",
                ):
                    with self.subTest(repetition=repetition, arm=arm, field=field):
                        changed = copy.deepcopy(self.projection)
                        changed["repetitions"][repetition][arm][field] ^= 1
                        _reauthenticate(changed)
                        with self.assertRaises(projector.ProjectionError):
                            projector.validate_projection(self.raw, changed)

    def test_rejects_every_root_join_mutation_after_rehash(self) -> None:
        mutations = (
            lambda value: value.__setitem__("source_commit", "b" * 40),
            lambda value: value["raw_binding"].__setitem__("raw_wire_schema_version", 2),
            lambda value: value["raw_binding"]["config"].__setitem__("requested_pool_size", 8),
            lambda value: value["raw_binding"].__setitem__("corpus_checksum", 1),
            lambda value: value["raw_binding"].__setitem__("cell_plan_checksum", 1),
            lambda value: value["raw_binding"].__setitem__("environment_checksum", 1),
            lambda value: value["raw_binding"].__setitem__("authority_run_identity", 1),
            lambda value: value["raw_binding"].__setitem__("controller_identity", 1),
            lambda value: value["raw_binding"].__setitem__("raw_artifact_checksum", 1),
            lambda value: value["raw_binding"].__setitem__("raw_source_envelope_checksum", 1),
            lambda value: value["cell_identity"].__setitem__("board_content_hash", 1),
            lambda value: value["host_environment"].__setitem__("host_kernel", "other"),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(self.projection)
                mutate(changed)
                _reauthenticate(changed)
                with self.assertRaises(projector.ProjectionError):
                    projector.validate_projection(self.raw, changed)

    def test_rejects_order_process_lifecycle_peak_timing_and_accounting_drift(self) -> None:
        mutations = (
            lambda value: value["repetitions"][0].__setitem__("repetition_index", 1),
            lambda value: value["repetitions"][0].__setitem__("execution_order", 1),
            lambda value: value["repetitions"][0]["baseline"].__setitem__("arm", 1),
            lambda value: value["repetitions"][0]["baseline"].__setitem__(
                "process_instance_identity", 2002
            ),
            lambda value: value["repetitions"][1]["candidate"]["preparer_lifecycle"].__setitem__(
                "invocations_started_before", 99
            ),
            lambda value: value["processes"][0].__setitem__(
                "process_lifetime_peak_host_bytes", 999
            ),
            lambda value: value["processes"][1]["configured_external_caps"].__setitem__(
                "wall_limit_nanoseconds", 999
            ),
            lambda value: value["repetitions"][0]["baseline"]["timing_nanoseconds"].__setitem__(
                "cold_residual", 99
            ),
            lambda value: value["repetitions"][0]["candidate"]["opportunity"].__setitem__(
                "route_queries", 99
            ),
            lambda value: value["repetitions"][0]["candidate"]["preparation"].__setitem__(
                "route_work_units", 99
            ),
            lambda value: value["repetitions"][0]["candidate"]["candidate_accounting"].__setitem__(
                "admitted", 99
            ),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                changed = copy.deepcopy(self.projection)
                mutate(changed)
                _reauthenticate(changed)
                with self.assertRaises(projector.ProjectionError):
                    projector.validate_projection(self.raw, changed)

    def test_rejects_later_raw_corruption_before_projection(self) -> None:
        changed = copy.deepcopy(self.raw)
        semantics = changed["attempts"][19]["candidate"]["record"]["semantics"]
        semantics["actual"]["route_work_units"] += 1
        semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
        changed["attempts"][19]["candidate"]["record"]["external_observation"][
            "associated_semantic_checksum"
        ] = semantics["semantic_checksum"]
        raw_test._refresh_pair(changed, 19)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "raw.json"
            path.write_text(_canonical(changed), encoding="utf-8")
            with self.assertRaises(raw_validator.EvidenceError):
                raw_validator.read_validated_publication_document(path, expected_commit=_COMMIT)
            completed = subprocess.run(
                [
                    str(_runfile("phase4_operational_projection")),
                    "--raw",
                    str(path),
                    "--expected-commit",
                    _COMMIT,
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(completed.stdout, "")
            self.assertIn("attempts[19].candidate.record.semantics", completed.stderr)

    def test_strict_reader_rejects_bounds_duplicates_types_order_and_nesting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            valid = _canonical(self.projection)
            cases = {
                "duplicate": valid.replace(
                    '{"schema_version":1', '{"schema_version":1,"schema_version":1', 1
                ),
                "bool_integer": valid.replace('"case_id":100', '"case_id":true', 1),
                "float_integer": valid.replace('"case_id":100', '"case_id":100.0', 1),
                "negative_integer": valid.replace('"case_id":100', '"case_id":-1', 1),
                "too_large_integer": valid.replace(
                    '"case_id":100', '"case_id":18446744073709551616', 1
                ),
                "nan": valid.replace('"case_id":100', '"case_id":NaN', 1),
                "bom": "\ufeff" + valid,
                "missing_lf": valid[:-1],
                "extra_lf": valid + "\n",
            }
            reordered = copy.deepcopy(self.projection)
            reordered["raw_binding"] = {
                key: reordered["raw_binding"][key]
                for key in reversed(tuple(reordered["raw_binding"].keys()))
            }
            cases["field_order"] = _canonical(reordered)
            nested: object = 0
            for _ in range(70):
                nested = [nested]
            cases["nesting"] = _canonical(nested)
            for name, encoded in cases.items():
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    path.write_text(encoded, encoding="utf-8")
                    with self.assertRaises(projector.ProjectionError):
                        value = projector.read_projection(path)
                        projector.validate_projection(self.raw, value)
            oversized = root / "oversized.json"
            oversized.write_bytes(b" " * (projector._MAXIMUM_PROJECTION_BYTES + 1))
            with self.assertRaisesRegex(projector.ProjectionError, "input bound"):
                projector.read_projection(oversized)

    def test_cli_projects_deterministically_then_validates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            raw_path = root / "raw.json"
            projection_path = root / "projection.json"
            raw_path.write_text(_canonical(self.raw), encoding="utf-8")
            command = [
                str(_runfile("phase4_operational_projection")),
                "--raw",
                str(raw_path),
                "--expected-commit",
                _COMMIT,
            ]
            first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=10)
            second = subprocess.run(
                command, check=False, text=True, capture_output=True, timeout=10
            )
            self.assertEqual(first.returncode, 0, first.stderr)
            self.assertEqual(first.stderr, "")
            self.assertEqual(first.stdout, second.stdout)
            self.assertEqual(first.stdout, _canonical(self.projection))
            projection_path.write_text(first.stdout, encoding="utf-8")
            validated = subprocess.run(
                [*command, "--validate", str(projection_path)],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(validated.returncode, 0, validated.stderr)
            self.assertIn("validated one", validated.stdout)

    def test_cli_rejects_projection_larger_than_its_own_input_bound(self) -> None:
        changed = copy.deepcopy(self.raw)
        changed["environment"]["compiler_identity"] = "x" * (
            projector._MAXIMUM_PROJECTION_BYTES + 1
        )
        changed["environment"]["environment_checksum"] = raw_validator.compute_environment_checksum(
            changed["environment"]
        )
        changed["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(changed)
        changed["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            changed
        )
        raw_validator.validate_document(changed, expected_commit=_COMMIT)
        with tempfile.TemporaryDirectory() as directory:
            raw_path = pathlib.Path(directory) / "raw.json"
            raw_path.write_text(_canonical(changed), encoding="utf-8")
            completed = subprocess.run(
                [
                    str(_runfile("phase4_operational_projection")),
                    "--raw",
                    str(raw_path),
                    "--expected-commit",
                    _COMMIT,
                ],
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(completed.stdout, "")
            self.assertIn("output bound", completed.stderr)


if __name__ == "__main__":
    unittest.main()
