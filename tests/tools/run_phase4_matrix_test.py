"""Unit tests for the resumable Phase 4 matrix operator."""

from __future__ import annotations

import json
import os
import pathlib
import signal
import sys
import tempfile
import time
import unittest
from unittest import mock

from tools import run_phase4_matrix as runner


def _wait_for_process_exit(pid: int, timeout: float = 5) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            state = (
                pathlib.Path(f"/proc/{pid}/stat")
                .read_text(encoding="ascii")
                .split(
                    ") ",
                    1,
                )[1][0]
            )
        except FileNotFoundError:
            return True
        if state == "Z":
            return True
        time.sleep(0.01)
    return False


class RunPhase4MatrixTest(unittest.TestCase):
    def test_frozen_rows_and_authority_versions_are_derived_from_protocol(self) -> None:
        rows = runner._matrix_rows()
        self.assertEqual(len(rows), 104)
        self.assertEqual(len(set(rows)), 104)
        self.assertEqual(sum(row[3] == "raw_success" for row in rows), 22)
        self.assertEqual(sum(row[3] == "same_run_raw_success" for row in rows), 78)
        self.assertEqual(sum(row[3] == "descriptor_only_excluded" for row in rows), 2)
        self.assertEqual(sum(row[3] == "compiled_work_bound" for row in rows), 2)

    def test_common_arguments_are_the_canonical_execution_envelope(self) -> None:
        arguments = runner._common_cell_arguments(100, 4, "a" * 40)
        self.assertIn("--workers=4", arguments)
        self.assertIn("--repetitions=20", arguments)
        self.assertIn("--setup_ns=300000000000", arguments)
        self.assertIn("--prepared_ns=300000000000", arguments)
        self.assertIn("--cold_ns=300000000000", arguments)
        self.assertIn("--address_space_bytes=68719476736", arguments)
        self.assertIn("--peak_host_bytes=17179869184", arguments)
        self.assertIn("--maximum_compiled_nodes=100000000", arguments)

    def test_child_tools_do_not_inherit_the_runner_runfiles_context(self) -> None:
        variables = {
            "RUNFILES_DIR": "/wrong/runfiles",
            "RUNFILES_MANIFEST_FILE": "/wrong/manifest",
            "TEST_SRCDIR": "/wrong/test",
            "TEST_WORKSPACE": "wrong",
            "PYTHON_RUNFILES": "/wrong/python",
            "JAVA_RUNFILES": "/wrong/java",
        }
        script = "import json,os;print(json.dumps(dict(os.environ),sort_keys=True))"
        with mock.patch.dict(os.environ, variables):
            result = runner._checked(
                [sys.executable, "-c", script],
                cwd=pathlib.Path.cwd(),
                timeout=30,
            )
        child_environment = json.loads(result)
        self.assertTrue(set(variables).isdisjoint(child_environment))

    def test_outer_bazel_run_is_rejected_before_nested_bazel_deadlock(self) -> None:
        with mock.patch.dict(
            os.environ,
            {"BUILD_WORKSPACE_DIRECTORY": "/workspace"},
        ):
            with self.assertRaisesRegex(runner.RunError, "holds the Bazel output-base lock"):
                runner._operator_repository()

    def test_command_timeout_contains_a_setsid_escapee(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_path = pathlib.Path(temporary) / "escapee.pid"
            script = (
                "import os,pathlib,time;"
                "child=os.fork();"
                "\nif child == 0:\n"
                " os.setsid();os.close(1);os.close(2);"
                f"pathlib.Path({str(pid_path)!r}).write_text("
                "str(os.getpid()),encoding='ascii');"
                "time.sleep(30);os._exit(0)\n"
                "time.sleep(30)"
            )
            started = time.monotonic()
            with self.assertRaises(runner.CommandTimeout):
                runner._run(
                    [sys.executable, "-c", script],
                    cwd=pathlib.Path.cwd(),
                    timeout=1,
                )
            self.assertLess(time.monotonic() - started, 5)
            escapee_pid = int(pid_path.read_text(encoding="ascii"))
            if not _wait_for_process_exit(escapee_pid):
                os.kill(escapee_pid, signal.SIGKILL)
                self.fail("setsid escapee survived matrix command timeout")

    def test_keyboard_interrupt_terminates_the_active_command(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_path = pathlib.Path(temporary) / "child.pid"
            controller = os.fork()
            if controller == 0:
                try:
                    runner._run(
                        [
                            sys.executable,
                            "-c",
                            (
                                "import os,pathlib,time;"
                                "child=os.fork();"
                                "\nif child == 0:\n"
                                " os.setsid();os.close(1);os.close(2);"
                                f"pathlib.Path({str(pid_path)!r}).write_text("
                                "str(os.getpid()),encoding='ascii');"
                                "time.sleep(30);os._exit(0)\n"
                                "time.sleep(30)"
                            ),
                        ],
                        cwd=pathlib.Path.cwd(),
                        timeout=60,
                    )
                except KeyboardInterrupt:
                    os._exit(0)
                os._exit(1)
            deadline = time.monotonic() + 5
            encoded_pid = ""
            while time.monotonic() < deadline:
                try:
                    encoded_pid = pid_path.read_text(encoding="ascii")
                except FileNotFoundError:
                    pass
                if encoded_pid.isdecimal():
                    break
                time.sleep(0.01)
            if not encoded_pid.isdecimal():
                os.kill(controller, signal.SIGKILL)
                os.waitpid(controller, 0)
                self.fail("setsid escapee did not publish its PID")
            child_pid = int(encoded_pid)
            os.kill(controller, signal.SIGINT)
            _, status = os.waitpid(controller, 0)
            self.assertEqual(os.waitstatus_to_exitcode(status), 0)
            if not _wait_for_process_exit(child_pid):
                os.kill(child_pid, signal.SIGKILL)
                self.fail("setsid escapee survived matrix-runner interruption")

    def test_sigterm_terminates_the_active_command_and_setsid_escapee(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_path = pathlib.Path(temporary) / "child.pid"
            controller = os.fork()
            if controller == 0:
                try:
                    runner._run(
                        [
                            sys.executable,
                            "-c",
                            (
                                "import os,pathlib,time;"
                                "child=os.fork();"
                                "\nif child == 0:\n"
                                " os.setsid();os.close(1);os.close(2);"
                                f"pathlib.Path({str(pid_path)!r}).write_text("
                                "str(os.getpid()),encoding='ascii');"
                                "time.sleep(30);os._exit(0)\n"
                                "time.sleep(30)"
                            ),
                        ],
                        cwd=pathlib.Path.cwd(),
                        timeout=60,
                    )
                except runner.CommandInterrupted as error:
                    os._exit(0 if error.signal_number == signal.SIGTERM else 2)
                os._exit(1)
            deadline = time.monotonic() + 5
            encoded_pid = ""
            while time.monotonic() < deadline:
                try:
                    encoded_pid = pid_path.read_text(encoding="ascii")
                except FileNotFoundError:
                    pass
                if encoded_pid.isdecimal():
                    break
                time.sleep(0.01)
            if not encoded_pid.isdecimal():
                os.kill(controller, signal.SIGKILL)
                os.waitpid(controller, 0)
                self.fail("setsid escapee did not publish its PID")
            child_pid = int(encoded_pid)
            os.kill(controller, signal.SIGTERM)
            _, status = os.waitpid(controller, 0)
            self.assertEqual(os.waitstatus_to_exitcode(status), 0)
            if not _wait_for_process_exit(child_pid):
                os.kill(child_pid, signal.SIGKILL)
                self.fail("setsid escapee survived matrix-runner SIGTERM")

    def test_repeated_mixed_signals_cannot_interrupt_descendant_cleanup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            pid_path = pathlib.Path(temporary) / "child.pid"
            controller = os.fork()
            if controller == 0:
                try:
                    runner._run(
                        [
                            sys.executable,
                            "-c",
                            (
                                "import os,pathlib,time;"
                                "child=os.fork();"
                                "\nif child == 0:\n"
                                " os.setsid();os.close(1);os.close(2);"
                                f"pathlib.Path({str(pid_path)!r}).write_text("
                                "str(os.getpid()),encoding='ascii');"
                                "time.sleep(30);os._exit(0)\n"
                                "time.sleep(30)"
                            ),
                        ],
                        cwd=pathlib.Path.cwd(),
                        timeout=60,
                    )
                except KeyboardInterrupt:
                    os._exit(0)
                except runner.CommandInterrupted:
                    os._exit(0)
                os._exit(1)
            deadline = time.monotonic() + 5
            encoded_pid = ""
            while time.monotonic() < deadline:
                try:
                    encoded_pid = pid_path.read_text(encoding="ascii")
                except FileNotFoundError:
                    pass
                if encoded_pid.isdecimal():
                    break
                time.sleep(0.01)
            if not encoded_pid.isdecimal():
                os.kill(controller, signal.SIGKILL)
                os.waitpid(controller, 0)
                self.fail("setsid escapee did not publish its PID")
            child_pid = int(encoded_pid)
            signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
            for index in range(300):
                try:
                    os.kill(controller, signals[index % len(signals)])
                except ProcessLookupError:
                    break
                time.sleep(0.001)
            os.waitpid(controller, 0)
            if not _wait_for_process_exit(child_pid):
                os.kill(child_pid, signal.SIGKILL)
                self.fail("setsid escapee survived repeated mixed signals")

    def test_signal_during_handler_install_does_not_break_restoration(self) -> None:
        termination_signals = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
        original_handlers = {
            signal_number: signal.getsignal(signal_number) for signal_number in termination_signals
        }
        real_signal = signal.signal
        delivered = False

        def install_and_interrupt(signal_number: int, handler: object) -> object:
            nonlocal delivered
            previous = real_signal(signal_number, handler)
            if not delivered and signal_number == signal.SIGINT:
                delivered = True
                os.kill(os.getpid(), signal.SIGINT)
            return previous

        with mock.patch.object(runner.signal, "signal", side_effect=install_and_interrupt):
            with self.assertRaises(KeyboardInterrupt):
                runner._run(
                    [sys.executable, "-c", "raise SystemExit(99)"],
                    cwd=pathlib.Path.cwd(),
                    timeout=30,
                )
        self.assertTrue(delivered)
        self.assertEqual(
            {
                signal_number: signal.getsignal(signal_number)
                for signal_number in termination_signals
            },
            original_handlers,
        )

    def test_signal_during_subreaper_enable_does_not_break_restoration(self) -> None:
        real_set_subreaper = runner.capture_tool._set_child_subreaper
        original_state = runner.capture_tool._child_subreaper_enabled()
        if original_state:
            real_set_subreaper(False)
        delivered = False

        def enable_and_interrupt(enabled: bool) -> None:
            nonlocal delivered
            real_set_subreaper(enabled)
            if enabled and not delivered:
                delivered = True
                os.kill(os.getpid(), signal.SIGINT)

        try:
            with mock.patch.object(
                runner.capture_tool,
                "_set_child_subreaper",
                side_effect=enable_and_interrupt,
            ):
                with self.assertRaises(KeyboardInterrupt):
                    runner._run(
                        [sys.executable, "-c", "raise SystemExit(99)"],
                        cwd=pathlib.Path.cwd(),
                        timeout=30,
                    )
            self.assertTrue(delivered)
            self.assertFalse(runner.capture_tool._child_subreaper_enabled())
        finally:
            real_set_subreaper(original_state)

    def test_cleanup_failure_retains_subreaper_authority(self) -> None:
        controller = os.fork()
        if controller == 0:
            try:
                if runner.capture_tool._child_subreaper_enabled():
                    runner.capture_tool._set_child_subreaper(False)
                with mock.patch.object(
                    runner.capture_tool,
                    "_terminate_adopted_descendants",
                    side_effect=RuntimeError("injected cleanup failure"),
                ):
                    try:
                        runner._run(
                            [sys.executable, "-c", "pass"],
                            cwd=pathlib.Path.cwd(),
                            timeout=30,
                        )
                    except RuntimeError:
                        os._exit(0 if runner.capture_tool._child_subreaper_enabled() else 2)
            except BaseException:
                os._exit(3)
            os._exit(4)
        _, status = os.waitpid(controller, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 0)

    def test_same_run_pair_is_installed_together_and_resumes_together(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            cell = root / "cells/0100/k4"
            raw = cell / "raw.json"
            sidecar = cell / "same-run.json"
            script = (
                "import pathlib,sys;"
                "p=pathlib.Path(sys.argv[-1].split('=',1)[1]);"
                "p.write_text('{\"artifact_checksum\":2}\\n',encoding='utf-8');"
                "sys.stdout.write('{\"artifact_checksum\":1}\\n')"
            )
            command = [sys.executable, "-c", script]
            with mock.patch.object(runner.same_run, "validate_join"):
                runner._write_raw_v2_pair(
                    command,
                    raw,
                    sidecar,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )
            self.assertEqual(raw.read_text(encoding="utf-8"), '{"artifact_checksum":1}\n')
            self.assertEqual(
                sidecar.read_text(encoding="utf-8"),
                '{"artifact_checksum":2}\n',
            )
            runner._write_raw_v2_pair(
                command,
                raw,
                sidecar,
                root=root,
                repo=root,
                expected_commit="a" * 40,
                timeout=30,
            )
            sidecar.unlink()
            with self.assertRaisesRegex(runner.RunError, "only one half"):
                runner._write_raw_v2_pair(
                    command,
                    raw,
                    sidecar,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )

    def test_invalid_success_pair_is_logged_and_leaves_no_cell_temporary(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            cell = root / "cells/0100/k4"
            raw = cell / "raw.json"
            sidecar = cell / "same-run.json"
            script = (
                "import pathlib,sys;"
                "p=pathlib.Path(sys.argv[-1].split('=',1)[1]);"
                "p.parent.mkdir(parents=True,exist_ok=True);"
                "p.write_text('{\"artifact_checksum\":2}\\n',encoding='utf-8');"
                "sys.stdout.write('{\"artifact_checksum\":1}\\n');"
                "sys.stderr.write('unexpected warning\\n')"
            )
            with self.assertRaisesRegex(runner.RunError, "retained under logs"):
                runner._write_raw_v2_pair(
                    [sys.executable, "-c", script],
                    raw,
                    sidecar,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )
            self.assertFalse(raw.exists())
            self.assertFalse(sidecar.exists())
            self.assertEqual(list(cell.glob(".same-run.runner-*.json")), [])
            self.assertEqual(len(list((root / "logs").glob("failure-*.json"))), 1)

    def test_quiet_invalid_raw_v1_never_reaches_canonical_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            raw = root / "cells/0200/k4/raw.json"
            with self.assertRaisesRegex(runner.RunError, "invalid evidence"):
                runner._write_raw_v1(
                    [sys.executable, "-c", "print('{}')"],
                    raw,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )
            self.assertFalse(raw.exists())
            self.assertEqual(len(list((root / "logs").glob("failure-*.json"))), 1)

    def test_raw_v2_join_rejection_never_installs_either_half(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            cell = root / "cells/0100/k4"
            raw = cell / "raw.json"
            sidecar = cell / "same-run.json"
            script = (
                "import pathlib,sys;"
                "p=pathlib.Path(sys.argv[-1].split('=',1)[1]);"
                "p.parent.mkdir(parents=True,exist_ok=True);"
                "p.write_text('{\"artifact_checksum\":2}\\n',encoding='utf-8');"
                "sys.stdout.write('{\"artifact_checksum\":1}\\n')"
            )
            rejection = runner.raw_validator.EvidenceError("pair mismatch")
            with (
                mock.patch.object(runner.same_run, "validate_join", side_effect=rejection),
                self.assertRaisesRegex(runner.RunError, "invalid pair"),
            ):
                runner._write_raw_v2_pair(
                    [sys.executable, "-c", script],
                    raw,
                    sidecar,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )
            self.assertFalse(raw.exists())
            self.assertFalse(sidecar.exists())
            self.assertEqual(list(cell.glob(".same-run.runner-*.json")), [])
            self.assertEqual(len(list((root / "logs").glob("failure-*.json"))), 1)

    def test_atomic_install_never_replaces_an_existing_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "artifact.json"
            runner._install_new(path, b"first\n")
            with self.assertRaisesRegex(runner.RunError, "already exists"):
                runner._install_new(path, b"second\n")
            self.assertEqual(path.read_bytes(), b"first\n")

    def test_evidence_root_allows_only_one_matrix_runner(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            with runner._exclusive_run_lock(root):
                with self.assertRaisesRegex(runner.RunError, "holds the evidence-root lock"):
                    with runner._exclusive_run_lock(root):
                        self.fail("a concurrent matrix runner acquired the same evidence root")

    def test_evidence_root_requires_a_commit_bound_ownership_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "foreign").write_text("not phase 4\n", encoding="utf-8")
            with self.assertRaisesRegex(runner.RunError, "nonempty evidence root"):
                runner._prepare_evidence_root(root, "a" * 40)
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            runner._prepare_evidence_root(root, "a" * 40)
            runner._prepare_evidence_root(root, "a" * 40)
            stale = root / "cells/0100/k4/.same-run.runner-stale.json"
            stale.parent.mkdir(parents=True)
            stale.touch()
            with self.assertRaisesRegex(runner.RunError, "unexpected file"):
                runner._prepare_evidence_root(root, "a" * 40)
            stale.unlink()
            with self.assertRaisesRegex(runner.RunError, "marker is invalid"):
                runner._prepare_evidence_root(root, "b" * 40)

    def test_malformed_resumed_raw_cannot_launch_downstream_producers(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            raw = root / "cells/0100/k4/raw.json"
            raw.parent.mkdir(parents=True)
            raw.write_text("{}\n", encoding="utf-8")
            tools = {"phase4_evidence_runner": pathlib.Path("/not-executed")}
            with mock.patch.object(runner, "_write_command_output") as downstream:
                with self.assertRaisesRegex(runner.RunError, "raw authority is invalid"):
                    runner._acquire_success_cell(
                        tools=tools,
                        repo=root,
                        root=root,
                        fixture=root / "fixture.kicad_pcb",
                        commit="a" * 40,
                        case_id=100,
                        pool=4,
                        evidence="raw_success",
                        role="calibration",
                        timeout=30,
                    )
            downstream.assert_not_called()

    def test_failed_raw_attempt_is_retained_outside_canonical_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            raw = root / "cells/0200/k4/raw.json"
            with self.assertRaisesRegex(runner.RunError, "retained under logs"):
                runner._write_raw_v1(
                    [
                        sys.executable,
                        "-c",
                        "import sys;sys.stdout.write('{\"failed\":true}\\n');sys.exit(7)",
                    ],
                    raw,
                    root=root,
                    repo=root,
                    expected_commit="a" * 40,
                    timeout=30,
                )
            logs = list((root / "logs").glob("failure-*.json"))
            self.assertEqual(len(logs), 1)
            self.assertFalse(raw.exists())
            self.assertIn('"returncode":7', logs[0].read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
