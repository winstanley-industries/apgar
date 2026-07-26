"""Acquisition-free process coverage for the operational publisher launcher."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

from tests.support import phase4_confirmatory_operational_test_artifacts as artifacts
from tools import phase4_confirmatory_same_run_operational_authority as authority
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_COMMIT = "a" * 40
_TEST_PUBLISHER = "phase4_confirmatory_same_run_operational_measurement_test_validator"
_PRODUCTION_PUBLISHER = "phase4_confirmatory_same_run_operational_measurement_validator"
_TEST_INNER = f"{_TEST_PUBLISHER}_py"
_PRODUCTION_INNER = f"{_PRODUCTION_PUBLISHER}_py"
_LIFETIME_PROBE = "phase4_confirmatory_operational_launcher_lifetime_probe"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def ignore_sigchld() -> None:
    signal.signal(signal.SIGCHLD, signal.SIG_IGN)


def close_stdin() -> None:
    os.close(0)


def publisher_command(
    raw: pathlib.Path,
    sidecar: pathlib.Path,
    capture: pathlib.Path,
    *,
    launcher: pathlib.Path | None = None,
    validate: pathlib.Path | None = None,
) -> list[str]:
    command = [
        str(launcher or runfile(_TEST_PUBLISHER)),
        f"--raw={raw}",
        f"--same-run-telemetry={sidecar}",
        f"--capture={capture}",
        f"--expected-commit={_COMMIT}",
    ]
    if validate is not None:
        command.append(f"--validate={validate}")
    return command


class Phase4ConfirmatoryOperationalPublisherLauncherProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.raw, sidecar, cls.capture = artifacts.make_operational_inputs(
            case_id=10_100,
            pool=4,
            same_run=True,
            h4096=False,
            authority_module=authority,
            commit=_COMMIT,
        )
        if sidecar is None:
            raise AssertionError("same-run operational input is missing telemetry")
        cls.sidecar = sidecar
        cls.publication = operational_validator.project_confirmatory_same_run_document(
            cls.raw,
            cls.sidecar,
            cls.capture,
        )
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        cls.capture_path = cls.root / "capture.json"
        cls.publication_path = cls.root / "publication.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")
        cls.capture_path.write_text(canonical(cls.capture), encoding="utf-8")
        cls.publication_path.write_text(canonical(cls.publication), encoding="utf-8")

        cls.hostile_cwd = cls.root / "hostile-cwd"
        cls.hostile_cwd.mkdir()
        cls.python_canary = cls.hostile_cwd / "ambient-python-ran"
        cls.worker_canary = cls.hostile_cwd / "ambient-worker-ran"
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
            forged_worker = cls.hostile_cwd / name
            forged_worker.write_text(
                f"#!/bin/sh\n: > '{cls.worker_canary}'\nexit 71\n",
                encoding="utf-8",
            )
            forged_worker.chmod(0o755)

        cls.shadow_runfiles = cls.root / "shadow.runfiles"
        shadow_main = cls.shadow_runfiles / "_main"
        shadow_main.mkdir(parents=True)
        for target in (_PRODUCTION_INNER, _TEST_INNER):
            forged_inner = shadow_main / target
            forged_inner.write_text(
                f"#!/bin/sh\n: > '{cls.python_canary}'\nexit 70\n",
                encoding="utf-8",
            )
            forged_inner.chmod(0o755)
            forged_interpreter = shadow_main / f"_{target}.venv" / "bin" / "python3"
            forged_interpreter.parent.mkdir(parents=True)
            forged_interpreter.symlink_to(hostile_python)
        for name in (authority.PRODUCTION_WORKER, authority.TEST_WORKER):
            (shadow_main / name).symlink_to(cls.hostile_cwd / name)

        cls.launcher_environment = os.environ.copy()
        cls.launcher_environment["RUNFILES_DIR"] = str(cls.shadow_runfiles)
        cls.launcher_environment["RUNFILES_MANIFEST_FILE"] = str(cls.shadow_runfiles / "MANIFEST")
        cls.launcher_environment["JAVA_RUNFILES"] = str(cls.shadow_runfiles)
        cls.launcher_environment["PATH"] = str(hostile_bin)
        cls.launcher_environment["PYTHONPATH"] = str(hostile_imports)
        cls.launcher_environment["PYTHONHOME"] = str(cls.hostile_cwd / "python-home")
        cls.launcher_environment["RULES_PYTHON_ADDITIONAL_INTERPRETER_ARGS"] = "--version"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_compiled_publisher_uses_only_its_adjacent_authenticated_runfiles(self) -> None:
        test_tree_launcher = runfile(_TEST_PUBLISHER)
        standalone_launcher = test_tree_launcher.resolve()
        self.assertTrue(pathlib.Path(f"{standalone_launcher}.runfiles").is_dir())
        publications = []
        for launcher in (test_tree_launcher, standalone_launcher):
            with self.subTest(launcher=launcher):
                completed = subprocess.run(
                    publisher_command(
                        self.raw_path,
                        self.sidecar_path,
                        self.capture_path,
                        launcher=launcher,
                    ),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=30,
                    cwd=self.hostile_cwd,
                    env=self.launcher_environment,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertEqual(json.loads(completed.stdout), self.publication)
                publications.append(completed.stdout)
        self.assertEqual(publications[0], publications[1])
        self.assertFalse(self.python_canary.exists())
        self.assertFalse(self.worker_canary.exists())

    def test_publisher_standalone_trees_are_fixtureless_and_target_specific(self) -> None:
        launchers = {
            _PRODUCTION_PUBLISHER: authority.PRODUCTION_WORKER,
            _TEST_PUBLISHER: authority.TEST_WORKER,
        }
        for launcher_name, expected_worker in launchers.items():
            with self.subTest(launcher=launcher_name):
                launcher = runfile(launcher_name).resolve()
                main = pathlib.Path(f"{launcher}.runfiles") / "_main"
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

    def test_launcher_rejects_forged_argv_zero_and_adjacent_tree(self) -> None:
        forged = self.hostile_cwd / "forged-publisher"
        forged.write_text("#!/bin/sh\nexit 73\n", encoding="utf-8")
        forged.chmod(0o755)
        forged_interpreter = (
            pathlib.Path(f"{forged}.runfiles")
            / "_main"
            / f"_{_TEST_INNER}.venv"
            / "bin"
            / "python3"
        )
        forged_interpreter.parent.mkdir(parents=True)
        forged_interpreter.symlink_to(self.hostile_cwd / "bin" / "python3")
        command = publisher_command(self.raw_path, self.sidecar_path, self.capture_path)
        command[0] = str(forged)
        completed = subprocess.run(
            command,
            executable=str(runfile(_TEST_PUBLISHER)),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=self.launcher_environment,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertIn("cannot authenticate its bundled runfiles", completed.stderr)
        self.assertFalse(self.python_canary.exists())

    def test_inner_python_entrypoints_reject_direct_or_forged_one_use_handshakes(
        self,
    ) -> None:
        for target in (_PRODUCTION_INNER, _TEST_INNER):
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

        read_descriptor, write_descriptor = os.pipe()
        try:
            os.write(
                write_descriptor,
                (f"APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-LAUNCH-V1\n{_TEST_INNER}\n").encode(),
            )
            os.close(write_descriptor)
            write_descriptor = -1
            forged_environment = os.environ.copy()
            forged_environment["APGAR_PHASE4_CONFIRMATORY_OPERATIONAL_LAUNCH_FD"] = str(
                read_descriptor
            )
            completed = subprocess.run(
                [str(runfile(_TEST_INNER)), "--help"],
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
                [str(runfile(_TEST_INNER)), "--help"],
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
            [str(runfile(_TEST_INNER)), "--help"],
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

    def test_launcher_skips_stage_one_site_initialization(self) -> None:
        canonical_launcher = runfile(_LIFETIME_PROBE).resolve()
        declared_sitecustomize = runfile("sitecustomize.py")
        broad_sitecustomize = canonical_launcher.parent / "sitecustomize.py"
        self.assertTrue(declared_sitecustomize.is_file())
        self.assertTrue(broad_sitecustomize.is_file())
        self.assertTrue(os.path.samefile(broad_sitecustomize, declared_sitecustomize))
        marker = self.root / "stage-one-operational-sitecustomize-ran"
        environment = self.launcher_environment.copy()
        environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
        probe_interpreter = (
            pathlib.Path(f"{canonical_launcher}.runfiles")
            / "_main"
            / f"_{_LIFETIME_PROBE}_py.venv"
            / "bin"
            / "python3"
        )
        probe = subprocess.run(
            [str(probe_interpreter), "-I", "-B", "-c", "pass"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(probe.returncode, 0, probe.stderr)
        self.assertTrue(marker.is_file())
        marker.unlink()
        completed = subprocess.run(
            [str(runfile(_LIFETIME_PROBE)), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)
        self.assertFalse(marker.exists())

    def test_launcher_ignores_manifest_declared_enclosing_package_init(self) -> None:
        shadow = self.root / "superset-launcher.runfiles"
        shutil.copytree(
            pathlib.Path(os.environ["TEST_SRCDIR"]),
            shadow,
            symlinks=True,
        )
        marker = self.root / "enclosing-operational-package-init-ran"
        main = shadow / os.environ["TEST_WORKSPACE"]
        package_init = main / "tools" / "__init__.py"
        self.assertTrue(package_init.is_file())
        repository_mapping = (shadow / "_repo_mapping").resolve()
        self.assertTrue(repository_mapping.name.endswith(".repo_mapping"))
        manifest = repository_mapping.with_name(
            repository_mapping.name.removesuffix(".repo_mapping") + ".runfiles_manifest"
        )
        logical_init = f"{os.environ['TEST_WORKSPACE']}/tools/__init__.py "
        self.assertTrue(
            any(
                line.startswith(logical_init)
                for line in manifest.read_text(encoding="utf-8").splitlines()
            )
        )
        environment = self.launcher_environment.copy()
        environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
        completed = subprocess.run(
            [str(main / _LIFETIME_PROBE), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)
        self.assertFalse(marker.exists())

    def test_launcher_ignores_an_incompletely_traversable_enclosing_tree(self) -> None:
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
                        [str(main / _LIFETIME_PROBE), "--help"],
                        check=False,
                        text=True,
                        capture_output=True,
                        timeout=10,
                        env=self.launcher_environment,
                    )
                finally:
                    subtree.chmod(0o755)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)

    def test_launcher_parent_death_terminates_the_delegated_authority(self) -> None:
        cache_directories_before = set(pathlib.Path("/tmp").glob("apgar-phase4-python-cache-*"))
        ready = self.root / "launcher-lifetime-ready"
        release = self.root / "launcher-lifetime-release"
        canary = self.root / "launcher-lifetime-canary"
        process = subprocess.Popen(
            [
                str(runfile(_LIFETIME_PROBE)),
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
            [str(runfile(_LIFETIME_PROBE)), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            preexec_fn=ignore_sigchld,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)
        self.assertEqual(completed.stderr, "")

    def test_launcher_promotes_the_handshake_above_closed_stdin(self) -> None:
        completed = subprocess.run(
            [str(runfile(_LIFETIME_PROBE)), "--help"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            preexec_fn=close_stdin,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)
        self.assertEqual(completed.stderr, "")

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
        mismatched_sidecar["source_commit"] = mismatched_raw["source_commit"]
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


if __name__ == "__main__":
    unittest.main()
