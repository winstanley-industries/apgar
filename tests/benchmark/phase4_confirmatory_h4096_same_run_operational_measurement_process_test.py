"""Acquisition-free process coverage for the H4096 same-run operational join."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import phase4_confirmatory_h4096_same_run_operational_authority as authority
from tools import (
    validate_phase4_confirmatory_h4096_same_run_operational_measurement as publisher,
)
from tools import (
    validate_phase4_confirmatory_same_run_operational_measurement as h2250_publisher,
)
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_COMMIT = "a" * 40


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def capture_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_h4096_same_run_operational_capture"
        if production
        else "phase4_confirmatory_h4096_same_run_operational_capture_test"
    )
    return [
        str(runfile(executable)),
        "--corpus-version=2",
        "--raw-wire-schema-version=2",
        "--case-id=10100",
        "--pool-size=4",
        "--workers=4",
        "--setup-ns=300000000000",
        "--prepared-ns=300000000000",
        "--cold-ns=300000000000",
        "--address-space-bytes=68719476736",
        "--peak-host-bytes=17179869184",
        "--maximum-nets=4096",
        "--maximum-compiled-nodes=100000000",
        "--maximum-compiled-host-bytes=8589934592",
        "--maximum-active-regions=250000",
        "--maximum-board-entities=100000",
        "--testing-allow-unstamped",
        f"--apgar-commit={_COMMIT}",
    ]


def publisher_command(
    raw: pathlib.Path,
    sidecar: pathlib.Path,
    capture: pathlib.Path,
    *,
    validate: pathlib.Path | None = None,
    output: pathlib.Path | None = None,
) -> list[str]:
    command = [
        str(runfile("phase4_confirmatory_h4096_same_run_operational_measurement_test_validator")),
        f"--raw={raw}",
        f"--same-run-telemetry={sidecar}",
        f"--capture={capture}",
        f"--expected-commit={_COMMIT}",
    ]
    if validate is not None:
        command.append(f"--validate={validate}")
    if output is not None:
        command.append(f"--output={output}")
    return command


def replace_option(command: list[str], name: str, value: str) -> list[str]:
    prefix = f"--{name}="
    return [f"{prefix}{value}" if argument.startswith(prefix) else argument for argument in command]


def _rebuild_raw_around_capture(capture: dict[str, object]) -> dict[str, object]:
    """Bind a complete synthetic Raw-v2 cell to the real repetition-zero replay."""
    raw = artifacts.make_raw(10100, 4, same_run=True, h4096=True, repetitions=20)
    raw["config"] = copy.deepcopy(capture["cell_config"])

    measured_semantics = {
        arm["arm"]: arm["measured_worker"]["payload"]["execution"]["semantics"]
        for arm in capture["arms"]
    }
    for repetition, pair in enumerate(raw["attempts"]):
        order = repetition % 2
        pair["execution_order"] = order
        for arm_name, arm_index in (("baseline", 0), ("candidate", 1)):
            arm = pair[arm_name]
            record = arm["record"]
            semantics = copy.deepcopy(measured_semantics[arm_index])
            semantics["repetition_index"] = repetition
            semantics["execution_order"] = order
            semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(semantics)
            record["semantics"] = semantics

            observation = record["external_observation"]
            observation["associated_semantic_checksum"] = semantics["semantic_checksum"]
            observation["configured_wall_limit_nanoseconds"] = raw["config"]["external_budget"][
                "maximum_cold_elapsed_nanoseconds"
            ]
            observation["configured_address_space_limit_bytes"] = raw["config"]["external_budget"][
                "maximum_address_space_bytes"
            ]
            observation["configured_peak_host_limit_bytes"] = raw["config"]["external_budget"][
                "maximum_peak_host_bytes"
            ]
            observation["authority_checksum"] = raw_validator.compute_authority_checksum(
                observation
            )
            record["artifact_checksum"] = raw_validator.compute_record_checksum(record)
            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)

        paired = pair["result"]
        paired["baseline"] = copy.deepcopy(pair["baseline"]["record"])
        paired["candidate"] = copy.deepcopy(pair["candidate"]["record"])
        paired["comparison"] = raw_validator._comparison(
            paired["baseline"],
            paired["candidate"],
        )
        paired["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(paired)
        paired["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(paired)
        pair["root_seed"] = paired["baseline"]["semantics"]["root_seed"]
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)

    raw["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
        raw,
        corpus_version=2,
    )
    raw["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(raw)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)
    return raw


def _spread(total: int, count: int, index: int) -> int:
    quotient, remainder = divmod(total, count)
    return quotient + (1 if index < remainder else 0)


def _rebuild_sidecar(raw: dict[str, object]) -> dict[str, object]:
    """Make complete aggregate-consistent telemetry for the captured semantics."""
    sidecar = artifacts.make_sidecar(raw)
    for attempt in sidecar["attempts"]:
        raw_attempt = raw["attempts"][attempt["repetition_index"]]
        for arm_name in ("baseline", "candidate"):
            arm = attempt[arm_name]
            semantics = raw_attempt[arm_name]["record"]["semantics"]
            rows = arm["telemetry"]["per_net"]
            requested = semantics["requested_columns"]
            executed = semantics["actual"]["route_queries"]
            admitted = semantics["admitted_candidates"]
            for index, row in enumerate(rows):
                columns = row["columns"]
                requested_row = _spread(requested, len(rows), index)
                admitted_row = _spread(admitted, len(rows), index)
                columns.update(
                    {
                        "requested_columns": requested_row,
                        "executed_route_queries": _spread(executed, len(rows), index),
                        "admitted_candidates": admitted_row,
                        "duplicate_candidates": requested_row - admitted_row,
                        "disconnected_columns": 0,
                        "unsupported_columns": 0,
                        "skipped_columns": 0,
                        "exact_validation_rejections": 0,
                        "other_rejections": 0,
                    }
                )
            arm["telemetry"]["telemetry_checksum"] = telemetry_validator.compute_telemetry_checksum(
                arm["telemetry"]
            )
            arm["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(arm)
        attempt["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(attempt)
    sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
        sidecar
    )
    return sidecar


def _with_exact_rejection(
    sidecar: dict[str, object],
) -> dict[str, object]:
    changed = copy.deepcopy(sidecar)
    row = next(
        row
        for attempt in changed["attempts"]
        for row in attempt["candidate"]["telemetry"]["per_net"]
        if row["columns"]["duplicate_candidates"] > 0
    )
    row["columns"]["duplicate_candidates"] -= 1
    row["columns"]["exact_validation_rejections"] += 1
    for attempt in changed["attempts"]:
        candidate = attempt["candidate"]
        if row in candidate["telemetry"]["per_net"]:
            candidate["telemetry"]["telemetry_checksum"] = (
                telemetry_validator.compute_telemetry_checksum(candidate["telemetry"])
            )
            candidate["capture_checksum"] = telemetry_validator.compute_arm_capture_checksum(
                candidate
            )
            attempt["capture_checksum"] = telemetry_validator.compute_pair_capture_checksum(attempt)
            break
    changed["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(changed)
    changed["source_envelope_checksum"] = telemetry_validator.compute_source_envelope_checksum(
        changed
    )
    return changed


def worker_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_h4096_same_run_operational_replay_worker"
        if production
        else "phase4_confirmatory_h4096_same_run_operational_replay_test_worker"
    )
    return [
        str(runfile(executable)),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
        "--raw_wire_schema_version=2",
        "--mode=measured",
        "--arm=baseline",
        "--case_id=10100",
        "--pool_size=4",
        "--workers=4",
        "--setup_ns=300000000000",
        "--prepared_ns=300000000000",
        "--cold_ns=300000000000",
        "--address_space_bytes=68719476736",
        "--peak_host_bytes=17179869184",
        "--maximum_nets=4096",
        "--maximum_compiled_nodes=100000000",
        "--maximum_compiled_host_bytes=8589934592",
        "--maximum_active_regions=250000",
        "--maximum_board_entities=100000",
        f"--apgar_commit={_COMMIT}",
    ]


class Phase4ConfirmatoryH4096SameRunOperationalMeasurementProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.hostile_cwd = cls.root / "hostile-cwd"
        cls.hostile_cwd.mkdir()
        cls.worker_canary = cls.hostile_cwd / "ambient-worker-ran"
        cls.python_canary = cls.hostile_cwd / "ambient-python-ran"

        hostile_bin = cls.hostile_cwd / "bin"
        hostile_bin.mkdir()
        hostile_python = hostile_bin / "python3"
        hostile_python.write_text(
            f"#!/bin/sh\n: > '{cls.python_canary}'\nexit 72\n",
            encoding="utf-8",
        )
        hostile_python.chmod(0o755)
        hostile_imports = cls.hostile_cwd / "python-imports"
        hostile_imports.mkdir()
        (hostile_imports / "sitecustomize.py").write_text(
            f"import pathlib\npathlib.Path({str(cls.python_canary)!r}).touch()\n",
            encoding="utf-8",
        )
        for name in (
            authority.PRODUCTION_WORKER,
            authority.TEST_WORKER,
        ):
            forged = cls.hostile_cwd / name
            forged.write_text(
                f"#!/bin/sh\n: > '{cls.worker_canary}'\nexit 71\n",
                encoding="utf-8",
            )
            forged.chmod(0o755)
        cls.shadow_runfiles = cls.root / "shadow.runfiles"
        shadow_main = cls.shadow_runfiles / "_main"
        shadow_main.mkdir(parents=True)
        for name in (
            authority.PRODUCTION_WORKER,
            authority.TEST_WORKER,
        ):
            (shadow_main / name).symlink_to(cls.hostile_cwd / name)
        cls.launcher_environment = os.environ.copy()
        cls.launcher_environment["RUNFILES_DIR"] = str(cls.shadow_runfiles)
        cls.launcher_environment["PATH"] = str(hostile_bin)
        cls.launcher_environment["PYTHONPATH"] = str(hostile_imports)
        cls.launcher_environment["PYTHONHOME"] = str(cls.hostile_cwd / "python-home")
        cls.launcher_environment["RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS"] = "--version"

        capture_run = subprocess.run(
            capture_command(),
            check=False,
            text=True,
            capture_output=True,
            timeout=900,
            cwd=cls.hostile_cwd,
            env=cls.launcher_environment,
        )
        if capture_run.returncode != 0:
            raise RuntimeError(capture_run.stderr)
        if cls.worker_canary.exists() or cls.python_canary.exists():
            raise RuntimeError("H4096 capture selected an ambient authority")
        cls.capture_bytes = capture_run.stdout
        cls.capture = json.loads(capture_run.stdout)
        cls.capture_path = cls.root / "capture.json"
        cls.capture_path.write_text(capture_run.stdout, encoding="utf-8")

        cls.raw = _rebuild_raw_around_capture(cls.capture)
        cls.sidecar = _rebuild_sidecar(cls.raw)
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "sidecar.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")

        publication_run = subprocess.run(
            publisher_command(cls.raw_path, cls.sidecar_path, cls.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            cwd=cls.hostile_cwd,
            env=cls.launcher_environment,
        )
        if publication_run.returncode != 0:
            raise RuntimeError(publication_run.stderr)
        if cls.worker_canary.exists() or cls.python_canary.exists():
            raise RuntimeError("H4096 publisher selected an ambient authority")
        cls.publication_bytes = publication_run.stdout
        cls.publication = json.loads(publication_run.stdout)
        cls.publication_path = cls.root / "publication.json"
        cls.publication_path.write_text(publication_run.stdout, encoding="utf-8")

        cls.h2250_raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
        )
        cls.h2250_sidecar = artifacts.make_sidecar(cls.h2250_raw)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_four_exec_join_binds_v2_domains_and_remains_nondecision(self) -> None:
        publisher.validate_join(
            self.raw,
            self.sidecar,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertEqual(
            self.publication["raw_authority_binding"]["authority"],
            "phase4_confirmatory_same_run_raw_evidence_v2",
        )
        self.assertEqual(
            self.publication["same_run_telemetry_binding"]["authority"],
            "phase4_confirmatory_same_run_decision_telemetry_v2",
        )
        self.assertEqual(
            self.publication["artifact_checksum"],
            publisher._publication_checksum(self.publication),
        )
        self.assertEqual(
            self.publication["source_envelope_checksum"],
            publisher._publication_source_checksum(self.publication),
        )
        self.assertTrue(self.publication["eligible_input_to_phase4_aggregation"])
        self.assertTrue(self.publication["cell_operational_telemetry_complete"])
        self.assertFalse(self.publication["standalone_decision_eligible"])
        self.assertFalse(self.publication["statistical_timing_eligible"])
        self.assertFalse(self.publication["coverage_complete"])

        ordinals: list[int] = []
        identities: set[int] = set()
        raw_pair = self.raw["attempts"][0]["result"]
        for index, arm_name in enumerate(("baseline", "candidate")):
            arm = self.capture["arms"][index]
            measured = arm["measured_process"]
            replay = arm["authority_process"]
            ordinals.extend((measured["dispatch_ordinal"], replay["dispatch_ordinal"]))
            identities.update(
                (
                    measured["process_instance_identity"],
                    replay["process_instance_identity"],
                )
            )
            measured_semantics = arm["measured_worker"]["payload"]["execution"]["semantics"]
            authority_semantics = arm["authority_worker"]["payload"]["semantics"]
            self.assertEqual(measured_semantics, authority_semantics)
            self.assertEqual(measured_semantics, raw_pair[arm_name]["semantics"])
            self.assertEqual(measured_semantics["budget_checksum"], 5851813264366095594)
        self.assertEqual(ordinals, [1, 3, 2, 4])
        self.assertEqual(len(identities), 4)
        self.assertNotIn(0, identities)

    def test_structurally_valid_false_guardrail_remains_publishable(self) -> None:
        changed = _with_exact_rejection(self.sidecar)
        projected = publisher.project_document(
            self.raw,
            changed,
            self.capture,
            expected_commit=_COMMIT,
            testing=True,
        )
        publisher.validate_join(
            self.raw,
            changed,
            self.capture,
            projected,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertFalse(
            projected["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"]
        )
        changed_path = self.root / "false-guardrail.json"
        changed_path.write_text(canonical(changed), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(self.raw_path, changed_path, self.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertFalse(
            json.loads(completed.stdout)["same_run_telemetry_binding"][
                "exact_rejection_guardrail_passed"
            ]
        )

    def test_h2250_and_h4096_authorities_cross_reject(self) -> None:
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            publisher.project_document(
                self.h2250_raw,
                self.h2250_sidecar,
                self.capture,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            h2250_publisher.validate_join(
                self.raw,
                self.sidecar,
                self.capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "another command or cell"):
            publisher.validate_join(
                self.h2250_raw,
                self.h2250_sidecar,
                self.capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )

    def test_capture_binds_target_digest_scope_and_test_escape(self) -> None:
        operational_validator.validate_capture(
            self.capture,
            expected_corpus_version=2,
            expected_publication_invocation=authority.TEST_INVOCATION,
            expected_worker_target=authority.TEST_WORKER_TARGET,
            expected_worker_sha256=authority.sha256_file(runfile(authority.TEST_WORKER)),
        )
        provenance = self.capture["reproducibility_provenance"]
        self.assertEqual(provenance["worker_target"], authority.TEST_WORKER_TARGET)
        self.assertEqual(
            provenance["worker_sha256"],
            authority.sha256_file(runfile(authority.TEST_WORKER)),
        )
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "fixed toolchain or backend identity",
        ):
            operational_validator.validate_capture(
                self.capture,
                expected_corpus_version=2,
                expected_publication_invocation=authority.TEST_INVOCATION,
                expected_worker_target=(
                    "//:phase4_confirmatory_same_run_operational_replay_test_worker"
                ),
                expected_worker_sha256=authority.sha256_file(runfile(authority.TEST_WORKER)),
            )
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "worker digest differs",
        ):
            operational_validator.validate_capture(
                self.capture,
                expected_corpus_version=2,
                expected_publication_invocation=authority.TEST_INVOCATION,
                expected_worker_target=authority.TEST_WORKER_TARGET,
                expected_worker_sha256="0" * 64,
            )

        valid = capture_command()
        mutations = (
            replace_option(valid, "corpus-version", "1"),
            replace_option(valid, "raw-wire-schema-version", "1"),
            replace_option(valid, "case-id", "10200"),
            replace_option(valid, "pool-size", "8"),
            replace_option(valid, "setup-ns", "299999999999"),
            [*valid, "--case-id=10100"],
        )
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                completed = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertIn(completed.returncode, {1, 2}, completed.stderr)
                self.assertEqual(completed.stdout, "")

        production_capture = subprocess.run(
            capture_command(production=True),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(production_capture.returncode, 2, production_capture.stderr)
        self.assertEqual(production_capture.stdout, "")

        production_worker = subprocess.run(
            worker_command(production=True),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(production_worker.returncode, 2, production_worker.stderr)
        self.assertEqual(production_worker.stdout, "")

        wrong_worker_scope = subprocess.run(
            replace_option(worker_command(), "case_id", "10200"),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(wrong_worker_scope.returncode, 2, wrong_worker_scope.stderr)
        self.assertEqual(wrong_worker_scope.stdout, "")

    def test_inner_python_entrypoints_require_their_compiled_launcher(self) -> None:
        for target in (
            "phase4_confirmatory_h4096_same_run_operational_capture_py",
            "phase4_confirmatory_h4096_same_run_operational_capture_test_py",
            "phase4_confirmatory_h4096_same_run_operational_measurement_validator_py",
            "phase4_confirmatory_h4096_same_run_operational_measurement_test_validator_py",
        ):
            with self.subTest(target=target):
                completed = subprocess.run(
                    [str(runfile(target)), "--help"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")
                self.assertIn("requires its compiled launcher", completed.stderr)

    def test_standalone_trees_are_fixtureless_and_target_specific(self) -> None:
        launchers = {
            "phase4_confirmatory_h4096_same_run_operational_capture": (authority.PRODUCTION_WORKER),
            "phase4_confirmatory_h4096_same_run_operational_capture_test": (authority.TEST_WORKER),
            "phase4_confirmatory_h4096_same_run_operational_measurement_validator": (
                authority.PRODUCTION_WORKER
            ),
            "phase4_confirmatory_h4096_same_run_operational_measurement_test_validator": (
                authority.TEST_WORKER
            ),
        }
        for launcher_name, expected_worker in launchers.items():
            with self.subTest(launcher=launcher_name):
                launcher = runfile(launcher_name).resolve()
                standalone = pathlib.Path(f"{launcher}.runfiles")
                main = standalone / "_main"
                self.assertTrue(main.is_dir())
                self.assertFalse(
                    (main / "tests/fixtures/phase4_supported_multinet_v1.kicad_pcb").exists()
                )
                workers = {
                    path.name
                    for path in main.iterdir()
                    if path.is_file()
                    and "operational_replay" in path.name
                    and "worker" in path.name
                }
                self.assertEqual(workers, {expected_worker})

    def test_publisher_opens_raw_sidecar_capture_then_validation(self) -> None:
        raw_fifo = self.root / "raw.fifo"
        sidecar_fifo = self.root / "sidecar.fifo"
        capture_fifo = self.root / "capture.fifo"
        publication_fifo = self.root / "publication.fifo"
        for path in (raw_fifo, sidecar_fifo, capture_fifo, publication_fifo):
            os.mkfifo(path)

        completed = subprocess.run(
            publisher_command(raw_fifo, sidecar_fifo, capture_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("regular file", completed.stderr)

        invalid_raw = copy.deepcopy(self.raw)
        invalid_raw["corpus_checksum"] = 0
        invalid_raw_path = self.root / "invalid-raw.json"
        invalid_raw_path.write_text(canonical(invalid_raw), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(invalid_raw_path, sidecar_fifo, capture_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("corpus_checksum", completed.stderr)
        self.assertNotIn("regular file", completed.stderr)

        completed = subprocess.run(
            publisher_command(self.raw_path, sidecar_fifo, capture_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("regular file", completed.stderr)

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        foreign_sidecar_path = self.root / "foreign-sidecar.json"
        foreign_sidecar_path.write_text(canonical(foreign_sidecar), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(self.raw_path, foreign_sidecar_path, capture_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("differs from Raw", completed.stderr)
        self.assertNotIn("regular file", completed.stderr)

        completed = subprocess.run(
            publisher_command(self.raw_path, self.sidecar_path, capture_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("regular file", completed.stderr)

        mismatched_raw = copy.deepcopy(self.raw)
        mismatched_raw["source_commit"] = "b" * 40
        mismatched_raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            mismatched_raw
        )
        mismatched_sidecar = copy.deepcopy(self.sidecar)
        mismatched_sidecar["source_commit"] = "b" * 40
        mismatched_sidecar["raw_source_envelope_checksum"] = mismatched_raw[
            "source_envelope_checksum"
        ]
        mismatched_sidecar["artifact_checksum"] = telemetry_validator.compute_cell_capture_checksum(
            mismatched_sidecar
        )
        mismatched_sidecar["source_envelope_checksum"] = (
            telemetry_validator.compute_source_envelope_checksum(mismatched_sidecar)
        )
        mismatched_raw_path = self.root / "mismatched-raw.json"
        mismatched_sidecar_path = self.root / "mismatched-sidecar.json"
        mismatched_raw_path.write_text(canonical(mismatched_raw), encoding="utf-8")
        mismatched_sidecar_path.write_text(canonical(mismatched_sidecar), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(
                mismatched_raw_path,
                mismatched_sidecar_path,
                self.capture_path,
                validate=publication_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("source envelopes differ", completed.stderr)
        self.assertNotIn("regular file", completed.stderr)

        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                self.sidecar_path,
                self.capture_path,
                validate=publication_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("regular file", completed.stderr)

    def test_output_install_is_atomic_and_no_replace(self) -> None:
        output = self.root / "installed-publication.json"
        first = subprocess.run(
            publisher_command(
                self.raw_path,
                self.sidecar_path,
                self.capture_path,
                output=output,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, "")
        installed = output.read_bytes()
        self.assertEqual(installed, self.publication_bytes.encode())

        second = subprocess.run(
            publisher_command(
                self.raw_path,
                self.sidecar_path,
                self.capture_path,
                output=output,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(second.returncode, 1, second.stderr)
        self.assertEqual(second.stdout, "")
        self.assertEqual(output.read_bytes(), installed)


if __name__ == "__main__":
    unittest.main()
