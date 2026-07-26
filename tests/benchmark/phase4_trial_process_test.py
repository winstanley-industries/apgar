import fcntl
import json
import os
import pathlib
import shutil
import signal
import subprocess
import tempfile
import time
import unittest


def runfile(relative: str) -> pathlib.Path:
    source_root = pathlib.Path(os.environ["TEST_SRCDIR"])
    workspace = os.environ["TEST_WORKSPACE"]
    return source_root / workspace / relative


def base_command(
    repetitions: int,
    cold_nanoseconds: int,
    *,
    executable: str = "phase4_evidence_runner",
    setup_nanoseconds: int = 60_000_000_000,
) -> list[str]:
    return [
        str(runfile(executable)),
        "--testing_allow_unstamped=1",
        "--case_id=100",
        "--pool_size=4",
        "--workers=1",
        f"--repetitions={repetitions}",
        f"--setup_ns={setup_nanoseconds}",
        f"--prepared_ns={min(cold_nanoseconds, 60_000_000_000)}",
        f"--cold_ns={cold_nanoseconds}",
        # ASan reserves a very large virtual shadow range before main. Keep the
        # test cap finite and exactly enforced while leaving that reservation
        # available; production evidence supplies its own tighter declared cap.
        "--address_space_bytes=140737488355328",
        "--peak_host_bytes=8589934592",
    ]


