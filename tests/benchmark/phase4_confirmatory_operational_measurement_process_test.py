"""Real-process coverage for the confirmatory ordinary operational publication."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import phase4_confirmatory_operational_authority as authority
from tools import validate_phase4_confirmatory_operational_measurement as publication_validator
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator

_COMMIT = "a" * 40
_TEST_INVOCATION = (
    "bazel --batch run --config=benchmark //:phase4_confirmatory_operational_capture_test"
)
_TEST_WORKER_TARGET = "//:phase4_confirmatory_operational_replay_test_worker"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def raw_command() -> list[str]:
    return [
        str(runfile("phase4_confirmatory_evidence_test_runner")),
        "--corpus_version=2",
        "--testing_allow_unstamped=1",
        "--case_id=10200",
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
    ]


def capture_command(*, production: bool = False) -> list[str]:
    executable = (
        "phase4_confirmatory_operational_capture"
        if production
        else "phase4_confirmatory_operational_capture_test"
    )
    return [
        str(runfile(executable)),
        "--corpus-version=2",
        "--raw-wire-schema-version=1",
        "--case-id=10200",
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
    capture: pathlib.Path,
    *,
    validate: pathlib.Path | None = None,
) -> list[str]:
    command = [
        str(runfile("phase4_confirmatory_operational_measurement_test_validator")),
        f"--raw={raw}",
        f"--capture={capture}",
        f"--expected-commit={_COMMIT}",
    ]
    if validate is not None:
        command.append(f"--validate={validate}")
    return command


class Phase4ConfirmatoryOperationalMeasurementProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)

        raw_run = subprocess.run(
            raw_command(),
            check=False,
            text=True,
            capture_output=True,
            timeout=360,
        )
        if raw_run.returncode != 0:
            raise RuntimeError(raw_run.stderr)
        cls.raw = json.loads(raw_run.stdout)
        cls.raw["source_commit"] = cls.raw["source_commit"] or ("0" * 40)
        cls.raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            cls.raw
        )
        cls.raw_path = cls.root / "raw.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")

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
            "phase4_confirmatory_operational_replay_worker",
            "phase4_confirmatory_operational_replay_test_worker",
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
            "phase4_confirmatory_operational_replay_worker",
            "phase4_confirmatory_operational_replay_test_worker",
        ):
            (shadow_main / name).symlink_to(cls.hostile_cwd / name)
        capture_environment = os.environ.copy()
        capture_environment["RUNFILES_DIR"] = str(cls.shadow_runfiles)
        capture_environment["PATH"] = str(hostile_bin)
        capture_environment["PYTHONPATH"] = str(hostile_imports)
        capture_environment["PYTHONHOME"] = str(cls.hostile_cwd / "python-home")
        capture_environment["RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS"] = "--version"
        cls.launcher_environment = capture_environment
        capture_run = subprocess.run(
            capture_command(),
            check=False,
            text=True,
            capture_output=True,
            timeout=900,
            cwd=cls.hostile_cwd,
            env=capture_environment,
        )
        if capture_run.returncode != 0:
            raise RuntimeError(capture_run.stderr)
        if cls.worker_canary.exists():
            raise RuntimeError("confirmatory capture selected a hostile-CWD worker")
        if cls.python_canary.exists():
            raise RuntimeError("confirmatory capture selected ambient Python authority")
        cls.capture_bytes = capture_run.stdout
        cls.capture = json.loads(cls.capture_bytes)
        cls.capture_path = cls.root / "capture.json"
        cls.capture_path.write_text(cls.capture_bytes, encoding="utf-8")

        first = subprocess.run(
            publisher_command(cls.raw_path, cls.capture_path),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
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
            env=cls.launcher_environment,
        )
        if second.returncode != 0:
            raise RuntimeError(second.stderr)
        cls.first_publication_bytes = first.stdout
        cls.second_publication_bytes = second.stdout
        cls.publication = json.loads(first.stdout)
        cls.publication_path = cls.root / "publication.json"
        cls.publication_path.write_text(first.stdout, encoding="utf-8")
        if cls.python_canary.exists():
            raise RuntimeError("confirmatory publisher selected ambient Python authority")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_real_four_exec_capture_strictly_joins_raw_and_publication(self) -> None:
        publication_validator.validate_join(
            self.raw,
            self.capture,
            self.publication,
            expected_commit=_COMMIT,
            testing=True,
        )
        raw_pair = self.raw["attempts"][0]["result"]
        self.assertIsNotNone(raw_pair)
        ordinals: list[int] = []
        identities: set[int] = set()
        for index, name in enumerate(("baseline", "candidate")):
            arm = self.capture["arms"][index]
            measured = arm["measured_process"]
            authority = arm["authority_process"]
            ordinals.extend((measured["dispatch_ordinal"], authority["dispatch_ordinal"]))
            identities.update(
                (
                    measured["process_instance_identity"],
                    authority["process_instance_identity"],
                )
            )
            self.assertEqual(arm["measured_worker"]["kind"], 0)
            self.assertEqual(arm["authority_worker"]["kind"], 1)
            measured_semantics = arm["measured_worker"]["payload"]["execution"]["semantics"]
            authority_semantics = arm["authority_worker"]["payload"]["semantics"]
            self.assertEqual(measured_semantics, authority_semantics)
            self.assertEqual(measured_semantics, raw_pair[name]["semantics"])
            self.assertEqual(measured_semantics["corpus_version"], 2)
        self.assertEqual(ordinals, [1, 3, 2, 4])
        self.assertEqual(len(identities), 4)
        self.assertNotIn(0, identities)

    def test_measured_and_authority_process_resources_are_separate(self) -> None:
        for arm in self.capture["arms"]:
            measured = arm["measured_process"]
            authority = arm["authority_process"]
            self.assertEqual(
                measured["total_cpu_nanoseconds"],
                measured["user_cpu_nanoseconds"] + measured["system_cpu_nanoseconds"],
            )
            self.assertGreater(measured["outer_wall_nanoseconds"], 0)
            self.assertGreater(measured["peak_host_bytes"], 0)
            self.assertNotIn("resource_measurements", measured)
            self.assertEqual(
                authority["resource_measurements"],
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
                self.assertNotIn(forbidden, authority)

    def test_publication_rebuild_is_deterministic_calibration_and_nondecision(self) -> None:
        self.assertEqual(self.first_publication_bytes, self.second_publication_bytes)
        validation = subprocess.run(
            publisher_command(
                self.raw_path,
                self.capture_path,
                validate=self.publication_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
            env=self.launcher_environment,
        )
        self.assertEqual(validation.returncode, 0, validation.stderr)
        self.assertEqual(self.publication["campaign_id"], "phase4_confirmatory_corpus_v2")
        self.assertEqual(self.publication["cell_role"], "calibration")
        self.assertTrue(self.publication["eligible_input_to_phase4_aggregation"])
        self.assertTrue(self.publication["cell_operational_telemetry_complete"])
        self.assertFalse(self.publication["standalone_decision_eligible"])
        self.assertFalse(self.publication["statistical_timing_eligible"])
        self.assertFalse(self.publication["coverage_complete"])
        self.assertFalse(self.capture["standalone_publication_eligible"])
        self.assertFalse(self.capture["source_stamped"])
        self.assertTrue(self.capture["source_tree_dirty"])

    def test_legacy_and_confirmatory_authorities_cross_reject(self) -> None:
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_checksum does not match the frozen representative manifest",
        ):
            raw_validator.validate_document(
                self.raw,
                allow_unstamped=True,
                expected_commit=_COMMIT,
            )
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "corpus_version must be 1",
        ):
            operational_validator.validate_capture(
                self.capture,
                expected_commit=None,
                expected_corpus_version=1,
                expected_publication_invocation=_TEST_INVOCATION,
                expected_worker_target=_TEST_WORKER_TARGET,
            )
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "cell is not a frozen executable operational-publication cell",
        ):
            operational_validator.validate_publication(
                self.raw,
                self.capture,
                None,
                self.publication,
            )

    def test_test_worker_never_mints_a_publishable_clean_source_envelope(self) -> None:
        changed = copy.deepcopy(self.capture)
        provenance = changed["reproducibility_provenance"]
        provenance["publication_invocation"] = (
            "bazel --batch run --config=benchmark //:phase4_confirmatory_operational_capture"
        )
        provenance["worker_target"] = "//:phase4_confirmatory_operational_replay_worker"
        production_worker = runfile("phase4_confirmatory_operational_replay_worker")
        production_digest = authority.sha256_file(production_worker)
        provenance["worker_sha256"] = production_digest
        provenance["worker_file_identity"]["sha256"] = production_digest
        payload = {key: value for key, value in provenance.items() if key != "provenance_checksum"}
        hashed = raw_validator.StableHashBuilder()
        hashed.string("APGAR-PHASE4-OPERATIONAL-REPRODUCIBILITY-PROVENANCE-V1")
        hashed.string(
            json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
        )
        provenance["provenance_checksum"] = hashed.finish()
        changed["artifact_checksum"] = operational_validator._capture_checksum(changed)
        changed["source_envelope_checksum"] = operational_validator._capture_source_checksum(
            changed
        )
        clean_raw = copy.deepcopy(self.raw)
        clean_raw["source_commit"] = _COMMIT
        clean_raw["source_stamped"] = True
        clean_raw["source_tree_dirty"] = False
        clean_raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            clean_raw
        )
        with self.assertRaisesRegex(
            operational_validator.EvidenceError,
            "not from the independently expected clean commit",
        ):
            publication_validator.validate_join(
                clean_raw,
                changed,
                self.publication,
                expected_commit=_COMMIT,
            )

    def test_capture_scope_rejects_heldout_and_other_drift_before_execution(self) -> None:
        valid = capture_command()

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in valid
            ]

        mutations = (
            [argument for argument in valid if not argument.startswith("--corpus-version=")],
            replace("corpus-version", "1"),
            replace("raw-wire-schema-version", "2"),
            replace("case-id", "11000"),
            replace("pool-size", "8"),
            replace("workers", "1"),
            replace("setup-ns", "299999999999"),
            replace("maximum-nets", "4095"),
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
                )
                self.assertIn(completed.returncode, {1, 2}, completed.stderr)
                self.assertEqual(completed.stdout, "")
        heldout = subprocess.run(
            replace("case-id", "11000"),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(heldout.returncode, 1)
        self.assertIn("restricted to the frozen", heldout.stderr)

    def test_launcher_ignores_forged_argv_zero(self) -> None:
        forged_argv_zero = self.hostile_cwd / "forged-launcher"
        forged_argv_zero.write_text("#!/bin/sh\nexit 73\n", encoding="utf-8")
        forged_argv_zero.chmod(0o755)
        forged_runfiles = pathlib.Path(f"{forged_argv_zero}.runfiles")
        forged_interpreter = (
            forged_runfiles
            / "_main"
            / "_phase4_confirmatory_operational_capture_test_py.venv"
            / "bin"
            / "python3"
        )
        forged_interpreter.parent.mkdir(parents=True)
        forged_interpreter.symlink_to(self.hostile_cwd / "bin" / "python3")
        command = capture_command()
        command[0] = str(forged_argv_zero)
        command = [
            "--case-id=11000" if argument.startswith("--case-id=") else argument
            for argument in command
        ]
        completed = subprocess.run(
            command,
            executable=str(runfile("phase4_confirmatory_operational_capture_test")),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 1, completed.stderr)
        self.assertIn("restricted to the frozen", completed.stderr)
        self.assertFalse(self.python_canary.exists())

    def test_publisher_scope_rejects_frozen_budget_drift(self) -> None:
        changed = copy.deepcopy(self.raw)
        changed["config"]["maximum_setup_elapsed_nanoseconds"] -= 1
        with self.assertRaisesRegex(
            raw_validator.EvidenceError,
            "restricted to Raw/Wire 1",
        ):
            publication_validator._require_scope(changed)

    def test_production_capture_and_worker_reject_test_escape(self) -> None:
        production_capture = subprocess.run(
            capture_command(production=True),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(production_capture.returncode, 2)
        self.assertEqual(production_capture.stdout, "")

        production_worker = subprocess.run(
            [
                str(runfile("phase4_confirmatory_operational_replay_worker")),
                "--testing_allow_unstamped=1",
                "--corpus_version=2",
                "--raw_wire_schema_version=1",
                "--mode=measured",
                "--arm=baseline",
                "--case_id=10200",
                "--pool_size=4",
                "--workers=4",
                f"--apgar_commit={_COMMIT}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(production_worker.returncode, 2)
        self.assertEqual(production_worker.stdout, "")

    def test_direct_test_worker_rejects_every_frozen_budget_drift_in_preflight(self) -> None:
        base = [
            str(runfile("phase4_confirmatory_operational_replay_test_worker")),
            "--testing_allow_unstamped=1",
            "--corpus_version=2",
            "--raw_wire_schema_version=1",
            "--mode=measured",
            "--arm=baseline",
            "--case_id=10200",
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

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument for argument in base
            ]

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
        ):
            with self.subTest(name=name):
                completed = subprocess.run(
                    replace(name, value),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")
                self.assertNotIn("RLIMIT_AS", completed.stderr)

    def test_publisher_obeys_raw_capture_publication_fifo_open_order(self) -> None:
        raw_fifo = self.root / "raw.fifo"
        capture_fifo = self.root / "capture.fifo"
        publication_fifo = self.root / "publication.fifo"
        os.mkfifo(raw_fifo)
        os.mkfifo(capture_fifo)
        os.mkfifo(publication_fifo)

        raw_first = subprocess.run(
            publisher_command(raw_fifo, capture_fifo, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(raw_first.returncode, 1)
        self.assertIn("raw cell input must resolve to a regular file", raw_first.stderr)
        self.assertNotIn("operational capture input", raw_first.stderr)

        capture_second = subprocess.run(
            publisher_command(self.raw_path, capture_fifo, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(capture_second.returncode, 1)
        self.assertIn(
            "operational capture input must resolve to a regular file",
            capture_second.stderr,
        )
        self.assertNotIn("operational publication input", capture_second.stderr)

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
        join_before_publication = subprocess.run(
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
        self.assertEqual(join_before_publication.returncode, 1)
        self.assertIn("operational capture config differs from Raw", join_before_publication.stderr)
        self.assertNotIn("operational publication input", join_before_publication.stderr)

        publication_third = subprocess.run(
            publisher_command(self.raw_path, self.capture_path, validate=publication_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(publication_third.returncode, 1)
        self.assertIn(
            "operational publication input must resolve to a regular file",
            publication_third.stderr,
        )


if __name__ == "__main__":
    unittest.main()
