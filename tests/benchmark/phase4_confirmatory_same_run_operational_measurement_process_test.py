"""Real-process coverage for the confirmatory same-run operational publication."""

from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from unittest import mock

from tools import validate_phase4_confirmatory_operational_measurement as ordinary_publisher
from tools import (
    validate_phase4_confirmatory_same_run_operational_measurement as publication_validator,
)
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def ignore_sigchld() -> None:
    signal.signal(signal.SIGCHLD, signal.SIG_IGN)


def normalize_test_source(raw: dict[str, object], sidecar: dict[str, object]) -> None:
    raw["source_commit"] = raw["source_commit"] or ("0" * 40)
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)
    sidecar["source_commit"] = raw["source_commit"]
    sidecar["source_stamped"] = raw["source_stamped"]
    sidecar["source_tree_dirty"] = raw["source_tree_dirty"]
    sidecar["raw_source_envelope_checksum"] = raw["source_envelope_checksum"]
    sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
        sidecar
    )


def raw_command(sidecar: pathlib.Path) -> list[str]:
    return [
        str(runfile("phase4_confirmatory_evidence_test_runner")),
        "--corpus_version=2",
        "--testing_allow_unstamped=1",
        "--case_id=10100",
        "--pool_size=4",
        "--workers=4",
        "--repetitions=20",
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
        f"--same_run_telemetry_output={sidecar}",
    ]


def capture_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_same_run_operational_capture"
        if production
        else "phase4_confirmatory_same_run_operational_capture_test"
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
) -> list[str]:
    command = [
        str(runfile("phase4_confirmatory_same_run_operational_measurement_test_validator")),
        f"--raw={raw}",
        f"--same-run-telemetry={sidecar}",
        f"--capture={capture}",
        f"--expected-commit={_COMMIT}",
    ]
    if validate is not None:
        command.append(f"--validate={validate}")
    return command