class Phase4TrialProcessTest(unittest.TestCase):
    def test_workers_reexecute_the_controller_inode_after_path_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary_path = pathlib.Path(temporary)
            controller_path = temporary_path / "phase4_evidence_fault_runner"
            replacement_path = temporary_path / "replacement_runner"
            shutil.copy2(runfile("phase4_evidence_fault_runner"), controller_path)
            shutil.copy2(
                runfile("phase4_confirmatory_h4096_evidence_test_runner"),
                replacement_path,
            )
            command = base_command(1, 60_000_000_000)
            command[0] = str(controller_path)
            environment = os.environ.copy()
            environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "executable_path_replacement"
            process = subprocess.Popen(
                command,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=environment,
            )
            try:
                stopped_status = None
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    waited_pid, status = os.waitpid(
                        process.pid,
                        os.WNOHANG | os.WUNTRACED,
                    )
                    if waited_pid == process.pid:
                        stopped_status = status
                        break
                    time.sleep(0.01)
                self.assertIsNotNone(stopped_status)
                self.assertTrue(os.WIFSTOPPED(stopped_status))
                self.assertEqual(os.WSTOPSIG(stopped_status), signal.SIGSTOP)

                os.replace(replacement_path, controller_path)
                os.kill(process.pid, signal.SIGCONT)
                stdout, stderr = process.communicate(timeout=30)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=10)

            self.assertEqual(process.returncode, 0, stderr)
            artifact = json.loads(stdout)
            self.assertIsNotNone(artifact["attempts"][0]["result"])

            replacement = subprocess.run(
                command,
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
                env=environment,
            )
            self.assertEqual(replacement.returncode, 2)

    def test_two_repetitions_reuse_distinct_long_lived_workers(self) -> None:
        completed = subprocess.run(
            base_command(2, 60_000_000_000),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        artifact = json.loads(completed.stdout)
        self.assertEqual(len(artifact["attempts"]), 2)
        first, second = artifact["attempts"]
        self.assertEqual((first["execution_order"], second["execution_order"]), (0, 1))
        self.assertEqual(
            (
                first["baseline"]["dispatch_ordinal"],
                first["candidate"]["dispatch_ordinal"],
                second["candidate"]["dispatch_ordinal"],
                second["baseline"]["dispatch_ordinal"],
            ),
            (1, 2, 3, 4),
        )
        baseline_processes = {
            attempt["baseline"]["process_instance_identity"] for attempt in artifact["attempts"]
        }
        candidate_processes = {
            attempt["candidate"]["process_instance_identity"] for attempt in artifact["attempts"]
        }
        self.assertEqual(len(baseline_processes), 1)
        self.assertEqual(len(candidate_processes), 1)
        self.assertNotEqual(baseline_processes, candidate_processes)
        for arm in ("baseline", "candidate"):
            peaks = {
                attempt[arm]["process_lifetime_peak_host_bytes"] for attempt in artifact["attempts"]
            }
            self.assertEqual(len(peaks), 1)
            self.assertGreater(next(iter(peaks)), 0)
        self.assertTrue(all(attempt["result"] is not None for attempt in artifact["attempts"]))
        first_lifecycle = first["candidate"]["record"]["preparer_lifecycle"]
        second_lifecycle = second["candidate"]["record"]["preparer_lifecycle"]
        self.assertGreater(first_lifecycle["invocations_completed_before"], 0)
        self.assertEqual(
            first_lifecycle["invocations_completed_after"],
            second_lifecycle["invocations_completed_before"],
        )

    def test_wall_timeout_is_incomplete_attempt(self) -> None:
        completed = subprocess.run(
            base_command(1, 1),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1, completed.stderr)
        artifact = json.loads(completed.stdout)
        attempt = artifact["attempts"][0]
        dispositions = {
            attempt["baseline"]["disposition"],
            attempt["candidate"]["disposition"],
        }
        self.assertIn(3, dispositions)
        timed_out = (
            attempt["baseline"] if attempt["baseline"]["disposition"] == 3 else attempt["candidate"]
        )
        self.assertTrue(timed_out["watchdog_kill_sent"])
        self.assertIsNone(attempt["result"])

    def run_fault(self, mode: str, *, setup_nanoseconds: int = 1_000_000_000) -> dict:
        environment = os.environ.copy()
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = mode
        completed = subprocess.run(
            base_command(
                1,
                60_000_000_000,
                executable="phase4_evidence_fault_runner",
                setup_nanoseconds=setup_nanoseconds,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(completed.returncode, 1, completed.stderr)
        return json.loads(completed.stdout)

    def test_coalesced_second_frame_is_protocol_failure_not_timeout(self) -> None:
        artifact = self.run_fault("double_ready")
        baseline = artifact["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 6)
        self.assertEqual(baseline["controller_invariant_id"], "P4HARNESS-FRAME-TRAILING-001")
        self.assertFalse(baseline["watchdog_kill_sent"])

    def test_split_second_ready_has_authenticated_state_failure(self) -> None:
        artifact = self.run_fault("delayed_double_ready")
        baseline = artifact["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 6)
        self.assertEqual(baseline["controller_invariant_id"], "P4HARNESS-RUN-STATE-001")
        self.assertTrue(baseline["controller_detail"])
        self.assertFalse(baseline["watchdog_kill_sent"])

    def test_child_exit_before_launch_gate_does_not_signal_controller(self) -> None:
        artifact = self.run_fault("exit_before_launch_gate")
        baseline = artifact["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 7)
        self.assertEqual(baseline["controller_invariant_id"], "P4HARNESS-LAUNCH-GATE-001")
        self.assertTrue(baseline["controller_detail"])
        self.assertIsNone(artifact["attempts"][0]["result"])

    def test_preparer_factory_resource_failure_is_encodable_summary(self) -> None:
        artifact = self.run_fault("preparer_factory_resource_failure")
        candidate = artifact["attempts"][0]["candidate"]
        self.assertEqual(candidate["disposition"], 1)
        self.assertEqual(candidate["child_failure"]["summary_code"], 13)
        self.assertEqual(candidate["child_failure"]["payload_kind"], 0)
        self.assertEqual(
            candidate["child_failure"]["summary_invariant_id"],
            "allocator.cpu_candidate_pool.preparer_thread.v1",
        )

    def test_earlier_typed_failure_retains_unavailable_reap_authority(self) -> None:
        artifact = self.run_fault("preparer_factory_resource_failure_reap_unavailable")
        candidate = artifact["attempts"][0]["candidate"]
        self.assertEqual(candidate["disposition"], 1)
        self.assertIsNotNone(candidate["child_failure"])
        self.assertEqual(candidate["controller_invariant_id"], "P4HARNESS-REAP-BOUNDED-001")
        self.assertEqual(candidate["process_lifetime_peak_host_bytes"], 0)
        self.assertEqual(candidate["raw_wait_status"], 0)
        self.assertEqual(candidate["process_exit_code"], -1)

    def test_ready_and_later_failure_must_match_warmup_case_identity(self) -> None:
        ready = self.run_fault("ready_bad_identity")
        for arm in ("baseline", "candidate"):
            attempt = ready["attempts"][0][arm]
            self.assertEqual(attempt["disposition"], 6)
            self.assertEqual(attempt["controller_invariant_id"], "P4HARNESS-READY-PAIR-001")

        failure = self.run_fault("failure_bad_ready_identity")
        candidate = failure["attempts"][0]["candidate"]
        self.assertEqual(candidate["disposition"], 6)
        self.assertEqual(candidate["controller_invariant_id"], "P4HARNESS-FAILURE-ASSOCIATION-001")
        self.assertIsNone(candidate["child_failure"])

    def test_warmup_failure_must_match_active_cell_and_ready_peer(self) -> None:
        artifact = self.run_fault("warmup_failure_bad_identity")
        candidate = artifact["attempts"][0]["candidate"]
        self.assertEqual(candidate["disposition"], 6)
        self.assertEqual(candidate["controller_invariant_id"], "P4HARNESS-FAILURE-ASSOCIATION-001")
        self.assertIsNone(candidate["child_failure"])

    def test_setup_and_teardown_watchdogs_are_explicit(self) -> None:
        setup = self.run_fault("setup_hang", setup_nanoseconds=100_000_000)
        baseline = setup["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 2)
        self.assertTrue(baseline["watchdog_kill_sent"])

        teardown = self.run_fault("teardown_hang", setup_nanoseconds=100_000_000)
        for arm in ("baseline", "candidate"):
            attempt = teardown["attempts"][0][arm]
            self.assertEqual(attempt["disposition"], 9)
            self.assertTrue(attempt["watchdog_kill_sent"])

    def test_continuous_output_cannot_starve_setup_watchdog(self) -> None:
        artifact = self.run_fault("continuous_output", setup_nanoseconds=100_000_000)
        baseline = artifact["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 2)
        self.assertTrue(baseline["watchdog_kill_sent"])
        self.assertIsNone(artifact["attempts"][0]["result"])

    def test_setup_deadline_starts_before_launch(self) -> None:
        artifact = self.run_fault("launch_delay", setup_nanoseconds=100_000_000)
        baseline = artifact["attempts"][0]["baseline"]
        self.assertEqual(baseline["disposition"], 2)
        self.assertEqual(baseline["controller_invariant_id"], "P4HARNESS-SETUP-DEADLINE-001")
        self.assertEqual(baseline["process_instance_identity"], 0)
        self.assertFalse(baseline["watchdog_kill_sent"])
        self.assertIsNone(artifact["attempts"][0]["result"])

    def test_teardown_ack_and_unexpected_output_invalidate_successes(self) -> None:
        early_exit = self.run_fault("exit_before_stop")
        self.assertTrue(
            all(
                early_exit["attempts"][0][arm]["disposition"] != 0
                for arm in ("baseline", "candidate")
            )
        )
        output = self.run_fault("unexpected_output")
        for arm in ("baseline", "candidate"):
            attempt = output["attempts"][0][arm]
            self.assertEqual(attempt["disposition"], 6)
            self.assertEqual(attempt["controller_invariant_id"], "P4HARNESS-WORKER-OUTPUT-001")

    def test_delayed_frame_after_stop_ack_invalidates_successes(self) -> None:
        artifact = self.run_fault("delayed_extra_frame")
        for arm in ("baseline", "candidate"):
            attempt = artifact["attempts"][0][arm]
            self.assertEqual(attempt["disposition"], 6)
            self.assertEqual(attempt["controller_invariant_id"], "P4HARNESS-STOP-TRAILING-001")
        self.assertIsNone(artifact["attempts"][0]["result"])

    def test_later_bad_association_retroactively_invalidates_worker_successes(self) -> None:
        environment = os.environ.copy()
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "second_run_bad_association"
        completed = subprocess.run(
            base_command(2, 60_000_000_000, executable="phase4_evidence_fault_runner"),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=environment,
        )
        self.assertEqual(completed.returncode, 1, completed.stderr)
        artifact = json.loads(completed.stdout)
        first = artifact["attempts"][0]["candidate"]
        second = artifact["attempts"][1]["candidate"]
        self.assertEqual(first["disposition"], 6)
        self.assertEqual(second["disposition"], 6)
        self.assertEqual(first["controller_invariant_id"], "P4HARNESS-ASSOCIATION-001")
        self.assertEqual(second["controller_invariant_id"], "P4HARNESS-ASSOCIATION-001")
        for field in (
            "process_instance_identity",
            "process_lifetime_peak_host_bytes",
            "raw_wait_status",
            "process_exit_code",
            "terminating_signal",
        ):
            self.assertEqual(first[field], second[field])
        exit_identity = (
            first["process_exit_code"],
            first["terminating_signal"],
        )
        self.assertIn(exit_identity, {(0, 0), (-1, signal.SIGKILL)})
        if os.WIFEXITED(first["raw_wait_status"]):
            self.assertEqual(exit_identity, (os.WEXITSTATUS(first["raw_wait_status"]), 0))
        else:
            self.assertTrue(os.WIFSIGNALED(first["raw_wait_status"]))
            self.assertEqual(
                exit_identity,
                (-1, os.WTERMSIG(first["raw_wait_status"])),
            )
        self.assertIsNone(artifact["attempts"][0]["result"])

    def test_non_cloexec_sentinel_is_not_inherited_by_worker(self) -> None:
        read_descriptor, write_descriptor = os.pipe()
        sentinel_descriptor = fcntl.fcntl(read_descriptor, fcntl.F_DUPFD, 700)
        os.close(read_descriptor)
        os.set_inheritable(sentinel_descriptor, True)
        environment = os.environ.copy()
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "inherited_fd_sentinel"
        environment["APGAR_PHASE4_TRIAL_SENTINEL_FD"] = str(sentinel_descriptor)
        try:
            completed = subprocess.run(
                base_command(1, 60_000_000_000, executable="phase4_evidence_fault_runner"),
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
                env=environment,
                pass_fds=(sentinel_descriptor,),
            )
        finally:
            os.close(sentinel_descriptor)
            os.close(write_descriptor)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        artifact = json.loads(completed.stdout)
        self.assertIsNotNone(artifact["attempts"][0]["result"])

    def test_unsafe_sigchld_policy_is_rejected_before_launch(self) -> None:
        def ignore_sigchld() -> None:
            signal.signal(signal.SIGCHLD, signal.SIG_IGN)

        completed = subprocess.run(
            base_command(1, 60_000_000_000),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            preexec_fn=ignore_sigchld,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")
        self.assertIn("P4HARNESS-SIGCHLD-POLICY-001", completed.stderr)


if __name__ == "__main__":
    unittest.main()
