"""Cross-process tests for Phase 4 operational measurement publication."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from types import SimpleNamespace
from unittest import mock

from tools import aggregate_phase4_matrix as matrix_aggregator
from tools import capture_phase4_operational_measurement as capture_tool
from tools import validate_phase4_operational_measurement as validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "b" * 40


def _runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def _canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def _normalize_raw(raw: dict[str, object], sidecar: dict[str, object]) -> None:
    _normalize_raw_only(raw)
    sidecar["source_commit"] = _COMMIT
    sidecar["source_stamped"] = True
    sidecar["source_tree_dirty"] = False
    sidecar["raw_source_envelope_checksum"] = raw["source_envelope_checksum"]
    sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
        sidecar
    )


def _normalize_raw_only(raw: dict[str, object]) -> None:
    raw["source_commit"] = _COMMIT
    raw["source_stamped"] = True
    raw["source_tree_dirty"] = False
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)


def _report_command(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(_runfile("phase4_per_net_report_test_runner")),
        "--testing_allow_unstamped=1",
        f"--apgar_commit={raw['source_commit']}",
        f"--case_id={config['case_id']}",
        f"--pool_size={config['requested_pool_size']}",
        f"--workers={config['preparation_worker_count']}",
        f"--repetitions={config['repetitions']}",
        f"--setup_ns={config['maximum_setup_elapsed_nanoseconds']}",
        f"--prepared_ns={config['external_budget']['maximum_prepared_elapsed_nanoseconds']}",
        f"--cold_ns={config['external_budget']['maximum_cold_elapsed_nanoseconds']}",
        f"--address_space_bytes={config['external_budget']['maximum_address_space_bytes']}",
        f"--peak_host_bytes={config['external_budget']['maximum_peak_host_bytes']}",
        f"--maximum_nets={config['corpus_limits']['maximum_nets']}",
        f"--maximum_compiled_nodes={config['corpus_limits']['maximum_compiled_nodes']}",
        f"--maximum_compiled_host_bytes={config['corpus_limits']['maximum_compiled_host_bytes']}",
        f"--maximum_active_regions={config['corpus_limits']['maximum_active_regions']}",
        f"--maximum_board_entities={config['corpus_limits']['maximum_board_entities']}",
        f"--raw_cell_plan_checksum={raw['cell_plan_checksum']}",
        f"--raw_cell_artifact_checksum={raw['artifact_checksum']}",
        f"--raw_source_envelope_checksum={raw['source_envelope_checksum']}",
        f"--pair_attempt_checksum={attempt['attempt_checksum']}",
        f"--paired_semantic_checksum={paired['semantic_checksum']}",
        f"--paired_artifact_checksum={paired['artifact_checksum']}",
        f"--baseline_semantic_checksum={baseline['semantics']['semantic_checksum']}",
        f"--baseline_arm_artifact_checksum={baseline['artifact_checksum']}",
        f"--candidate_semantic_checksum={candidate['semantics']['semantic_checksum']}",
        f"--candidate_arm_artifact_checksum={candidate['artifact_checksum']}",
    ]


def _rehash_worker(worker: dict[str, object]) -> None:
    kind = worker["kind"]
    payload = worker["payload"]
    assert isinstance(kind, int)
    assert isinstance(payload, dict)
    checksum_name = "profile_checksum" if kind == 0 else "authority_checksum"
    worker["artifact_checksum"] = capture_tool._worker_artifact_checksum(
        kind,
        worker["compiler_identity"],
        payload[checksum_name],
    )
    worker["source_envelope_checksum"] = capture_tool._worker_source_checksum(worker)


def _rehash_capture(capture: dict[str, object]) -> None:
    capture["artifact_checksum"] = capture_tool._artifact_checksum(capture)
    capture["source_envelope_checksum"] = capture_tool._source_checksum(capture)


def _rehash_provenance(provenance: dict[str, object]) -> None:
    payload = {key: item for key, item in provenance.items() if key != "provenance_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-REPRODUCIBILITY-PROVENANCE-V1")
    hashed.string(capture_tool._canonical(payload))
    provenance["provenance_checksum"] = hashed.finish()


def _normalize_capture(capture: dict[str, object]) -> None:
    capture["source_commit"] = _COMMIT
    capture["source_stamped"] = True
    capture["source_tree_dirty"] = False
    arms = capture["arms"]
    assert isinstance(arms, list)
    for arm in arms:
        assert isinstance(arm, dict)
        for name in ("measured_worker", "authority_worker"):
            worker = arm[name]
            assert isinstance(worker, dict)
            worker["source_commit"] = _COMMIT
            worker["source_stamped"] = True
            worker["source_tree_dirty"] = False
            _rehash_worker(worker)
    _rehash_capture(capture)


def _worker_options(worker: pathlib.Path) -> SimpleNamespace:
    return SimpleNamespace(
        worker=worker,
        fixture=_runfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"),
        apgar_commit=None,
        case_id=100,
        pool_size=4,
        workers=4,
        setup_ns=300_000_000_000,
        prepared_ns=300_000_000_000,
        cold_ns=300_000_000_000,
        address_space_bytes=64 << 30,
        peak_host_bytes=16 << 30,
        maximum_nets=4096,
        maximum_compiled_nodes=100_000_000,
        maximum_compiled_host_bytes=8 << 30,
        maximum_active_regions=250_000,
        maximum_board_entities=100_000,
    )


class Phase4OperationalMeasurementTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        sidecar_path = cls.root / "same-run-original.json"
        raw_run = subprocess.run(
            [
                str(_runfile("phase4_evidence_runner")),
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
                f"--same_run_telemetry_output={sidecar_path}",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if raw_run.returncode != 0:
            raise RuntimeError(raw_run.stderr)
        cls.raw = json.loads(raw_run.stdout)
        cls.sidecar = dict(same_run_validator.read_document(sidecar_path))
        _normalize_raw(cls.raw, cls.sidecar)

        capture_run = subprocess.run(
            [
                str(_runfile("phase4_operational_capture")),
                f"--worker={_runfile('phase4_operational_replay_worker')}",
                (
                    "--fixture="
                    + str(_runfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"))
                ),
                "--testing-allow-unstamped",
                "--case-id=100",
                "--pool-size=4",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        if capture_run.returncode != 0:
            raise RuntimeError(capture_run.stderr)
        cls.capture = json.loads(capture_run.stdout)
        _normalize_capture(cls.capture)
        cls.publication = validator.project_document(cls.raw, cls.capture, cls.sidecar)

        raw_v1_run = subprocess.run(
            [
                str(_runfile("phase4_evidence_runner")),
                "--testing_allow_unstamped=1",
                "--case_id=200",
                "--pool_size=4",
                "--workers=4",
                "--repetitions=20",
                "--setup_ns=300000000000",
                "--prepared_ns=300000000000",
                "--cold_ns=300000000000",
                "--address_space_bytes=68719476736",
                "--peak_host_bytes=17179869184",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=120,
        )
        if raw_v1_run.returncode != 0:
            raise RuntimeError(raw_v1_run.stderr)
        cls.raw_v1 = json.loads(raw_v1_run.stdout)
        _normalize_raw_only(cls.raw_v1)
        report_v1_run = subprocess.run(
            _report_command(cls.raw_v1),
            check=False,
            text=True,
            capture_output=True,
            timeout=120,
        )
        if report_v1_run.returncode != 0:
            raise RuntimeError(report_v1_run.stderr)
        cls.report_v1 = json.loads(report_v1_run.stdout)
        capture_v1_run = subprocess.run(
            [
                str(_runfile("phase4_operational_capture")),
                f"--worker={_runfile('phase4_operational_replay_worker')}",
                (
                    "--fixture="
                    + str(_runfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"))
                ),
                "--testing-allow-unstamped",
                "--case-id=200",
                "--pool-size=4",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=120,
        )
        if capture_v1_run.returncode != 0:
            raise RuntimeError(capture_v1_run.stderr)
        cls.capture_v1 = json.loads(capture_v1_run.stdout)
        _normalize_capture(cls.capture_v1)
        cls.publication_v1 = validator.project_document(cls.raw_v1, cls.capture_v1, None)

        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        cls.capture_path = cls.root / "capture.json"
        cls.raw_path.write_text(_canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(_canonical(cls.sidecar), encoding="utf-8")
        cls.capture_path.write_text(_canonical(cls.capture), encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_complete_join_has_four_execs_and_only_measured_resources(self) -> None:
        validator.validate_capture(self.capture, expected_commit=_COMMIT)
        validator.validate_publication(self.raw, self.capture, self.sidecar, self.publication)
        identities: set[int] = set()
        for arm in self.capture["arms"]:
            measured = arm["measured_process"]
            authority = arm["authority_process"]
            identities.add(measured["process_instance_identity"])
            identities.add(authority["process_instance_identity"])
            self.assertEqual(
                measured["total_cpu_nanoseconds"],
                measured["user_cpu_nanoseconds"] + measured["system_cpu_nanoseconds"],
            )
            self.assertGreater(measured["peak_host_bytes"], 0)
            self.assertEqual(
                authority["resource_measurements"],
                {
                    "status": "not_used",
                    "reason": "unmeasured_full_preimage_authority_replay",
                },
            )
            self.assertNotIn("peak_host_bytes", authority)
            self.assertNotIn("total_cpu_nanoseconds", authority)
        self.assertEqual(len(identities), 4)
        self.assertTrue(self.publication["eligible_input_to_phase4_aggregation"])
        self.assertFalse(self.publication["standalone_decision_eligible"])
        self.assertFalse(self.publication["coverage_complete"])
        self.assertTrue(self.publication["cell_operational_telemetry_complete"])

    def test_raw_v1_publication_and_version_dispositions_are_enforced(self) -> None:
        validator.validate_capture(self.capture_v1, expected_commit=_COMMIT)
        validator.validate_publication(self.raw_v1, self.capture_v1, None, self.publication_v1)
        self.assertEqual(self.publication_v1["raw_authority_binding"]["kind"], "raw_v1")
        with self.assertRaisesRegex(validator.EvidenceError, "Raw-v1.*companion"):
            validator.project_document(self.raw_v1, self.capture_v1, self.sidecar)
        with self.assertRaisesRegex(validator.EvidenceError, "requires same-run"):
            validator.project_document(self.raw, self.capture, None)

    def test_matrix_adapter_rebuilds_real_raw_report_capture_and_publication(self) -> None:
        bindings = [
            {
                "kind": kind,
                "path": f"synthetic/{kind}.json",
                "artifact_checksum": index + 1,
                "source_envelope_checksum": index + 2,
            }
            for index, kind in enumerate(("raw", "report", "capture", "operational"))
        ]
        row = matrix_aggregator.validate_success_cell_documents(
            raw=self.raw_v1,
            sidecar=None,
            report=self.report_v1,
            capture=self.capture_v1,
            publication=self.publication_v1,
            expected_commit=_COMMIT,
            role="calibration",
            evidence_requirement="raw_success",
            in_noncalibration_closure=False,
            bindings=bindings,
        )
        self.assertEqual((row["case_id"], row["requested_pool_size"]), (200, 4))
        self.assertIn(row["comparison"], {"candidate_win", "candidate_loss", "tie"})
        self.assertEqual(len(row["timing_diagnostic"]["ratios_ppm"]), 20)

    def test_reauthenticated_compact_authority_drift_cannot_cross_process_join(self) -> None:
        changed = copy.deepcopy(self.capture)
        arm = changed["arms"][1]
        authority_worker = arm["authority_worker"]
        authority = authority_worker["payload"]
        authority["candidate_session_witness"]["counters"]["transient_result_bytes"] += 1
        authority["authority_checksum"] = validator._authority_checksum(authority)
        _rehash_worker(authority_worker)
        _rehash_capture(changed)
        with self.assertRaisesRegex(
            validator.EvidenceError, "measured and full-preimage witnesses differ"
        ):
            validator.validate_capture(changed, expected_commit=_COMMIT)

    def test_reauthenticated_impossible_candidate_terminal_reason_is_rejected(self) -> None:
        for terminal_reason in (4, 255):
            with self.subTest(terminal_reason=terminal_reason):
                changed = copy.deepcopy(self.capture)
                candidate = changed["arms"][1]
                profile = candidate["measured_worker"]["payload"]
                authority = candidate["authority_worker"]["payload"]
                profile["candidate_session"]["replay_witness"]["terminal_reason"] = terminal_reason
                authority["candidate_session_witness"]["terminal_reason"] = terminal_reason
                profile["profile_checksum"] = validator._profile_checksum(profile)
                authority["authority_checksum"] = validator._authority_checksum(authority)
                _rehash_worker(candidate["measured_worker"])
                _rehash_worker(candidate["authority_worker"])
                _rehash_capture(changed)
                with self.assertRaisesRegex(validator.EvidenceError, "terminal_reason"):
                    validator.validate_capture(changed, expected_commit=_COMMIT)

    def test_capture_rejects_process_reuse_case_source_drift_and_numeric_authority(self) -> None:
        reused = copy.deepcopy(self.capture)
        reused["arms"][1]["authority_process"]["process_instance_identity"] = reused["arms"][0][
            "measured_process"
        ]["process_instance_identity"]
        _rehash_capture(reused)
        with self.assertRaisesRegex(validator.EvidenceError, "four distinct processes"):
            validator.validate_capture(reused, expected_commit=_COMMIT)

        wrong_source = copy.deepcopy(self.capture)
        profile = wrong_source["arms"][0]["measured_worker"]["payload"]
        profile["case_build"]["case_source"] = 1
        profile["case_build"]["fixture_import_applicability"] = {
            "status": 0,
            "reason": 0,
        }
        profile["case_build"]["synthetic_materialization_applicability"] = {
            "status": 1,
            "reason": 8,
        }
        profile["case_build"]["compile_probe_applicability"] = {
            "status": 1,
            "reason": 9,
        }
        profile["profile_checksum"] = validator._profile_checksum(profile)
        _rehash_worker(wrong_source["arms"][0]["measured_worker"])
        _rehash_capture(wrong_source)
        with self.assertRaisesRegex(validator.EvidenceError, "frozen descriptor"):
            validator.validate_capture(wrong_source, expected_commit=_COMMIT)

        numeric = copy.deepcopy(self.capture)
        numeric["arms"][0]["authority_process"]["peak_host_bytes"] = 1
        _rehash_capture(numeric)
        with self.assertRaisesRegex(validator.EvidenceError, "missing, extra, or reordered fields"):
            validator.validate_capture(numeric, expected_commit=_COMMIT)

        incomplete_provenance = copy.deepcopy(self.capture)
        provenance = incomplete_provenance["reproducibility_provenance"]
        provenance["toolchain_files"].pop()
        _rehash_provenance(provenance)
        _rehash_capture(incomplete_provenance)
        with self.assertRaisesRegex(validator.EvidenceError, "toolchain_files is incomplete"):
            validator.validate_capture(incomplete_provenance, expected_commit=_COMMIT)

    def test_absolute_deadline_accepts_boundary_and_rejects_one_nanosecond_over(self) -> None:
        deadline = (
            self.capture["cell_config"]["maximum_setup_elapsed_nanoseconds"]
            + 2 * self.capture["cell_config"]["external_budget"]["maximum_cold_elapsed_nanoseconds"]
        )
        boundary = copy.deepcopy(self.capture)
        for arm in boundary["arms"]:
            arm["measured_process"]["outer_wall_nanoseconds"] = deadline
        _rehash_capture(boundary)
        validator.validate_capture(boundary, expected_commit=_COMMIT)

        over = copy.deepcopy(boundary)
        over["arms"][0]["measured_process"]["outer_wall_nanoseconds"] = deadline + 1
        _rehash_capture(over)
        with self.assertRaisesRegex(validator.EvidenceError, "absolute deadline"):
            validator.validate_capture(over, expected_commit=_COMMIT)

    def test_capture_enforces_peak_cap_for_every_child(self) -> None:
        run = subprocess.run(
            [
                str(_runfile("phase4_operational_capture")),
                f"--worker={_runfile('phase4_operational_replay_worker')}",
                (
                    "--fixture="
                    + str(_runfile("tests/fixtures/phase4_supported_multinet_v1.kicad_pcb"))
                ),
                "--testing-allow-unstamped",
                "--case-id=100",
                "--pool-size=4",
                "--peak-host-bytes=1",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=60,
        )
        self.assertEqual(run.returncode, 1)
        self.assertIn("peak-host-memory cap", run.stderr)

    def test_unified_cgroup_path_and_ancestor_controls_are_bound(self) -> None:
        cgroup_root = self.root / f"cgroup-{self.id().rsplit('.', 1)[-1]}"
        leaf = cgroup_root / "parent" / "leaf"
        leaf.mkdir(parents=True)
        record = self.root / f"cgroup-record-{self.id().rsplit('.', 1)[-1]}"
        record.write_text("0::/parent/leaf\n", encoding="utf-8")
        (leaf / "cpu.max").write_text("max 100000\n", encoding="utf-8")
        (leaf / "memory.max").write_text("max\n", encoding="utf-8")
        (leaf / "cpuset.cpus.effective").write_text("2-3\n", encoding="utf-8")
        (leaf.parent / "cpu.max").write_text("50000 100000\n", encoding="utf-8")
        (leaf.parent / "memory.max").write_text("1073741824\n", encoding="utf-8")

        context = capture_tool._cgroup_context(record, cgroup_root)
        self.assertEqual(context["cgroup_unified_path"], "/parent/leaf")
        self.assertEqual(
            [entry["path"] for entry in context["cgroup_cpu_max_ancestry"]],
            ["/parent/leaf", "/parent", "/"],
        )
        self.assertEqual(
            context["cgroup_cpu_max_ancestry"][1]["control"],
            {"status": "measured", "value": "50000 100000"},
        )
        self.assertEqual(
            context["cgroup_cpu_max_ancestry"][2]["control"],
            {"status": "unavailable", "reason": "cgroup_file_unavailable"},
        )
        self.assertEqual(
            context["cgroup_cpuset_effective"],
            {"status": "measured", "value": "2-3"},
        )
        with self.assertRaisesRegex(capture_tool.CaptureError, "one unified"):
            capture_tool._unified_cgroup_path("0::/a\n0::/b\n")
        expected_environment = capture_tool._execution_environment()
        changed_environment = copy.deepcopy(expected_environment)
        changed_environment["affinity_cpu_ids"] = [
            *changed_environment["affinity_cpu_ids"],
            1 << 30,
        ]
        with (
            mock.patch.object(
                capture_tool,
                "_execution_environment",
                return_value=changed_environment,
            ),
            self.assertRaisesRegex(capture_tool.CaptureError, "environment drifted"),
        ):
            capture_tool._require_execution_environment(expected_environment)

        reordered = copy.deepcopy(self.capture)
        provenance = reordered["reproducibility_provenance"]
        provenance["host"]["cgroup_cpu_max_ancestry"].reverse()
        _rehash_provenance(provenance)
        _rehash_capture(reordered)
        with self.assertRaisesRegex(validator.EvidenceError, "leaf-to-root"):
            validator.validate_capture(reordered, expected_commit=_COMMIT)

    def test_dispatch_executes_pinned_worker_after_path_replacement(self) -> None:
        worker_path = self.root / f"worker-{self.id().rsplit('.', 1)[-1]}"
        replacement = self.root / f"replacement-{self.id().rsplit('.', 1)[-1]}"
        shutil.copy2(_runfile("phase4_operational_replay_worker"), worker_path)
        worker_fd, _ = capture_tool._open_pinned_worker(worker_path)
        try:
            shutil.copy2("/bin/false", replacement)
            os.replace(replacement, worker_path)
            options = _worker_options(worker_path)
            observation, worker = capture_tool._launch(
                capture_tool._cell_arguments(options, "measured", "baseline"),
                worker_fd=worker_fd,
                address_space_bytes=options.address_space_bytes,
                peak_host_bytes=options.peak_host_bytes,
                deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                controller_identity=123,
                dispatch_ordinal=1,
                expected_kind=0,
                measurement_role="measured_baseline",
            )
        finally:
            os.close(worker_fd)
        self.assertEqual(observation["process_exit_code"], 0)
        self.assertEqual(worker["kind"], 0)

    def test_process_group_signal_never_uses_a_reaped_leader_pid(self) -> None:
        worker_path = _runfile("phase4_operational_replay_worker")
        worker_fd, _ = capture_tool._open_pinned_worker(worker_path)
        options = _worker_options(worker_path)
        real_wait4 = capture_tool.os.wait4
        real_kill_process_group = capture_tool._kill_process_group
        leader_reaped = False

        def tracked_wait4(pid: int, flags: int) -> tuple[int, int, object]:
            nonlocal leader_reaped
            result = real_wait4(pid, flags)
            if result[0] == pid:
                leader_reaped = True
            return result

        def guarded_kill_process_group(pid: int) -> bool:
            self.assertFalse(leader_reaped, "numeric process group used after exact-child reap")
            return real_kill_process_group(pid)

        try:
            with (
                mock.patch.object(capture_tool.os, "wait4", side_effect=tracked_wait4),
                mock.patch.object(
                    capture_tool,
                    "_kill_process_group",
                    side_effect=guarded_kill_process_group,
                ),
            ):
                observation, worker = capture_tool._launch(
                    capture_tool._cell_arguments(options, "measured", "baseline"),
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=124,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                )
        finally:
            os.close(worker_fd)
        self.assertTrue(leader_reaped)
        self.assertEqual(observation["process_exit_code"], 0)
        self.assertEqual(worker["kind"], 0)

    def test_detached_worker_dies_when_its_controller_exits(self) -> None:
        read_descriptor, write_descriptor = os.pipe()
        ready_read, ready_write = os.pipe()
        supervisor = os.fork()
        if supervisor == 0:
            os.close(read_descriptor)
            child = os.fork()
            if child == 0:
                try:
                    os.close(ready_read)
                    capture_tool._arm_parent_death_signal(os.getppid())
                    os.setsid()
                    os.write(write_descriptor, f"{os.getpid()}\n".encode("ascii"))
                    os.close(write_descriptor)
                    os.write(ready_write, b"1")
                    os.close(ready_write)
                    time.sleep(30)
                finally:
                    os._exit(0)
            os.close(write_descriptor)
            os.close(ready_write)
            os.read(ready_read, 1)
            os.close(ready_read)
            os._exit(0)
        os.close(write_descriptor)
        os.close(ready_read)
        os.close(ready_write)
        encoded = os.read(read_descriptor, 64)
        os.close(read_descriptor)
        os.waitpid(supervisor, 0)
        worker_pid = int(encoded)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                state = (
                    pathlib.Path(f"/proc/{worker_pid}/stat")
                    .read_text(encoding="ascii")
                    .split(") ", 1)[1][0]
                )
            except FileNotFoundError:
                break
            if state == "Z":
                break
            time.sleep(0.01)
        else:
            os.kill(worker_pid, signal.SIGKILL)
            self.fail("detached operational worker survived controller death")

    def test_child_environment_drift_before_reap_is_rejected(self) -> None:
        worker_path = _runfile("phase4_operational_replay_worker")
        worker_fd, _ = capture_tool._open_pinned_worker(worker_path)
        options = _worker_options(worker_path)
        expected = capture_tool._execution_environment()
        child_environment = {
            "affinity_cpu_ids": expected["affinity_cpu_ids"],
            "cgroup_unified_path": expected["cgroup_unified_path"],
        }
        changed = copy.deepcopy(child_environment)
        changed["cgroup_unified_path"] = "/changed"
        try:
            with (
                mock.patch.object(
                    capture_tool,
                    "_process_execution_environment",
                    side_effect=[child_environment, changed],
                ),
                self.assertRaisesRegex(capture_tool.CaptureError, "drifted before exact reap"),
            ):
                capture_tool._launch(
                    capture_tool._cell_arguments(options, "measured", "baseline"),
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=128,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                    expected_execution_environment=expected,
                )
        finally:
            os.close(worker_fd)

    def test_proc_stat_starttime_parser_handles_spaces_and_parentheses_in_comm(self) -> None:
        record = "123 (worker ) name with spaces) S " + " ".join(
            str(value) for value in range(4, 23)
        )
        self.assertEqual(capture_tool._proc_start_ticks(record), "22")
        with self.assertRaisesRegex(capture_tool.CaptureError, "starttime"):
            capture_tool._proc_start_ticks("123 (worker) S 4 5")

    def test_post_reap_descendants_cannot_escape_watchdog_or_resource_scope(self) -> None:
        worker = self.root / f"descendant-worker-{self.id().rsplit('.', 1)[-1]}"
        worker.write_text(
            f"#!{sys.executable}\n"
            "import os\n"
            "import sys\n"
            "import time\n"
            "child = os.fork()\n"
            "if child == 0:\n"
            "    if sys.argv[1] == 'close':\n"
            "        os.close(1)\n"
            "        os.close(2)\n"
            "    time.sleep(10)\n"
            "    os._exit(0)\n"
            "os._exit(0)\n",
            encoding="utf-8",
        )
        worker.chmod(0o700)
        for mode in ("hold", "close"):
            with self.subTest(mode=mode):
                worker_fd, _ = capture_tool._open_pinned_worker(worker)
                started = time.monotonic()
                try:
                    with self.assertRaisesRegex(capture_tool.CaptureError, "surviving descendant"):
                        capture_tool._launch(
                            [str(worker), mode],
                            worker_fd=worker_fd,
                            address_space_bytes=64 << 30,
                            peak_host_bytes=16 << 30,
                            deadline_nanoseconds=1_000_000_000,
                            controller_identity=125,
                            dispatch_ordinal=1,
                            expected_kind=0,
                            measurement_role="measured_baseline",
                        )
                finally:
                    os.close(worker_fd)
                self.assertLess(time.monotonic() - started, 2.0)

    def test_setsid_closed_descendant_rejects_otherwise_valid_worker(self) -> None:
        wrapper = self.root / f"escape-wrapper-{self.id().rsplit('.', 1)[-1]}"
        wrapper.write_text(
            f"#!{sys.executable}\n"
            "import os\n"
            "import sys\n"
            "import time\n"
            "child = os.fork()\n"
            "if child == 0:\n"
            "    os.setsid()\n"
            "    os.close(1)\n"
            "    os.close(2)\n"
            "    time.sleep(10)\n"
            "    os._exit(0)\n"
            "os.execv(sys.argv[1], sys.argv[1:])\n",
            encoding="utf-8",
        )
        wrapper.chmod(0o700)
        real_worker = _runfile("phase4_operational_replay_worker")
        options = _worker_options(real_worker)
        real_arguments = capture_tool._cell_arguments(options, "measured", "baseline")
        arguments = [str(wrapper), str(real_worker), *real_arguments[1:]]
        worker_fd, _ = capture_tool._open_pinned_worker(wrapper)
        started = time.monotonic()
        try:
            with self.assertRaisesRegex(capture_tool.CaptureError, "surviving descendant"):
                capture_tool._launch(
                    arguments,
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=126,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                )
        finally:
            os.close(worker_fd)
        self.assertLess(time.monotonic() - started, 2.0)

    def test_selector_setup_failure_reaps_the_exact_child(self) -> None:
        worker_fd, _ = capture_tool._open_pinned_worker(
            _runfile("phase4_operational_replay_worker")
        )
        options = _worker_options(_runfile("phase4_operational_replay_worker"))
        original_wait4 = os.wait4
        reaped: list[int] = []

        def tracked_wait4(pid: int, flags: int) -> object:
            result = original_wait4(pid, flags)
            if result[0] == pid:
                reaped.append(pid)
            return result

        class BrokenSelector:
            def register(self, *_args: object) -> None:
                raise RuntimeError("injected selector registration failure")

            def close(self) -> None:
                raise OSError("injected selector close failure")

        try:
            with (
                mock.patch.object(
                    capture_tool.selectors, "DefaultSelector", return_value=BrokenSelector()
                ),
                mock.patch.object(capture_tool.os, "wait4", side_effect=tracked_wait4),
                self.assertRaisesRegex(RuntimeError, "selector registration failure") as raised,
            ):
                capture_tool._launch(
                    capture_tool._cell_arguments(options, "measured", "baseline"),
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=124,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                )
        finally:
            os.close(worker_fd)
        self.assertEqual(len(reaped), 1)
        self.assertTrue(
            any(
                "selector or pidfd cleanup failed" in note
                for note in getattr(raised.exception, "__notes__", ())
            )
        )

    def test_fork_failure_restores_subreaper_and_setup_descriptors(self) -> None:
        worker_path = _runfile("phase4_operational_replay_worker")
        worker_fd, _ = capture_tool._open_pinned_worker(worker_path)
        options = _worker_options(worker_path)
        subreaper_enabled = False

        def stable_fd_roster() -> set[tuple[int, str]]:
            result: set[tuple[int, str]] = set()
            for name in os.listdir("/proc/self/fd"):
                try:
                    target = os.readlink(f"/proc/self/fd/{name}")
                except FileNotFoundError:
                    continue
                result.add((int(name), target))
            return result

        def set_subreaper(enabled: bool) -> None:
            nonlocal subreaper_enabled
            subreaper_enabled = enabled

        before_fds = stable_fd_roster()
        try:
            with (
                mock.patch.object(capture_tool, "_child_subreaper_enabled", return_value=False),
                mock.patch.object(capture_tool, "_direct_child_pids", return_value=[]),
                mock.patch.object(capture_tool, "_set_child_subreaper", side_effect=set_subreaper),
                mock.patch.object(capture_tool.os, "fork", side_effect=RuntimeError("fork fault")),
                self.assertRaisesRegex(RuntimeError, "fork fault"),
            ):
                capture_tool._launch(
                    capture_tool._cell_arguments(options, "measured", "baseline"),
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=127,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                )
        finally:
            os.close(worker_fd)
        self.assertFalse(subreaper_enabled)
        self.assertEqual(
            stable_fd_roster(),
            {item for item in before_fds if item[0] != worker_fd},
        )

    def test_descendant_bound_rejects_only_after_complete_cleanup(self) -> None:
        with (
            mock.patch.object(capture_tool, "_MAXIMUM_ADOPTED_DESCENDANTS", 1),
            mock.patch.object(
                capture_tool,
                "_direct_child_pids",
                side_effect=[[100, 101, 102], []],
            ),
            mock.patch.object(
                capture_tool, "_pidfd_open", side_effect=lambda pid: pid + 1000
            ) as pidfd_open,
            mock.patch.object(capture_tool, "_pidfd_send_kill"),
            mock.patch.object(
                capture_tool.os,
                "wait4",
                side_effect=lambda pid, _flags: (pid, 0, None),
            ) as wait4,
            mock.patch.object(capture_tool.os, "close"),
            self.assertRaisesRegex(capture_tool.CaptureError, "cleanup bound"),
        ):
            capture_tool._terminate_adopted_descendants(
                leader_pid=100,
                deadline=time.monotonic_ns() + 1_000_000_000,
            )
        self.assertEqual(pidfd_open.call_count, 2)
        self.assertEqual(wait4.call_count, 2)

    def test_immediately_reapable_descendants_cannot_extend_cleanup_deadline(self) -> None:
        with (
            mock.patch.object(
                capture_tool.time,
                "monotonic_ns",
                side_effect=[0, 1, 2, 3, 4, 5, 11],
            ),
            mock.patch.object(
                capture_tool,
                "_direct_child_pids",
                side_effect=[[101], [102]],
            ) as child_roster,
            mock.patch.object(capture_tool, "_pidfd_open", return_value=1101),
            mock.patch.object(capture_tool, "_pidfd_send_kill"),
            mock.patch.object(
                capture_tool.os,
                "wait4",
                return_value=(101, 0, None),
            ) as wait4,
            mock.patch.object(capture_tool.os, "close"),
            self.assertRaisesRegex(capture_tool.CaptureError, "cleanup deadline"),
        ):
            capture_tool._terminate_adopted_descendants(
                leader_pid=100,
                deadline=10,
            )
        self.assertEqual(child_roster.call_count, 1)
        self.assertEqual(wait4.call_count, 1)

    def test_residual_child_roster_retains_subreaper_until_controller_exit(self) -> None:
        worker_path = _runfile("phase4_operational_replay_worker")
        worker_fd, _ = capture_tool._open_pinned_worker(worker_path)
        options = _worker_options(worker_path)
        subreaper_updates: list[bool] = []

        class BrokenSelector:
            def register(self, *_args: object) -> None:
                raise RuntimeError("injected selector registration failure")

            def close(self) -> None:
                return None

        try:
            with (
                mock.patch.object(capture_tool, "_child_subreaper_enabled", return_value=False),
                mock.patch.object(
                    capture_tool,
                    "_direct_child_pids",
                    side_effect=[[], [], [], [999_999]],
                ),
                mock.patch.object(
                    capture_tool,
                    "_set_child_subreaper",
                    side_effect=subreaper_updates.append,
                ),
                mock.patch.object(
                    capture_tool.selectors,
                    "DefaultSelector",
                    return_value=BrokenSelector(),
                ),
                self.assertRaisesRegex(RuntimeError, "selector registration failure") as raised,
            ):
                capture_tool._launch(
                    capture_tool._cell_arguments(options, "measured", "baseline"),
                    worker_fd=worker_fd,
                    address_space_bytes=options.address_space_bytes,
                    peak_host_bytes=options.peak_host_bytes,
                    deadline_nanoseconds=options.setup_ns + 2 * options.cold_ns,
                    controller_identity=129,
                    dispatch_ordinal=1,
                    expected_kind=0,
                    measurement_role="measured_baseline",
                )
        finally:
            os.close(worker_fd)
        self.assertEqual(subreaper_updates, [True])
        self.assertTrue(
            any(
                "subreaper authority retained" in note
                for note in getattr(raised.exception, "__notes__", ())
            )
        )

    def test_post_link_directory_fsync_failure_rolls_back_and_allows_retry(self) -> None:
        output = self.root / f"rollback-{self.id().rsplit('.', 1)[-1]}.json"
        encoded = _canonical(self.publication).encode("utf-8")
        real_fsync = os.fsync
        calls = 0

        def fail_first_directory_fsync(descriptor: int) -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                raise OSError("injected directory fsync failure")
            real_fsync(descriptor)

        with (
            mock.patch.object(validator.os, "fsync", side_effect=fail_first_directory_fsync),
            self.assertRaisesRegex(validator.EvidenceError, "atomically install"),
        ):
            validator._write_publication_no_replace(output, encoded)
        self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob(f".{output.name}.phase4-tmp-*")), [])

        validator._write_publication_no_replace(output, encoded)
        self.assertEqual(output.read_bytes(), encoded)

    def test_rollback_directory_fsync_failure_is_reported(self) -> None:
        output = self.root / f"rollback-fsync-{self.id().rsplit('.', 1)[-1]}.json"
        encoded = _canonical(self.publication).encode("utf-8")
        real_fsync = os.fsync
        calls = 0

        def fail_install_and_rollback_directory_fsync(descriptor: int) -> None:
            nonlocal calls
            calls += 1
            if calls in {2, 3}:
                raise OSError(f"injected directory fsync failure {calls}")
            real_fsync(descriptor)

        with (
            mock.patch.object(
                validator.os,
                "fsync",
                side_effect=fail_install_and_rollback_directory_fsync,
            ),
            self.assertRaisesRegex(validator.EvidenceError, "rollback failed"),
        ):
            validator._write_publication_no_replace(output, encoded)
        self.assertFalse(output.exists())
        self.assertEqual(list(self.root.glob(f".{output.name}.phase4-tmp-*")), [])

    def test_cli_installs_canonical_publication_atomically_without_replacement(self) -> None:
        output = self.root / f"publication-{self.id().rsplit('.', 1)[-1]}.json"
        command = [
            str(_runfile("phase4_operational_measurement_validator")),
            "--raw",
            str(self.raw_path),
            "--same-run-telemetry",
            str(self.sidecar_path),
            "--capture",
            str(self.capture_path),
            "--expected-commit",
            _COMMIT,
            "--output",
            str(output),
        ]
        first = subprocess.run(command, check=False, text=True, capture_output=True, timeout=20)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, "")
        before = output.read_bytes()
        self.assertEqual(before.decode("utf-8"), _canonical(self.publication))

        second = subprocess.run(command, check=False, text=True, capture_output=True, timeout=20)
        self.assertEqual(second.returncode, 1)
        self.assertIn("already exists", second.stderr)
        self.assertEqual(output.read_bytes(), before)

        validated = subprocess.run(
            command[:-2] + ["--validate", str(output)],
            check=False,
            text=True,
            capture_output=True,
            timeout=20,
        )
        self.assertEqual(validated.returncode, 0, validated.stderr)
        self.assertIn("validated one", validated.stdout)


if __name__ == "__main__":
    unittest.main()