class Phase4ConfirmatorySameRunOperationalMeasurementProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        raw_run = subprocess.run(
            raw_command(cls.sidecar_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=360,
        )
        if raw_run.returncode != 0:
            raise RuntimeError(raw_run.stderr)
        cls.raw = json.loads(raw_run.stdout)
        cls.sidecar = same_run_validator.read_document(cls.sidecar_path)
        normalize_test_source(cls.raw, cls.sidecar)
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")

        cls.hostile_cwd = cls.root / "hostile-cwd"
        cls.hostile_cwd.mkdir()
        cls.worker_canary = cls.hostile_cwd / "forged-worker-ran"
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
            "phase4_confirmatory_same_run_operational_replay_worker",
            "phase4_confirmatory_same_run_operational_replay_test_worker",
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
            "phase4_confirmatory_same_run_operational_replay_worker",
            "phase4_confirmatory_same_run_operational_replay_test_worker",
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
            raise RuntimeError("same-run capture selected an ambient authority")
        cls.capture_bytes = capture_run.stdout
        cls.capture = json.loads(capture_run.stdout)
        cls.capture_path = cls.root / "capture.json"
        cls.capture_path.write_text(capture_run.stdout, encoding="utf-8")

        publications: list[str] = []
        for _ in range(2):
            completed = subprocess.run(
                publisher_command(cls.raw_path, cls.sidecar_path, cls.capture_path),
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
                env=cls.launcher_environment,
            )
            if completed.returncode != 0:
                raise RuntimeError(completed.stderr)
            publications.append(completed.stdout)
        if cls.python_canary.exists():
            raise RuntimeError("same-run publisher selected ambient Python authority")
        cls.first_publication_bytes, cls.second_publication_bytes = publications
        cls.publication = json.loads(publications[0])
        cls.publication_path = cls.root / "publication.json"
        cls.publication_path.write_text(publications[0], encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_real_four_exec_capture_strictly_joins_raw_sidecar_and_publication(self) -> None:
        publication_validator.validate_join(
            self.raw,
            self.sidecar,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        self.assertTrue(same_run_validator.exact_rejection_guardrail_passes(self.sidecar))
        self.assertEqual(self.publication["cell_role"], "exact")
        self.assertTrue(
            self.publication["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"]
        )
        self.assertEqual(
            self.publication["same_run_telemetry_binding"]["artifact_checksum"],
            self.sidecar["artifact_checksum"],
        )
        raw_pair = self.raw["attempts"][0]["result"]
        ordinals: list[int] = []
        identities: set[int] = set()
        for index, name in enumerate(("baseline", "candidate")):
            arm = self.capture["arms"][index]
            measured = arm["measured_process"]
            authority_process = arm["authority_process"]
            ordinals.extend((measured["dispatch_ordinal"], authority_process["dispatch_ordinal"]))
            identities.update(
                (
                    measured["process_instance_identity"],
                    authority_process["process_instance_identity"],
                )
            )
            measured_semantics = arm["measured_worker"]["payload"]["execution"]["semantics"]
            authority_semantics = arm["authority_worker"]["payload"]["semantics"]
            self.assertEqual(measured_semantics, authority_semantics)
            self.assertEqual(measured_semantics, raw_pair[name]["semantics"])
            self.assertEqual(measured_semantics["corpus_version"], 2)
        self.assertEqual(ordinals, [1, 3, 2, 4])
        self.assertEqual(len(identities), 4)
        self.assertNotIn(0, identities)

    def test_resources_and_publication_flags_are_typed_and_nondecision(self) -> None:
        for arm in self.capture["arms"]:
            measured = arm["measured_process"]
            authority_process = arm["authority_process"]
            self.assertEqual(
                measured["total_cpu_nanoseconds"],
                measured["user_cpu_nanoseconds"] + measured["system_cpu_nanoseconds"],
            )
            self.assertGreater(measured["outer_wall_nanoseconds"], 0)
            self.assertGreater(measured["peak_host_bytes"], 0)
            self.assertEqual(
                authority_process["resource_measurements"],
                {
                    "status": "not_used",
                    "reason": "unmeasured_full_preimage_authority_replay",
                },
            )
        self.assertTrue(self.publication["eligible_input_to_phase4_aggregation"])
        self.assertTrue(self.publication["cell_operational_telemetry_complete"])
        self.assertFalse(self.publication["standalone_decision_eligible"])
        self.assertFalse(self.publication["statistical_timing_eligible"])
        self.assertFalse(self.publication["coverage_complete"])
        self.assertFalse(self.capture["source_stamped"] and not self.capture["source_tree_dirty"])
        self.assertRegex(self.capture["source_commit"], r"[0-9a-f]{40}")
        if self.capture["source_commit"] == "0" * 40:
            self.assertFalse(self.capture["source_stamped"])

    def test_publication_is_deterministic_and_rebuild_validates(self) -> None:
        self.assertEqual(self.first_publication_bytes, self.second_publication_bytes)
        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                self.sidecar_path,
                self.capture_path,
                validate=self.publication_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_structurally_valid_false_guardrail_is_publishable_negative_evidence(self) -> None:
        changed = copy.deepcopy(self.sidecar)
        row = next(
            row
            for attempt in changed["attempts"]
            for arm in (attempt["baseline"], attempt["candidate"])
            for row in arm["telemetry"]["per_net"]
            if row["columns"]["duplicate_candidates"] > 0
        )
        row["columns"]["duplicate_candidates"] -= 1
        row["columns"]["exact_validation_rejections"] += 1
        selected_arm = next(
            arm
            for attempt in changed["attempts"]
            for arm in (attempt["baseline"], attempt["candidate"])
            if row in arm["telemetry"]["per_net"]
        )
        selected_attempt = next(
            attempt
            for attempt in changed["attempts"]
            if selected_arm in (attempt["baseline"], attempt["candidate"])
        )
        selected_arm["telemetry"]["telemetry_checksum"] = (
            same_run_validator.compute_telemetry_checksum(selected_arm["telemetry"])
        )
        selected_arm["capture_checksum"] = same_run_validator.compute_arm_capture_checksum(
            selected_arm
        )
        selected_attempt["capture_checksum"] = same_run_validator.compute_pair_capture_checksum(
            selected_attempt
        )
        changed["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(changed)
        changed["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            changed
        )
        same_run_validator.validate_confirmatory_join(
            self.raw,
            changed,
            allow_unstamped=True,
            expected_commit=_COMMIT,
        )
        projected = operational_validator.project_confirmatory_same_run_document(
            self.raw,
            changed,
            self.capture,
        )
        publication_validator.validate_join(
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
        changed_path = self.root / "negative-same-run.json"
        publication_path = self.root / "negative-publication.json"
        changed_path.write_text(canonical(changed), encoding="utf-8")
        completed = subprocess.run(
            publisher_command(self.raw_path, changed_path, self.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        rebuilt = json.loads(completed.stdout)
        self.assertFalse(rebuilt["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"])
        publication_path.write_text(completed.stdout, encoding="utf-8")
        completed = subprocess.run(
            publisher_command(
                self.raw_path,
                changed_path,
                self.capture_path,
                validate=publication_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_cross_authorities_and_foreign_sidecar_reject(self) -> None:
        with self.assertRaisesRegex(raw_validator.EvidenceError, "raw cell fields differ"):
            ordinary_publisher.validate_join(
                self.raw,
                self.capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "fixed toolchain or backend identity is invalid|publication invocation is invalid",
        ):
            operational_validator.validate_capture(
                self.capture,
                expected_corpus_version=2,
                expected_publication_invocation=(
                    "bazel --batch run --config=benchmark "
                    "//:phase4_confirmatory_operational_capture_test"
                ),
                expected_worker_target="//:phase4_confirmatory_operational_replay_test_worker",
            )
        foreign = copy.deepcopy(self.sidecar)
        foreign["raw_cell_artifact_checksum"] ^= 1
        foreign["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
            foreign
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "differs from Raw"):
            publication_validator.validate_join(
                self.raw,
                foreign,
                self.capture,
                self.publication,
                expected_commit=_COMMIT,
                testing=True,
            )

    def test_worker_io_failure_uses_the_stable_publisher_error_boundary(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with (
            mock.patch.object(
                publication_validator.authority,
                "resolve_bundled_worker",
                side_effect=OSError("missing fixed worker"),
            ),
            contextlib.redirect_stdout(stdout),
            contextlib.redirect_stderr(stderr),
        ):
            result = publication_validator.main(
                [
                    f"--raw={self.raw_path}",
                    f"--same-run-telemetry={self.sidecar_path}",
                    f"--capture={self.capture_path}",
                    f"--expected-commit={_COMMIT}",
                ],
                testing=True,
            )
        self.assertEqual(result, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn(
            "Phase 4 confirmatory same-run operational publication failed: missing fixed worker",
            stderr.getvalue(),
        )
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_capture_and_worker_reject_scope_budget_and_test_escape(self) -> None:
        valid = capture_command()

        def replace(command: list[str], name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in command
            ]

        for index, command in enumerate(
            (
                replace(valid, "corpus-version", "1"),
                replace(valid, "raw-wire-schema-version", "1"),
                replace(valid, "case-id", "10101"),
                replace(valid, "case-id", "10200"),
                replace(valid, "case-id", "11000"),
                replace(valid, "case-id", "14000"),
                replace(valid, "pool-size", "8"),
                replace(valid, "workers", "1"),
                replace(valid, "setup-ns", "299999999999"),
                replace(valid, "maximum-nets", "4095"),
                [*valid, "--case-id=10100"],
            )
        ):
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

        worker = [
            str(runfile("phase4_confirmatory_same_run_operational_replay_test_worker")),
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
        direct_worker_mutations = [
            replace(worker, "corpus_version", "1"),
            replace(worker, "raw_wire_schema_version", "1"),
            replace(worker, "case_id", "10101"),
            replace(worker, "case_id", "10200"),
            replace(worker, "case_id", "11000"),
            replace(worker, "case_id", "14000"),
            replace(worker, "pool_size", "8"),
            replace(worker, "workers", "1"),
            replace(worker, "case_id", "010100"),
            replace(worker, "workers", "+4"),
            [*worker, "--case_id=10100"],
        ]
        direct_worker_mutations.extend(
            replace(worker, name, value)
            for name, value in (
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
            )
        )
        for index, command in enumerate(direct_worker_mutations):
            with self.subTest(index=index):
                completed = subprocess.run(
                    command,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertNotIn("RLIMIT_AS", completed.stderr)
        production_capture = subprocess.run(
            capture_command(production=True),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(production_capture.returncode, 2)
        production_worker = list(worker)
        production_worker[0] = str(
            runfile("phase4_confirmatory_same_run_operational_replay_worker")
        )
        completed = subprocess.run(
            production_worker,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")

    def test_launcher_ignores_forged_argv_zero(self) -> None:
        forged = self.hostile_cwd / "forged-launcher"
        forged.write_text("#!/bin/sh\nexit 73\n", encoding="utf-8")
        forged.chmod(0o755)
        forged_interpreter = (
            pathlib.Path(f"{forged}.runfiles")
            / "_main"
            / "_phase4_confirmatory_same_run_operational_capture_test_py.venv"
            / "bin"
            / "python3"
        )
        forged_interpreter.parent.mkdir(parents=True)
        forged_interpreter.symlink_to(self.hostile_cwd / "bin" / "python3")
        command = capture_command()
        command[0] = str(forged)
        command = [
            "--case-id=11000" if argument.startswith("--case-id=") else argument
            for argument in command
        ]
        completed = subprocess.run(
            command,
            executable=str(runfile("phase4_confirmatory_same_run_operational_capture_test")),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertIn("cannot authenticate its bundled runfiles", completed.stderr)
        self.assertFalse(self.python_canary.exists())

    def test_inner_python_entrypoints_require_compiled_launcher(self) -> None:
        for target in (
            "phase4_confirmatory_same_run_operational_capture_py",
            "phase4_confirmatory_same_run_operational_capture_test_py",
            "phase4_confirmatory_same_run_operational_measurement_validator_py",
            "phase4_confirmatory_same_run_operational_measurement_test_validator_py",
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

        target = "phase4_confirmatory_same_run_operational_capture_test_py"
        read_descriptor, write_descriptor = os.pipe()
        try:
            os.write(
                write_descriptor,
                (f"APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-LAUNCH-V1\n{target}\n").encode(),
            )
            os.close(write_descriptor)
            write_descriptor = -1
            forged_environment = os.environ.copy()
            forged_environment["APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD"] = str(
                read_descriptor
            )
            completed = subprocess.run(
                [str(runfile(target)), "--help"],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
                env=forged_environment,
                pass_fds=(read_descriptor,),
            )
            self.assertEqual(completed.returncode, 2, completed.stderr)
            self.assertEqual(completed.stdout, "")
            self.assertIn("requires its compiled launcher", completed.stderr)
        finally:
            os.close(read_descriptor)
            if write_descriptor >= 0:
                os.close(write_descriptor)

        read_descriptor, write_descriptor = os.pipe()
        try:
            hanging_environment = os.environ.copy()
            hanging_environment["APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD"] = str(
                read_descriptor
            )
            completed = subprocess.run(
                [str(runfile(target)), "--help"],
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
                env=hanging_environment,
                pass_fds=(read_descriptor,),
            )
            self.assertEqual(completed.returncode, 2, completed.stderr)
            self.assertEqual(completed.stdout, "")
            self.assertIn("requires its compiled launcher", completed.stderr)
        finally:
            os.close(read_descriptor)
            os.close(write_descriptor)

        oversized_environment = os.environ.copy()
        oversized_environment["APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD"] = str(1 << 200)
        completed = subprocess.run(
            [str(runfile(target)), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=oversized_environment,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn("requires its compiled launcher", completed.stderr)
        self.assertNotIn("Traceback", completed.stderr)

    def test_launcher_accepts_a_complete_enclosing_runfiles_tree(self) -> None:
        shadow = self.root / "complete-launcher.runfiles"
        shutil.copytree(
            pathlib.Path(os.environ["TEST_SRCDIR"]),
            shadow,
            symlinks=True,
        )
        launcher = (
            shadow
            / os.environ["TEST_WORKSPACE"]
            / "phase4_confirmatory_operational_launcher_lifetime_probe"
        )
        completed = subprocess.run(
            [str(launcher), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)

    def test_launcher_rejects_an_incompletely_traversable_runfiles_tree(self) -> None:
        shadow = self.root / "unreadable-launcher.runfiles"
        shutil.copytree(
            pathlib.Path(os.environ["TEST_SRCDIR"]),
            shadow,
            symlinks=True,
        )
        main = shadow / os.environ["TEST_WORKSPACE"]
        for name, subtree in (
            ("main", main / "tools"),
            (
                "external",
                shadow
                / "rules_python++python+python_3_13_x86_64-unknown-linux-gnu"
                / "lib"
                / "python3.13",
            ),
        ):
            with self.subTest(name=name):
                subtree.chmod(0o111)
                try:
                    completed = subprocess.run(
                        [
                            str(main / "phase4_confirmatory_operational_launcher_lifetime_probe"),
                            "--help",
                        ],
                        check=False,
                        text=True,
                        capture_output=True,
                        timeout=10,
                        env=self.launcher_environment,
                    )
                finally:
                    subtree.chmod(0o755)
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")
                self.assertIn("cannot authenticate its bundled runfiles", completed.stderr)

    def test_launcher_parent_death_terminates_the_delegated_authority(self) -> None:
        cache_directories_before = set(pathlib.Path("/tmp").glob("apgar-phase4-python-cache-*"))
        ready = self.root / "launcher-lifetime-ready"
        release = self.root / "launcher-lifetime-release"
        canary = self.root / "launcher-lifetime-canary"
        process = subprocess.Popen(
            [
                str(runfile("phase4_confirmatory_operational_launcher_lifetime_probe")),
                f"--ready={ready}",
                f"--release={release}",
                f"--canary={canary}",
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        child_pid: int | None = None
        try:
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                try:
                    child_pid = int(ready.read_text(encoding="ascii").strip())
                    break
                except (FileNotFoundError, ValueError):
                    if process.poll() is not None:
                        break
                    time.sleep(0.01)
            self.assertIsNotNone(child_pid)
            self.assertIsNone(process.poll())
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
            release.touch()
            deadline = time.monotonic() + 2.0
            state: str | None = None
            while time.monotonic() < deadline:
                try:
                    status = pathlib.Path(f"/proc/{child_pid}/status").read_text(encoding="ascii")
                    state = next(
                        line.split()[1] for line in status.splitlines() if line.startswith("State:")
                    )
                except (FileNotFoundError, StopIteration):
                    state = None
                if state in {None, "Z"} or canary.exists():
                    break
                time.sleep(0.01)
            self.assertFalse(canary.exists())
            self.assertIn(state, {None, "Z"})
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            if child_pid is not None:
                try:
                    os.kill(child_pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            process.communicate(timeout=10)
        self.assertEqual(
            set(pathlib.Path("/tmp").glob("apgar-phase4-python-cache-*")),
            cache_directories_before,
        )

    def test_launcher_normalizes_inherited_ignored_sigchld(self) -> None:
        completed = subprocess.run(
            [
                str(runfile("phase4_confirmatory_operational_launcher_lifetime_probe")),
                "--help",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            preexec_fn=ignore_sigchld,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)
        self.assertEqual(completed.stderr, "")

    def test_publisher_obeys_raw_sidecar_capture_publication_open_order(self) -> None:
        sidecar_fifo = self.root / "sidecar.fifo"
        capture_fifo = self.root / "capture.fifo"
        publication_fifo = self.root / "publication.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(capture_fifo)
        os.mkfifo(publication_fifo)

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
        foreign_sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            same_run_validator.compute_source_envelope_checksum(foreign_sidecar)
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
        mismatched_sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(
            mismatched_sidecar
        )
        mismatched_sidecar["source_envelope_checksum"] = (
            same_run_validator.compute_source_envelope_checksum(mismatched_sidecar)
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


if __name__ == "__main__":
    unittest.main()
