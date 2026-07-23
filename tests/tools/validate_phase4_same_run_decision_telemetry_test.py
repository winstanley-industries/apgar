import copy
import json
import os
import pathlib
import subprocess
import tempfile
import unittest

from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as validator


def runfile(relative: str) -> pathlib.Path:
    root = pathlib.Path(os.environ["TEST_SRCDIR"])
    return root / os.environ["TEST_WORKSPACE"] / relative


class Phase4SameRunDecisionTelemetryTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        root = pathlib.Path(cls.temporary.name)
        cls.root = root
        cls.raw_path = root / "raw.json"
        cls.sidecar_path = root / "same_run.json"
        cls.command = [
            str(runfile("phase4_evidence_runner")),
            "--testing_allow_unstamped=1",
            "--case_id=100",
            "--pool_size=4",
            "--workers=4",
            "--repetitions=20",
            "--setup_ns=300000000000",
            "--prepared_ns=300000000000",
            "--cold_ns=300000000000",
            "--address_space_bytes=68719476736",
            "--peak_host_bytes=17179869184",
        ]
        command = cls.command + [f"--same_run_telemetry_output={cls.sidecar_path}"]
        completed = subprocess.run(command, check=False, text=True, capture_output=True, timeout=60)
        if completed.returncode != 0:
            raise RuntimeError(completed.stderr)
        cls.raw_path.write_text(completed.stdout, encoding="utf-8")
        cls.raw = json.loads(completed.stdout)
        cls.sidecar = validator.read_document(cls.sidecar_path)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def validate(self, sidecar: object) -> None:
        validator.validate_join(
            self.raw,
            sidecar,
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

    def test_reader_enforces_the_32_mib_bound_with_a_bounded_read(self) -> None:
        path = self.root / "sidecar-bound.json"
        with path.open("wb") as stream:
            stream.seek(validator._MAX_BYTES - 1)
            stream.write(b"\n")
        with self.assertRaisesRegex(raw_validator.EvidenceError, "cannot read same-run telemetry"):
            validator.read_document(path)
        with path.open("wb") as stream:
            stream.seek(validator._MAX_BYTES)
            stream.write(b"\n")
        with self.assertRaisesRegex(
            raw_validator.EvidenceError, f"exceeds {validator._MAX_BYTES} bytes"
        ):
            validator.read_document(path)

    def test_real_controller_emits_all_same_run_attempts(self) -> None:
        with self.assertRaisesRegex(raw_validator.EvidenceError, "fields differ"):
            raw_validator.validate_document(
                self.raw,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )
        raw_validator.validate_same_run_document_v2(
            self.raw,
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )
        self.validate(self.sidecar)
        self.assertEqual(len(self.sidecar["attempts"]), 20)
        self.assertTrue(
            all(
                len(attempt[arm]["telemetry"]["per_net"]) == 6
                for attempt in self.sidecar["attempts"]
                for arm in ("baseline", "candidate")
            )
        )
        # This is an observed decision result, not corrupt evidence. The
        # frozen protocol must disclose it as a guardrail failure.
        self.assertFalse(validator.exact_rejection_guardrail_passes(self.sidecar))

        with self.assertRaisesRegex(
            validator.EvidenceError, "testing cardinality overrides require"
        ):
            validator.validate_join(
                self.raw,
                self.sidecar,
                allow_unstamped=False,
                expected_commit="0123456789abcdef0123456789abcdef01234567",
                expected_repetitions=1,
                expected_workers=4,
            )

        mismatched = copy.deepcopy(self.raw)
        pair = mismatched["attempts"][-1]
        pair["candidate"]["record"] = copy.deepcopy(
            mismatched["attempts"][1]["candidate"]["record"]
        )
        pair["candidate"]["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(
            pair["candidate"]
        )
        not_run = pair["baseline"]
        not_run.update(
            {
                "disposition": 10,
                "dispatch_ordinal": 0,
                "process_instance_identity": 0,
                "outer_elapsed_nanoseconds": 0,
                "process_lifetime_peak_host_bytes": 0,
                "raw_wait_status": 0,
                "process_exit_code": -1,
                "terminating_signal": 0,
                "watchdog_kill_sent": False,
                "controller_invariant_id": "",
                "controller_detail": "",
                "record": None,
                "child_failure": None,
            }
        )
        not_run["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(not_run)
        pair["result"] = None
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
        mismatched["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(mismatched)
        mismatched["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            mismatched
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "associated with another"):
            raw_validator.validate_same_run_total_attempt_document_v2(
                mismatched,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

    def test_raw_output_failure_never_publishes_sidecar(self) -> None:
        sidecar = self.root / "stdout_failure.json"
        with pathlib.Path("/dev/full").open("wb") as output:
            completed = subprocess.run(
                self.command + [f"--same_run_telemetry_output={sidecar}"],
                check=False,
                stdout=output,
                stderr=subprocess.PIPE,
                text=True,
                timeout=60,
            )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(sidecar.exists())

    def test_total_attempt_lifecycle_accounts_for_late_finalization_failure(self) -> None:
        incomplete = copy.deepcopy(self.raw)
        arm = incomplete["attempts"][5]["candidate"]
        arm["disposition"] = 8
        arm["controller_invariant_id"] = "P4PAIR-FINALIZE-005"
        arm["controller_detail"] = "one or more external trial budgets were exceeded"
        arm["record"] = None
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        pair = incomplete["attempts"][5]
        pair["result"] = None
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
        incomplete["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(incomplete)
        incomplete["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            incomplete
        )

        raw_validator.validate_same_run_total_attempt_document_v2(
            incomplete,
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

        unproven = copy.deepcopy(incomplete)
        arm = unproven["attempts"][5]["candidate"]
        arm["disposition"] = 6
        arm["controller_invariant_id"] = "foreign-controller-failure"
        arm["controller_detail"] = "does not prove a completed candidate invocation"
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        unproven["attempts"][5]["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(
            unproven["attempts"][5]
        )
        unproven["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(unproven)
        unproven["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            unproven
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "lifecycle is not continuous"):
            raw_validator.validate_same_run_total_attempt_document_v2(
                unproven,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

        invented_prefix = copy.deepcopy(incomplete)
        arm = invented_prefix["attempts"][5]["candidate"]
        arm["disposition"] = 6
        arm["controller_invariant_id"] = "P4PAIR-FINALIZE-FAKE"
        arm["controller_detail"] = "invented prefix is not an invocation witness"
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        invented_prefix["attempts"][5]["attempt_checksum"] = (
            raw_validator.compute_pair_attempt_checksum(invented_prefix["attempts"][5])
        )
        invented_prefix["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
            invented_prefix
        )
        invented_prefix["source_envelope_checksum"] = (
            raw_validator.compute_source_envelope_checksum(invented_prefix)
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "lifecycle is not continuous"):
            raw_validator.validate_same_run_total_attempt_document_v2(
                invented_prefix,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

    def test_incomplete_total_attempt_still_requires_deterministic_semantics(self) -> None:
        incomplete = copy.deepcopy(self.raw)
        failed_arm = incomplete["attempts"][5]["candidate"]
        failed_arm["disposition"] = 8
        failed_arm["controller_invariant_id"] = "P4PAIR-FINALIZE-005"
        failed_arm["controller_detail"] = "one or more external trial budgets were exceeded"
        failed_arm["record"] = None
        failed_arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(failed_arm)
        failed_pair = incomplete["attempts"][5]
        failed_pair["result"] = None
        failed_pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(failed_pair)

        pair = incomplete["attempts"][7]
        record = pair["baseline"]["record"]
        record["semantics"]["outcome"]["world_checksum"] += 1
        record["semantics"]["semantic_checksum"] = raw_validator.compute_semantic_checksum(
            record["semantics"]
        )
        observation = record["external_observation"]
        observation["associated_semantic_checksum"] = record["semantics"]["semantic_checksum"]
        observation["authority_checksum"] = raw_validator.compute_authority_checksum(observation)
        record["artifact_checksum"] = raw_validator.compute_record_checksum(record)
        pair["result"]["baseline"] = copy.deepcopy(record)
        pair["result"]["semantic_checksum"] = raw_validator.compute_paired_semantic_checksum(
            pair["result"]
        )
        pair["result"]["artifact_checksum"] = raw_validator.compute_paired_artifact_checksum(
            pair["result"]
        )
        pair["baseline"]["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(
            pair["baseline"]
        )
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
        incomplete["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(incomplete)
        incomplete["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            incomplete
        )

        with self.assertRaisesRegex(raw_validator.EvidenceError, "not deterministic"):
            raw_validator.validate_same_run_total_attempt_document_v2(
                incomplete,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

    def test_process_wide_failure_cannot_leave_same_process_successes(self) -> None:
        partial = copy.deepcopy(self.raw)
        arm = partial["attempts"][5]["baseline"]
        arm["disposition"] = 9
        arm["watchdog_kill_sent"] = True
        arm["controller_invariant_id"] = "P4HARNESS-STOP-TIMEOUT-001"
        arm["controller_detail"] = "worker did not acknowledge stop before teardown deadline"
        arm["record"] = None
        arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
        pair = partial["attempts"][5]
        pair["result"] = None
        pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
        partial["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(partial)
        partial["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(
            partial
        )

        with self.assertRaisesRegex(raw_validator.EvidenceError, "process-wide"):
            raw_validator.validate_same_run_total_attempt_document_v2(
                partial,
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

    def test_sidecar_publication_is_no_replace_and_atomic(self) -> None:
        existing = self.root / "existing.json"
        existing.write_text("sentinel\n", encoding="utf-8")
        completed = subprocess.run(
            self.command + [f"--same_run_telemetry_output={existing}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(existing.read_text(encoding="utf-8"), "sentinel\n")

        interrupted = self.root / "interrupted.json"
        environment = os.environ.copy()
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_before_publish"
        fault_command = [str(runfile("phase4_evidence_fault_runner")), *self.command[1:]]
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={interrupted}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(interrupted.exists())

        malformed = self.root / "malformed_capture.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_corrupt_capture"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={malformed}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(malformed.exists())
        raw_validator.validate_same_run_document_v2(
            json.loads(completed.stdout),
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

        allocation_failure = self.root / "allocation_failure.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_serialize_bad_alloc"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={allocation_failure}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(allocation_failure.exists())
        raw_validator.validate_same_run_document_v2(
            json.loads(completed.stdout),
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

        publication_allocation_failure = self.root / "publication_allocation_failure.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_publish_bad_alloc"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={publication_allocation_failure}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(publication_allocation_failure.exists())
        raw_validator.validate_same_run_document_v2(
            json.loads(completed.stdout),
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

        foreign_record = self.root / "rehashed_foreign_record.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_rehashed_foreign_record"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={foreign_record}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(foreign_record.exists())
        with self.assertRaisesRegex(
            raw_validator.EvidenceError, "identities or equal-budget opportunities differ"
        ):
            raw_validator.validate_same_run_document_v2(
                json.loads(completed.stdout),
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

        wrong_comparison = self.root / "rehashed_wrong_comparison.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_rehashed_wrong_comparison"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={wrong_comparison}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(wrong_comparison.exists())
        with self.assertRaisesRegex(raw_validator.EvidenceError, "comparison is inconsistent"):
            raw_validator.validate_same_run_document_v2(
                json.loads(completed.stdout),
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

        nondeterministic = self.root / "rehashed_nondeterministic.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_rehashed_nondeterministic_record"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={nondeterministic}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(nondeterministic.exists())
        with self.assertRaisesRegex(raw_validator.EvidenceError, "not deterministic"):
            raw_validator.validate_same_run_document_v2(
                json.loads(completed.stdout),
                allow_unstamped=True,
                expected_repetitions=20,
                expected_workers=4,
            )

        rehashed_partition = self.root / "rehashed_partition.json"
        environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = "sidecar_rehashed_partition_drift"
        completed = subprocess.run(
            fault_command + [f"--same_run_telemetry_output={rehashed_partition}"],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
            env=environment,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertFalse(rehashed_partition.exists())
        raw_validator.validate_same_run_document_v2(
            json.loads(completed.stdout),
            allow_unstamped=True,
            expected_repetitions=20,
            expected_workers=4,
        )

    def test_incomplete_wire_v2_runs_preserve_raw_attempt_evidence(self) -> None:
        for mode in (
            "exit_before_launch_gate",
            "exit_before_stop",
            "preparer_factory_resource_failure",
            "preparer_factory_resource_failure_reap_unavailable",
            "setup_hang",
            "delayed_double_ready",
            "delayed_extra_frame",
            "teardown_hang",
        ):
            with self.subTest(mode=mode):
                sidecar = self.root / f"{mode}.json"
                environment = os.environ.copy()
                environment["APGAR_PHASE4_TRIAL_FAULT_MODE"] = mode
                command = [str(runfile("phase4_evidence_fault_runner")), *self.command[1:]]
                if mode in {"setup_hang", "teardown_hang"}:
                    command = [
                        "--setup_ns=100000000" if value.startswith("--setup_ns=") else value
                        for value in command
                    ]
                completed = subprocess.run(
                    command + [f"--same_run_telemetry_output={sidecar}"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=60,
                    env=environment,
                )
                self.assertEqual(completed.returncode, 1, completed.stderr)
                raw = json.loads(completed.stdout)
                self.assertEqual(raw["raw_evidence_schema_version"], 2)
                self.assertEqual(raw["wire_schema_version"], 2)
                self.assertEqual(len(raw["attempts"]), 20)
                self.assertTrue(any(attempt["result"] is None for attempt in raw["attempts"]))
                raw_validator.validate_same_run_total_attempt_document_v2(
                    raw,
                    allow_unstamped=True,
                    expected_repetitions=20,
                    expected_workers=4,
                )
                self.assertFalse(sidecar.exists())
                if mode == "exit_before_stop":
                    later = copy.deepcopy(raw)
                    arm = later["attempts"][-1]["baseline"]
                    arm["outer_elapsed_nanoseconds"] += 1
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    later["attempts"][-1]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(later["attempts"][-1])
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "artifact_checksum"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            later,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    root = copy.deepcopy(later)
                    root["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(root)
                    with self.assertRaisesRegex(
                        raw_validator.EvidenceError, "source_envelope_checksum"
                    ):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            root,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    contradictory = copy.deepcopy(raw)
                    arm = contradictory["attempts"][-1]["baseline"]
                    arm["disposition"] = 10
                    arm["controller_invariant_id"] = ""
                    arm["controller_detail"] = ""
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    contradictory["attempts"][-1]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(contradictory["attempts"][-1])
                    )
                    contradictory["artifact_checksum"] = (
                        raw_validator.compute_cell_artifact_checksum(contradictory)
                    )
                    contradictory["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(contradictory)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "serial prefix"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            contradictory,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    process_drift = copy.deepcopy(raw)
                    arm = process_drift["attempts"][7]["baseline"]
                    arm["process_instance_identity"] += 1
                    arm["process_lifetime_peak_host_bytes"] += 1
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    process_drift["attempts"][7]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(process_drift["attempts"][7])
                    )
                    process_drift["artifact_checksum"] = (
                        raw_validator.compute_cell_artifact_checksum(process_drift)
                    )
                    process_drift["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(process_drift)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "one process"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            process_drift,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )
                if mode == "setup_hang":
                    processless = copy.deepcopy(raw)
                    arm = next(
                        arm
                        for arm in (
                            processless["attempts"][0]["baseline"],
                            processless["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    arm["process_instance_identity"] = 0
                    arm["process_lifetime_peak_host_bytes"] = 123
                    arm["raw_wait_status"] = 9
                    arm["process_exit_code"] = -1
                    arm["terminating_signal"] = 9
                    arm["watchdog_kill_sent"] = True
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    processless["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(processless["attempts"][0])
                    )
                    processless["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        processless
                    )
                    processless["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(processless)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "processless"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            processless,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    for impossible_wait in (-1, 65536):
                        with self.subTest(impossible_wait=impossible_wait):
                            invalid_wait = copy.deepcopy(raw)
                            arm = next(
                                arm
                                for arm in (
                                    invalid_wait["attempts"][0]["baseline"],
                                    invalid_wait["attempts"][0]["candidate"],
                                )
                                if arm["disposition"] != 10
                            )
                            arm["raw_wait_status"] = impossible_wait
                            arm["process_exit_code"] = 0
                            arm["terminating_signal"] = 0
                            arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(
                                arm
                            )
                            invalid_wait["attempts"][0]["attempt_checksum"] = (
                                raw_validator.compute_pair_attempt_checksum(
                                    invalid_wait["attempts"][0]
                                )
                            )
                            invalid_wait["artifact_checksum"] = (
                                raw_validator.compute_cell_artifact_checksum(invalid_wait)
                            )
                            invalid_wait["source_envelope_checksum"] = (
                                raw_validator.compute_source_envelope_checksum(invalid_wait)
                            )
                            with self.assertRaisesRegex(
                                raw_validator.EvidenceError, "exact Linux wait status"
                            ):
                                raw_validator.validate_same_run_total_attempt_document_v2(
                                    invalid_wait,
                                    allow_unstamped=True,
                                    expected_repetitions=20,
                                    expected_workers=4,
                                )

                    wrong_exit = copy.deepcopy(raw)
                    arm = next(
                        arm
                        for arm in (
                            wrong_exit["attempts"][0]["baseline"],
                            wrong_exit["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    arm["disposition"] = 4
                    arm["raw_wait_status"] = 0
                    arm["process_exit_code"] = 0
                    arm["terminating_signal"] = 0
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    wrong_exit["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(wrong_exit["attempts"][0])
                    )
                    wrong_exit["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        wrong_exit
                    )
                    wrong_exit["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(wrong_exit)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "signal exit witness"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            wrong_exit,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    contradictory_wait = copy.deepcopy(raw)
                    arm = next(
                        arm
                        for arm in (
                            contradictory_wait["attempts"][0]["baseline"],
                            contradictory_wait["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    arm["disposition"] = 4
                    arm["raw_wait_status"] = 9
                    arm["process_exit_code"] = 5
                    arm["terminating_signal"] = 9
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    contradictory_wait["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(
                            contradictory_wait["attempts"][0]
                        )
                    )
                    contradictory_wait["artifact_checksum"] = (
                        raw_validator.compute_cell_artifact_checksum(contradictory_wait)
                    )
                    contradictory_wait["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(contradictory_wait)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "exact wait4 status"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            contradictory_wait,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    measured_only = copy.deepcopy(raw)
                    arm = next(
                        arm
                        for arm in (
                            measured_only["attempts"][0]["baseline"],
                            measured_only["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    arm["disposition"] = 9
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    measured_only["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(measured_only["attempts"][0])
                    )
                    measured_only["artifact_checksum"] = (
                        raw_validator.compute_cell_artifact_checksum(measured_only)
                    )
                    measured_only["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(measured_only)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "measured-only"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            measured_only,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    unavailable = copy.deepcopy(raw)
                    arm = next(
                        arm
                        for arm in (
                            unavailable["attempts"][0]["baseline"],
                            unavailable["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    arm["disposition"] = 6
                    arm["process_lifetime_peak_host_bytes"] = 0
                    arm["raw_wait_status"] = 0
                    arm["process_exit_code"] = -1
                    arm["terminating_signal"] = 0
                    arm["controller_invariant_id"] = "P4HARNESS-WAIT4-AUTHORITY-001"
                    arm["controller_detail"] = "exact wait4 authority unavailable"
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    unavailable["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(unavailable["attempts"][0])
                    )
                    unavailable["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        unavailable
                    )
                    unavailable["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(unavailable)
                    )
                    raw_validator.validate_same_run_total_attempt_document_v2(
                        unavailable,
                        allow_unstamped=True,
                        expected_repetitions=20,
                        expected_workers=4,
                    )
                if mode == "preparer_factory_resource_failure":
                    released = copy.deepcopy(raw)
                    arm = released["attempts"][-1]["baseline"]
                    arm["dispatch_ordinal"] = 999
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    released["attempts"][-1]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(released["attempts"][-1])
                    )
                    released["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        released
                    )
                    released["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(released)
                    )
                    with self.assertRaisesRegex(raw_validator.EvidenceError, "serial prefix"):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            released,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

                    doubled = copy.deepcopy(raw)
                    peer = doubled["attempts"][0]["baseline"]
                    peer["disposition"] = 6
                    peer["process_instance_identity"] = 1
                    peer["process_lifetime_peak_host_bytes"] = 1
                    peer["process_exit_code"] = 0
                    peer["controller_invariant_id"] = "foreign-setup-failure"
                    peer["controller_detail"] = "second independent failure"
                    peer["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(peer)
                    doubled["attempts"][0]["attempt_checksum"] = (
                        raw_validator.compute_pair_attempt_checksum(doubled["attempts"][0])
                    )
                    doubled["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        doubled
                    )
                    doubled["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(doubled)
                    )
                    with self.assertRaisesRegex(
                        raw_validator.EvidenceError, "multiple independent"
                    ):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            doubled,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )
                if mode == "preparer_factory_resource_failure_reap_unavailable":
                    failed = next(
                        arm
                        for arm in (
                            raw["attempts"][0]["baseline"],
                            raw["attempts"][0]["candidate"],
                        )
                        if arm["disposition"] != 10
                    )
                    self.assertEqual(failed["disposition"], 1)
                    self.assertIsNotNone(failed["child_failure"])
                    self.assertEqual(
                        failed["controller_invariant_id"], "P4HARNESS-REAP-BOUNDED-001"
                    )
                    self.assertEqual(failed["process_lifetime_peak_host_bytes"], 0)
                    self.assertEqual(failed["raw_wait_status"], 0)
                    self.assertEqual(failed["process_exit_code"], -1)

                    spliced = copy.deepcopy(self.raw)
                    arm = spliced["attempts"][5]["baseline"]
                    failure = copy.deepcopy(failed["child_failure"])
                    failure["arm"] = 0
                    failure["payload_checksum"] = raw_validator.compute_failure_checksum(failure)
                    arm["disposition"] = 1
                    arm["controller_invariant_id"] = ""
                    arm["controller_detail"] = ""
                    arm["record"] = None
                    arm["child_failure"] = failure
                    arm["attempt_checksum"] = raw_validator.compute_arm_attempt_checksum(arm)
                    pair = spliced["attempts"][5]
                    pair["result"] = None
                    pair["attempt_checksum"] = raw_validator.compute_pair_attempt_checksum(pair)
                    spliced["artifact_checksum"] = raw_validator.compute_cell_artifact_checksum(
                        spliced
                    )
                    spliced["source_envelope_checksum"] = (
                        raw_validator.compute_source_envelope_checksum(spliced)
                    )
                    with self.assertRaisesRegex(
                        raw_validator.EvidenceError, "terminate the dispatch prefix"
                    ):
                        raw_validator.validate_same_run_total_attempt_document_v2(
                            spliced,
                            allow_unstamped=True,
                            expected_repetitions=20,
                            expected_workers=4,
                        )

    def test_foreign_semantics_and_missing_full_entity_ref_fail(self) -> None:
        foreign = copy.deepcopy(self.sidecar)
        foreign["attempts"][0]["baseline"]["associated_semantic_checksum"] += 1
        foreign["attempts"][0]["baseline"]["capture_checksum"] = (
            validator.compute_arm_capture_checksum(foreign["attempts"][0]["baseline"])
        )
        foreign["attempts"][0]["capture_checksum"] = validator.compute_pair_capture_checksum(
            foreign["attempts"][0]
        )
        foreign["artifact_checksum"] = validator.compute_cell_capture_checksum(foreign)
        foreign["source_envelope_checksum"] = validator.compute_source_envelope_checksum(foreign)
        with self.assertRaisesRegex(validator.EvidenceError, "differs from Raw"):
            self.validate(foreign)

        missing = copy.deepcopy(self.sidecar)
        missing["attempts"][0]["candidate"]["telemetry"]["per_net"].pop()
        with self.assertRaisesRegex(validator.EvidenceError, "complete frozen roster"):
            self.validate(missing)

        generation = copy.deepcopy(self.sidecar)
        generation["attempts"][0]["candidate"]["telemetry"]["per_net"][0]["net"]["generation"] = 1
        with self.assertRaisesRegex(validator.EvidenceError, "full EntityRef roster"):
            self.validate(generation)

    def test_rebucket_without_authentic_checksum_and_raw_source_drift_fail(self) -> None:
        rebucketed = copy.deepcopy(self.sidecar)
        rows = rebucketed["attempts"][0]["baseline"]["telemetry"]["per_net"]
        row = next(row for row in rows if row["columns"]["exact_validation_rejections"] != 0)
        row["columns"]["exact_validation_rejections"] -= 1
        row["columns"]["other_rejections"] += 1
        with self.assertRaisesRegex(validator.EvidenceError, "telemetry_checksum"):
            self.validate(rebucketed)

        source = copy.deepcopy(self.sidecar)
        source["raw_source_envelope_checksum"] += 1
        with self.assertRaisesRegex(validator.EvidenceError, "differs from Raw"):
            self.validate(source)

        reauthenticated = copy.deepcopy(self.sidecar)
        original_artifact = reauthenticated["artifact_checksum"]
        reauthenticated["raw_controller_identity"] += 1
        reauthenticated["artifact_checksum"] = validator.compute_cell_capture_checksum(
            reauthenticated
        )
        self.assertNotEqual(reauthenticated["artifact_checksum"], original_artifact)
        reauthenticated["source_envelope_checksum"] = validator.compute_source_envelope_checksum(
            reauthenticated
        )
        with self.assertRaisesRegex(validator.EvidenceError, "raw_controller_identity differs"):
            self.validate(reauthenticated)


if __name__ == "__main__":
    unittest.main()
