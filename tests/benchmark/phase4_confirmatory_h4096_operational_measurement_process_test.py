"""Acquisition-free process coverage for the H4096 ordinary operational join."""

from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import pathlib
import subprocess
import tempfile
import unittest
from unittest import mock

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import capture_phase4_confirmatory_h4096_operational_measurement as capture_entry
from tools import capture_phase4_operational_measurement as capture_tool
from tools import phase4_confirmatory_h4096_operational_authority as authority
from tools import (
    validate_phase4_confirmatory_h4096_operational_measurement as publisher,
)
from tools import (
    validate_phase4_confirmatory_h4096_same_run_operational_measurement as h4096_same_run_publisher,
)
from tools import (
    validate_phase4_confirmatory_operational_measurement as h2250_publisher,
)
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator

_COMMIT = "a" * 40
_ARTIFACT_DOMAIN = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2"
_SOURCE_DOMAIN = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def capture_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_h4096_operational_capture"
        if production
        else "phase4_confirmatory_h4096_operational_capture_test"
    )
    return [
        str(runfile(executable)),
        "--corpus-version=2",
        "--raw-wire-schema-version=1",
        "--case-id=10200",
        "--pool-size=8",
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
    capture: pathlib.Path,
    *,
    validate: pathlib.Path | None = None,
    output: pathlib.Path | None = None,
) -> list[str]:
    command = [
        str(runfile("phase4_confirmatory_h4096_operational_measurement_test_validator")),
        f"--raw={raw}",
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


def worker_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_h4096_operational_replay_worker"
        if production
        else "phase4_confirmatory_h4096_operational_replay_test_worker"
    )
    return [
        str(runfile(executable)),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
        "--raw_wire_schema_version=1",
        "--mode=measured",
        "--arm=baseline",
        "--case_id=10200",
        "--pool_size=8",
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


def _rebuild_raw_around_capture(capture: dict[str, object]) -> dict[str, object]:
    """Bind a complete synthetic Raw-v1 cell to the real repetition-zero replay."""
    raw = artifacts.make_raw(
        10200,
        8,
        same_run=False,
        h4096=True,
        repetitions=20,
    )
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


def _rechecksum_capture_with_budget(
    capture: dict[str, object],
    budget_checksum: int,
) -> dict[str, object]:
    """Build an internally valid capture carrying a substituted paired budget."""
    result = copy.deepcopy(capture)
    for arm in result["arms"]:
        measured_worker = arm["measured_worker"]
        measured_payload = measured_worker["payload"]
        measured_semantics = measured_payload["execution"]["semantics"]
        measured_semantics["budget_checksum"] = budget_checksum
        measured_semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(
            measured_semantics
        )
        measured_payload["profile_checksum"] = operational_validator._profile_checksum(
            measured_payload
        )
        measured_worker["artifact_checksum"] = capture_tool._worker_artifact_checksum(
            measured_worker["kind"],
            measured_worker["compiler_identity"],
            measured_payload["profile_checksum"],
        )
        measured_worker["source_envelope_checksum"] = capture_tool._worker_source_checksum(
            measured_worker
        )

        authority_worker = arm["authority_worker"]
        authority_payload = authority_worker["payload"]
        authority_semantics = authority_payload["semantics"]
        authority_semantics["budget_checksum"] = budget_checksum
        authority_semantics["semantic_checksum"] = raw_validator.compute_semantic_checksum(
            authority_semantics
        )
        authority_payload["authority_checksum"] = operational_validator._authority_checksum(
            authority_payload
        )
        authority_worker["artifact_checksum"] = capture_tool._worker_artifact_checksum(
            authority_worker["kind"],
            authority_worker["compiler_identity"],
            authority_payload["authority_checksum"],
        )
        authority_worker["source_envelope_checksum"] = capture_tool._worker_source_checksum(
            authority_worker
        )

    result["artifact_checksum"] = capture_tool._artifact_checksum(result)
    result["source_envelope_checksum"] = capture_tool._source_checksum(result)
    return result


class Phase4ConfirmatoryH4096OperationalMeasurementProcessTest(unittest.TestCase):
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
        for name in (authority.PRODUCTION_WORKER, authority.TEST_WORKER):
            forged = cls.hostile_cwd / name
            forged.write_text(
                f"#!/bin/sh\n: > '{cls.worker_canary}'\nexit 71\n",
                encoding="utf-8",
            )
            forged.chmod(0o755)

        cls.shadow_runfiles = cls.root / "shadow.runfiles"
        shadow_main = cls.shadow_runfiles / "_main"
        shadow_main.mkdir(parents=True)
        for name in (authority.PRODUCTION_WORKER, authority.TEST_WORKER):
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
            raise RuntimeError("H4096 ordinary capture selected an ambient authority")
        cls.capture_bytes = capture_run.stdout
        cls.capture = json.loads(capture_run.stdout)
        cls.capture_path = cls.root / "capture.json"
        cls.capture_path.write_text(capture_run.stdout, encoding="utf-8")

        cls.raw = _rebuild_raw_around_capture(cls.capture)
        cls.raw_path = cls.root / "raw.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")

        first = subprocess.run(
            publisher_command(cls.raw_path, cls.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            cwd=cls.hostile_cwd,
            env=cls.launcher_environment,
        )
        if first.returncode != 0:
            raise RuntimeError(first.stderr)
        second = subprocess.run(
            publisher_command(cls.raw_path, cls.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            cwd=cls.hostile_cwd,
            env=cls.launcher_environment,
        )
        if second.returncode != 0:
            raise RuntimeError(second.stderr)
        if cls.worker_canary.exists() or cls.python_canary.exists():
            raise RuntimeError("H4096 ordinary publisher selected an ambient authority")
        cls.publication_bytes = first.stdout
        cls.second_publication_bytes = second.stdout
        cls.publication = json.loads(first.stdout)
        cls.publication_path = cls.root / "publication.json"
        cls.publication_path.write_text(first.stdout, encoding="utf-8")

        cls.h2250_raw = artifacts.make_raw(
            10200,
            4,
            same_run=False,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        cls.h2250_alias_raw = artifacts.make_raw(
            10200,
            8,
            same_run=False,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        cls.h4096_same_run_raw = artifacts.make_raw(
            10100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        cls.h4096_same_run_sidecar = artifacts.make_sidecar(cls.h4096_same_run_raw)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_four_exec_join_binds_v2_domains_and_remains_nondecision(self) -> None:
        publisher.validate_join(
            self.raw,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertEqual(publisher._ARTIFACT_DOMAIN, _ARTIFACT_DOMAIN)
        self.assertEqual(publisher._SOURCE_DOMAIN, _SOURCE_DOMAIN)
        self.assertEqual(
            self.publication["raw_authority_binding"]["authority"],
            "phase4_confirmatory_raw_evidence_v2",
        )
        self.assertEqual(
            self.publication["raw_authority_binding"]["raw_evidence_schema_version"],
            1,
        )
        self.assertEqual(self.publication["raw_authority_binding"]["wire_schema_version"], 1)
        self.assertEqual(
            self.publication["artifact_checksum"],
            publisher._publication_checksum(self.publication),
        )
        self.assertEqual(
            self.publication["source_envelope_checksum"],
            publisher._publication_source_checksum(self.publication),
        )
        self.assertEqual(self.publication["campaign_id"], "phase4_confirmatory_corpus_v2")
        self.assertEqual(self.publication["cell_role"], "calibration")
        self.assertTrue(self.publication["eligible_input_to_phase4_aggregation"])
        self.assertTrue(self.publication["cell_operational_telemetry_complete"])
        self.assertFalse(self.publication["standalone_decision_eligible"])
        self.assertFalse(self.publication["statistical_timing_eligible"])
        self.assertFalse(self.publication["coverage_complete"])
        self.assertFalse(self.capture["standalone_publication_eligible"])

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
            self.assertEqual(arm["measured_worker"]["kind"], 0)
            self.assertEqual(arm["authority_worker"]["kind"], 1)
            measured_semantics = arm["measured_worker"]["payload"]["execution"]["semantics"]
            authority_semantics = arm["authority_worker"]["payload"]["semantics"]
            self.assertEqual(measured_semantics, authority_semantics)
            self.assertEqual(measured_semantics, raw_pair[arm_name]["semantics"])
            self.assertEqual(measured_semantics["corpus_version"], 2)
            self.assertEqual(
                measured_semantics["budget_checksum"],
                12108149041077564710,
            )
        self.assertEqual(ordinals, [1, 3, 2, 4])
        self.assertEqual(len(identities), 4)
        self.assertNotIn(0, identities)

    def test_measured_and_authority_resources_are_separate(self) -> None:
        for arm in self.capture["arms"]:
            measured = arm["measured_process"]
            replay = arm["authority_process"]
            self.assertEqual(
                measured["total_cpu_nanoseconds"],
                measured["user_cpu_nanoseconds"] + measured["system_cpu_nanoseconds"],
            )
            self.assertGreater(measured["outer_wall_nanoseconds"], 0)
            self.assertGreater(measured["peak_host_bytes"], 0)
            self.assertNotIn("resource_measurements", measured)
            self.assertEqual(
                replay["resource_measurements"],
                {
                    "status": "not_used",
                    "reason": "unmeasured_full_preimage_authority_replay",
                },
            )
            for forbidden in (
                "outer_wall_nanoseconds",
                "user_cpu_nanoseconds",
                "system_cpu_nanoseconds",
                "total_cpu_nanoseconds",
                "peak_host_bytes",
                "measurement_scope",
            ):
                self.assertNotIn(forbidden, replay)

    def test_publication_is_deterministic_and_validates_exactly(self) -> None:
        self.assertEqual(self.publication_bytes, self.second_publication_bytes)
        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                self.capture_path,
                validate=self.publication_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            cwd=self.hostile_cwd,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("validated one Phase 4", completed.stdout)
        self.assertFalse(self.worker_canary.exists())
        self.assertFalse(self.python_canary.exists())

        relabeled = copy.deepcopy(self.publication)
        relabeled["raw_authority_binding"]["authority"] = "phase4_confirmatory_raw_evidence_v1"
        relabeled["artifact_checksum"] = publisher._publication_checksum(relabeled)
        relabeled["source_envelope_checksum"] = publisher._publication_source_checksum(relabeled)
        with self.assertRaises(ValueError):
            publisher.validate_join(
                self.raw,
                self.capture,
                relabeled,
                expected_commit=_COMMIT,
                testing=True,
            )

    def test_h2250_ordinary_and_h4096_same_run_authorities_cross_reject(self) -> None:
        with self.assertRaises(raw_validator.EvidenceError):
            publisher.project_document(
                self.h2250_raw,
                self.capture,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            publisher.project_document(
                self.h2250_alias_raw,
                self.capture,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            h2250_publisher.validate_join(
                self.raw,
                self.capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            publisher.project_document(
                self.h4096_same_run_raw,
                self.capture,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            h4096_same_run_publisher.project_document(
                self.raw,
                self.h4096_same_run_sidecar,
                self.capture,
                expected_commit=_COMMIT,
                testing=True,
            )

    def test_capture_binds_target_digest_invocation_and_closed_scope(self) -> None:
        worker_digest = authority.sha256_file(runfile(authority.TEST_WORKER))
        operational_validator.validate_capture(
            self.capture,
            expected_corpus_version=2,
            expected_publication_invocation=authority.TEST_INVOCATION,
            expected_worker_target=authority.TEST_WORKER_TARGET,
            expected_worker_sha256=worker_digest,
        )
        provenance = self.capture["reproducibility_provenance"]
        self.assertEqual(provenance["publication_invocation"], authority.TEST_INVOCATION)
        self.assertEqual(provenance["worker_target"], authority.TEST_WORKER_TARGET)
        self.assertEqual(provenance["worker_sha256"], worker_digest)
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "fixed toolchain or backend identity",
        ):
            operational_validator.validate_capture(
                self.capture,
                expected_corpus_version=2,
                expected_publication_invocation=authority.TEST_INVOCATION,
                expected_worker_target=(
                    "//:phase4_confirmatory_h4096_same_run_operational_replay_test_worker"
                ),
                expected_worker_sha256=worker_digest,
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

        wrong_budget = _rechecksum_capture_with_budget(self.capture, 1)
        operational_validator.validate_capture(
            wrong_budget,
            expected_corpus_version=2,
            expected_publication_invocation=authority.TEST_INVOCATION,
            expected_worker_target=authority.TEST_WORKER_TARGET,
            expected_worker_sha256=worker_digest,
        )
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            mock.patch.object(capture_tool, "capture", return_value=wrong_budget),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            return_code = capture_entry.main(capture_command()[1:], testing=True)
        self.assertEqual(return_code, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("paired semantic budget authority differs", stderr.getvalue())

        valid = capture_command()
        mutations = (
            [
                argument
                for argument in valid
                if not argument.startswith("--raw-wire-schema-version=")
            ],
            replace_option(valid, "corpus-version", "1"),
            replace_option(valid, "raw-wire-schema-version", "2"),
            replace_option(valid, "case-id", "10100"),
            replace_option(valid, "pool-size", "4"),
            replace_option(valid, "pool-size", "16"),
            replace_option(valid, "workers", "1"),
            replace_option(valid, "setup-ns", "299999999999"),
            replace_option(valid, "prepared-ns", "299999999999"),
            replace_option(valid, "cold-ns", "299999999999"),
            replace_option(valid, "address-space-bytes", "68719476735"),
            replace_option(valid, "peak-host-bytes", "17179869183"),
            replace_option(valid, "maximum-nets", "4095"),
            replace_option(valid, "maximum-compiled-nodes", "99999999"),
            replace_option(valid, "maximum-compiled-host-bytes", "8589934591"),
            replace_option(valid, "maximum-active-regions", "249999"),
            replace_option(valid, "maximum-board-entities", "99999"),
            [*valid, "--case-id=10200"],
        )
        for index, command in enumerate(mutations):
            with self.subTest(index=index):
                completed = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                    env=self.launcher_environment,
                )
                self.assertIn(completed.returncode, {1, 2}, completed.stderr)
                self.assertEqual(completed.stdout, "")

        production_capture = subprocess.run(
            capture_command(production=True),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
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

        for name, value in (
            ("raw_wire_schema_version", "2"),
            ("case_id", "10100"),
            ("pool_size", "4"),
            ("pool_size", "16"),
            ("workers", "1"),
            ("setup_ns", "299999999999"),
            ("prepared_ns", "299999999999"),
            ("cold_ns", "299999999999"),
            ("address_space_bytes", "68719476735"),
            ("peak_host_bytes", "17179869183"),
            ("maximum_nets", "4095"),
            ("maximum_compiled_nodes", "99999999"),
            ("maximum_compiled_host_bytes", "8589934591"),
            ("maximum_active_regions", "249999"),
            ("maximum_board_entities", "99999"),
        ):
            with self.subTest(worker_option=name):
                completed = subprocess.run(
                    replace_option(worker_command(), name, value),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")

    def test_inner_python_entrypoints_require_their_compiled_launcher(self) -> None:
        for target in (
            "phase4_confirmatory_h4096_operational_capture_py",
            "phase4_confirmatory_h4096_operational_capture_test_py",
            "phase4_confirmatory_h4096_operational_measurement_validator_py",
            "phase4_confirmatory_h4096_operational_measurement_test_validator_py",
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
            "phase4_confirmatory_h4096_operational_capture": authority.PRODUCTION_WORKER,
            "phase4_confirmatory_h4096_operational_capture_test": authority.TEST_WORKER,
            "phase4_confirmatory_h4096_operational_measurement_validator": (
                authority.PRODUCTION_WORKER
            ),
            "phase4_confirmatory_h4096_operational_measurement_test_validator": (
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

    def test_publisher_opens_raw_capture_then_validation(self) -> None:
        raw_fifo = self.root / "raw.fifo"
        capture_fifo = self.root / "capture.fifo"
        publication_fifo = self.root / "publication.fifo"
        for path in (raw_fifo, capture_fifo, publication_fifo):
            os.mkfifo(path)

        completed = subprocess.run(
            publisher_command(raw_fifo, capture_fifo, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("raw cell input must resolve to a regular file", completed.stderr)
        self.assertNotIn("operational capture input", completed.stderr)

        invalid_raw = copy.deepcopy(self.raw)
        invalid_raw["corpus_checksum"] ^= 1
        invalid_raw_path = self.root / "invalid-raw.json"
        invalid_raw_path.write_text(canonical(invalid_raw), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(invalid_raw_path, capture_fifo, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("corpus_checksum", completed.stderr)
        self.assertNotIn("operational capture input", completed.stderr)

        completed = subprocess.run(
            publisher_command(self.raw_path, capture_fifo, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn(
            "operational capture input must resolve to a regular file",
            completed.stderr,
        )
        self.assertNotIn("operational publication input", completed.stderr)

        mismatched_capture = copy.deepcopy(self.capture)
        mismatched_capture["cell_config"]["maximum_setup_elapsed_nanoseconds"] -= 1
        mismatched_capture["artifact_checksum"] = operational_validator._capture_checksum(
            mismatched_capture
        )
        mismatched_capture["source_envelope_checksum"] = (
            operational_validator._capture_source_checksum(mismatched_capture)
        )
        mismatched_capture_path = self.root / "mismatched-capture.json"
        mismatched_capture_path.write_text(canonical(mismatched_capture), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                mismatched_capture_path,
                validate=publication_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn("operational capture config differs from Raw", completed.stderr)
        self.assertNotIn("operational publication input", completed.stderr)

        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                self.capture_path,
                validate=publication_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertIn(
            "operational publication input must resolve to a regular file",
            completed.stderr,
        )

    def test_output_install_is_atomic_and_no_replace(self) -> None:
        mutually_exclusive = subprocess.run(
            publisher_command(
                self.raw_path,
                self.capture_path,
                validate=self.publication_path,
                output=self.root / "mutually-exclusive.json",
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(mutually_exclusive.returncode, 1, mutually_exclusive.stderr)
        self.assertIn("--validate and --output are mutually exclusive", mutually_exclusive.stderr)

        output = self.root / "installed-publication.json"
        first = subprocess.run(
            publisher_command(
                self.raw_path,
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
        self.assertFalse(any(output.parent.glob(f".{output.name}.phase4-tmp-*")))

        second = subprocess.run(
            publisher_command(
                self.raw_path,
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
        self.assertIn("already exists", second.stderr)
        self.assertEqual(output.read_bytes(), installed)
        self.assertFalse(any(output.parent.glob(f".{output.name}.phase4-tmp-*")))

    def test_cli_rejects_same_run_telemetry(self) -> None:
        completed = subprocess.run(
            [
                *publisher_command(self.raw_path, self.capture_path),
                f"--same-run-telemetry={self.raw_path}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            cwd=self.hostile_cwd,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn("unrecognized arguments: --same-run-telemetry=", completed.stderr)
        self.assertFalse(self.worker_canary.exists())
        self.assertFalse(self.python_canary.exists())


if __name__ == "__main__":
    unittest.main()
