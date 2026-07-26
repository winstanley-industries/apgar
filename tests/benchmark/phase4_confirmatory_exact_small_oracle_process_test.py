"""Adversarial process coverage for the frozen H=2250 exact-small authority."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_exact_small_oracle as confirmatory_oracle
from tools import validate_phase4_exact_small_oracle as legacy_oracle
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40
_SESSION_AUTHORITY = "P4PAIR-CORPUS-V2-SESSION-AUTHORITY-001"
_H4096_TEST_CLEAN_LAUNCHER = "phase4_confirmatory_h4096_exact_small_oracle_test_clean_validator"
_EXACT_LAUNCHERS_UNDER_TEST = (
    "phase4_exact_small_oracle_validator",
    "phase4_exact_small_oracle_v2_validator",
    "phase4_confirmatory_exact_small_oracle_validator",
    _H4096_TEST_CLEAN_LAUNCHER,
)
_PYTHON_REPOSITORY = "rules_python++python+python_3_13_x86_64-unknown-linux-gnu"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def launcher_help_command(executable: pathlib.Path) -> list[str]:
    command = [str(executable)]
    if executable.name == _H4096_TEST_CLEAN_LAUNCHER:
        command.extend(("--expected-commit", _COMMIT))
    command.append("--help")
    return command


def join_arguments(raw: dict[str, object]) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
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


def snapshot_command(raw: dict[str, object], report: dict[str, object]) -> list[str]:
    return [
        str(runfile("phase4_confirmatory_exact_small_snapshot_test_runner")),
        "--corpus_version=2",
        "--raw_evidence_schema_version=2",
        "--raw_wire_schema_version=2",
        *join_arguments(raw),
        f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
        f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
    ]


def validator_command(
    raw: pathlib.Path,
    sidecar: pathlib.Path,
    report: pathlib.Path,
    snapshot: pathlib.Path,
) -> list[str]:
    return [
        str(runfile("phase4_confirmatory_exact_small_oracle_validator")),
        "--expected-commit",
        _COMMIT,
        "--raw",
        str(raw),
        "--same-run-telemetry",
        str(sidecar),
        "--report",
        str(report),
        "--snapshot",
        str(snapshot),
    ]


def reauthenticate_oracle(artifact: dict[str, object]) -> None:
    artifact["artifact_checksum"] = confirmatory_oracle.compute_artifact_checksum(artifact)
    artifact["source_envelope_checksum"] = confirmatory_oracle.compute_source_envelope_checksum(
        artifact
    )


def structural_only_oracle_fixture(
    raw: dict[str, object],
    sidecar: dict[str, object],
    report: dict[str, object],
) -> dict[str, object]:
    """Build bounded serializer-only structure, never acquisition evidence."""
    snapshot_artifact_checksum = 7001
    snapshot_source_envelope_checksum = legacy_oracle.compute_snapshot_source_envelope(
        {
            "source_commit": _COMMIT,
            "source_stamped": True,
            "source_tree_dirty": False,
            "artifact_checksum": snapshot_artifact_checksum,
        }
    )
    objective = {
        "selected_net_count": 6,
        "total_overuse_units": 1,
        "total_intrinsic_base_cost": 159_000,
    }
    roster = [row["net"] for row in report["arms"][1]["diagnostic"]["telemetry"]["per_net"]]
    witness = [
        {
            "net": copy.deepcopy(net),
            "candidate_id": {"high": net["id"], "low": index + 1},
        }
        for index, net in enumerate(roster)
    ]
    artifact: dict[str, object] = {
        "source_commit": _COMMIT,
        "source_stamped": True,
        "source_tree_dirty": False,
        "source_envelope_checksum": 0,
        "schema_version": 1,
        "campaign_id": "phase4_confirmatory_corpus_v2",
        "cell_role": "exact",
        "eligible_input_to_phase4_aggregation": True,
        "standalone_decision_eligible": False,
        "statistical_timing_eligible": False,
        "coverage_complete": False,
        "exact_small_oracle_complete": True,
        "config": copy.deepcopy(raw["config"]),
        "corpus_version": 2,
        "corpus_checksum": raw["corpus_checksum"],
        "raw_binding": {
            "authority": "phase4_confirmatory_same_run_raw_evidence_v1",
            "raw_evidence_schema_version": raw["raw_evidence_schema_version"],
            "wire_schema_version": raw["wire_schema_version"],
            "cell_plan_checksum": raw["cell_plan_checksum"],
            "artifact_checksum": raw["artifact_checksum"],
            "source_envelope_checksum": raw["source_envelope_checksum"],
        },
        "same_run_telemetry_binding": {
            "authority": "phase4_confirmatory_same_run_decision_telemetry_v1",
            "schema_version": sidecar["schema_version"],
            "raw_evidence_schema_version": sidecar["raw_evidence_schema_version"],
            "raw_wire_schema_version": sidecar["raw_wire_schema_version"],
            "telemetry_wire_schema_version": sidecar["telemetry_wire_schema_version"],
            "raw_artifact_checksum": sidecar["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": sidecar["raw_source_envelope_checksum"],
            "artifact_checksum": sidecar["artifact_checksum"],
            "source_envelope_checksum": sidecar["source_envelope_checksum"],
            "exact_rejection_guardrail_passed": (
                same_run_validator.exact_rejection_guardrail_passes(sidecar)
            ),
        },
        "per_net_report_binding": {
            "authority": "phase4_confirmatory_same_run_per_net_report_publication_join_v1",
            "schema_version": report["schema_version"],
            "corpus_version": 2,
            "raw_wire_schema_version": report["raw_wire_schema_version"],
            "raw_artifact_checksum": report["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": report["raw_source_envelope_checksum"],
            "artifact_checksum": report["artifact_checksum"],
            "source_envelope_checksum": report["source_envelope_checksum"],
        },
        "snapshot_binding": {
            "authority": "phase4_exact_small_snapshot_v1",
            "schema_version": 1,
            "corpus_version": 2,
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
            "per_net_report_artifact_checksum": report["artifact_checksum"],
            "per_net_report_source_envelope_checksum": report["source_envelope_checksum"],
            "artifact_checksum": snapshot_artifact_checksum,
            "source_envelope_checksum": snapshot_source_envelope_checksum,
        },
        "candidate_semantic_checksum": raw["attempts"][0]["candidate"]["record"]["semantics"][
            "semantic_checksum"
        ],
        "cartesian_product": 1,
        "production_objective": copy.deepcopy(objective),
        "optimum_objective": copy.deepcopy(objective),
        "production_overused_resource_count": 1,
        "canonical_witness_overused_resource_count": 1,
        "production_is_optimal": True,
        "optimum_count": 1,
        "canonical_witness": witness,
        "artifact_checksum": 0,
    }
    reauthenticate_oracle(artifact)
    confirmatory_oracle.validate_oracle_artifact(artifact)
    return artifact


class Phase4ConfirmatoryExactSmallOracleProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)
        cls.raw = artifacts.make_raw(
            10_100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.raw, _COMMIT)
        cls.sidecar = artifacts.make_sidecar(cls.raw)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        cls.report_path = cls.root / "report.json"
        cls.snapshot_path = cls.root / "unavailable-snapshot.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")
        cls.report_path.write_text(canonical(cls.report), encoding="utf-8")
        cls.snapshot_path.write_text("{}\n", encoding="utf-8")
        cls.structural_only_artifact_fixture = structural_only_oracle_fixture(
            cls.raw,
            cls.sidecar,
            cls.report,
        )
        cls.h4096_raw = artifacts.make_raw(
            10_100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.h4096_raw, _COMMIT)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_synthetic_frozen_authorities_and_structural_artifact_validate(self) -> None:
        raw, sidecar, report = confirmatory_oracle._validate_authorities(
            self.raw,
            self.sidecar,
            self.report,
            expected_commit=_COMMIT,
        )
        self.assertIs(raw, self.raw)
        self.assertIs(sidecar, self.sidecar)
        self.assertIs(report, self.report)
        serialized = confirmatory_oracle.serialize_oracle_artifact(
            self.structural_only_artifact_fixture
        )
        self.assertEqual(json.loads(serialized), self.structural_only_artifact_fixture)

        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle._validate_authorities(
                self.h4096_raw,
                artifacts.make_sidecar(self.h4096_raw),
                artifacts.make_report(self.h4096_raw),
                expected_commit=_COMMIT,
            )

    def test_raw_runner_closes_before_missing_or_fifo_fixture_access(self) -> None:
        missing = self.root / "raw-must-not-open.kicad_pcb"
        fifo = self.root / "raw-must-not-read.fifo"
        os.mkfifo(fifo)
        for fixture in (missing, fifo):
            with self.subTest(fixture=fixture.name):
                completed = subprocess.run(
                    [
                        str(runfile("phase4_confirmatory_evidence_test_runner")),
                        "--corpus_version=2",
                        "--testing_allow_unstamped=1",
                        "--case_id=10100",
                        "--pool_size=4",
                        "--workers=4",
                        "--repetitions=20",
                        f"--fixture_path={fixture}",
                        f"--same_run_telemetry_output={self.root / 'forbidden-sidecar.json'}",
                    ],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)
                self.assertNotIn("failed to read", completed.stderr)

    def test_snapshot_runners_close_session_before_stdout_or_snapshot_work(self) -> None:
        command = snapshot_command(self.raw, self.report)
        for target in (
            "phase4_confirmatory_exact_small_snapshot_test_runner",
            "phase4_confirmatory_exact_small_snapshot_runner",
        ):
            with self.subTest(target=target):
                invocation = list(command)
                invocation[0] = str(runfile(target))
                if target == "phase4_confirmatory_exact_small_snapshot_runner":
                    invocation = [
                        argument
                        for argument in invocation
                        if argument != "--testing_allow_unstamped=1"
                    ]
                completed = subprocess.run(
                    invocation,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                self.assertIn(_SESSION_AUTHORITY, completed.stderr)

    def test_snapshot_runner_argument_mutations_remain_fail_closed(self) -> None:
        valid = snapshot_command(self.raw, self.report)

        def replace(name: str, value: str) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in valid
            ]

        mutations = (
            [argument for argument in valid if not argument.startswith("--corpus_version=")],
            replace("corpus_version", "1"),
            replace("raw_evidence_schema_version", "1"),
            replace("raw_wire_schema_version", "1"),
            replace("case_id", "10200"),
            replace("pool_size", "8"),
            replace("raw_cell_artifact_checksum", "0"),
            replace("candidate_semantic_checksum", "0"),
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
                self.assertEqual(completed.returncode, 2, completed.stderr)
                self.assertEqual(completed.stdout, "")

    def test_cli_obeys_raw_sidecar_report_snapshot_open_order(self) -> None:
        sidecar_fifo = self.root / "oracle-sidecar.fifo"
        report_fifo = self.root / "oracle-report.fifo"
        snapshot_fifo = self.root / "oracle-snapshot.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)
        os.mkfifo(snapshot_fifo)

        invalid_raw = copy.deepcopy(self.raw)
        invalid_raw["corpus_checksum"] = 0
        invalid_raw_path = self.root / "invalid-raw.json"
        invalid_raw_path.write_text(canonical(invalid_raw), encoding="utf-8")
        completed = subprocess.run(
            validator_command(invalid_raw_path, sidecar_fifo, report_fifo, snapshot_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertNotIn("regular file", completed.stderr)

        for sidecar, report, snapshot, expected in (
            (sidecar_fifo, report_fifo, snapshot_fifo, "regular file"),
            (self.sidecar_path, report_fifo, snapshot_fifo, "per-net report"),
            (self.sidecar_path, self.report_path, snapshot_fifo, "exact-small snapshot"),
        ):
            completed = subprocess.run(
                validator_command(self.raw_path, sidecar, report, snapshot),
                check=False,
                text=True,
                capture_output=True,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 1)
            self.assertEqual(completed.stdout, "")
            self.assertIn(expected, completed.stderr)
            self.assertIn("regular file", completed.stderr)

    def test_strict_output_validation_rejects_rechecksummed_aliases(self) -> None:
        mutations: list[dict[str, object]] = []
        for path, value in (
            (("campaign_id",), "phase4_confirmatory_corpus_v1"),
            (("coverage_complete",), 0),
            (("production_is_optimal",), False),
            (("snapshot_binding", "corpus_version"), 1),
            (
                ("same_run_telemetry_binding", "raw_artifact_checksum"),
                self.structural_only_artifact_fixture["same_run_telemetry_binding"][
                    "raw_artifact_checksum"
                ]
                ^ 1,
            ),
        ):
            changed = copy.deepcopy(self.structural_only_artifact_fixture)
            target = changed
            for component in path[:-1]:
                target = target[component]
            target[path[-1]] = value
            mutations.append(changed)

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["canonical_witness"].reverse()
        mutations.append(changed)
        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["unexpected"] = 1
        mutations.append(changed)

        for section, field in (
            (None, "maximum_setup_elapsed_nanoseconds"),
            ("external_budget", "maximum_prepared_elapsed_nanoseconds"),
            ("corpus_limits", "maximum_nets"),
        ):
            changed = copy.deepcopy(self.structural_only_artifact_fixture)
            config = changed["config"]
            if section is None:
                config[field] -= 1
            else:
                config[section][field] -= 1
            changed["raw_binding"]["cell_plan_checksum"] = raw_validator.compute_cell_plan_checksum(
                {
                    "config": config,
                    "corpus_checksum": changed["corpus_checksum"],
                },
                corpus_version=2,
            )
            mutations.append(changed)

        for index, artifact in enumerate(mutations):
            with self.subTest(index=index):
                reauthenticate_oracle(artifact)
                with self.assertRaises(raw_validator.EvidenceError):
                    confirmatory_oracle.serialize_oracle_artifact(artifact)

    def test_false_exact_rejection_guardrail_remains_structurally_explicit(self) -> None:
        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"] = False
        reauthenticate_oracle(changed)
        validated = confirmatory_oracle.validate_oracle_artifact(changed)
        self.assertFalse(
            validated["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"]
        )

    def test_ambient_runfiles_cannot_substitute_candidate_replay(self) -> None:
        shadow = self.root / "shadow-runfiles"
        inner = runfile("phase4_confirmatory_exact_small_oracle_validator_py").resolve()
        bundled_runfiles = pathlib.Path(f"{inner}.runfiles")
        self.assertTrue(bundled_runfiles.is_dir())
        shutil.copytree(bundled_runfiles, shadow, symlinks=True)
        shadow_workspace = shadow / "_main"
        marker = self.root / "shadow-replay-selected"
        target = "phase4_confirmatory_exact_small_candidate_admission_replay"
        helper = shadow_workspace / target
        helper.unlink()
        helper.write_text(
            '#!/bin/sh\n: > "$APGAR_REPLAY_SHADOW_MARKER"\n',
            encoding="utf-8",
        )
        helper.chmod(0o755)
        manifest = self.root / "shadow-runfiles.MF"
        manifest.write_text(f"_main/{target} {helper}\n", encoding="utf-8")
        environment = os.environ.copy()
        environment.update(
            {
                "APGAR_REPLAY_SHADOW_MARKER": str(marker),
                "JAVA_RUNFILES": str(shadow),
                "PYTHON_RUNFILES": str(shadow),
                "RUNFILES_DIR": str(shadow),
                "RUNFILES_MANIFEST_FILE": str(manifest),
                "TEST_SRCDIR": str(shadow),
                "TEST_WORKSPACE": "_main",
            }
        )
        completed = subprocess.run(
            validator_command(
                self.raw_path,
                self.sidecar_path,
                self.report_path,
                self.snapshot_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            env=environment,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn("snapshot", completed.stderr)
        self.assertFalse(marker.exists())

        direct_inner = subprocess.run(
            [
                str(runfile("phase4_confirmatory_exact_small_oracle_validator_py")),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(self.raw_path),
                "--same-run-telemetry",
                str(self.sidecar_path),
                "--report",
                str(self.report_path),
                "--snapshot",
                str(self.snapshot_path),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(direct_inner.returncode, 2)
        self.assertEqual(direct_inner.stdout, "")
        self.assertIn("requires its compiled launcher", direct_inner.stderr)

    def test_compiled_launchers_promote_the_handshake_above_closed_stdin(self) -> None:
        for target in _EXACT_LAUNCHERS_UNDER_TEST:
            with self.subTest(target=target):
                completed = subprocess.run(
                    launcher_help_command(runfile(target)),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                    preexec_fn=lambda: os.close(0),
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertNotIn("requires its compiled launcher", completed.stderr)

    def test_compiled_launchers_skip_stage_one_site_initialization(self) -> None:
        declared_sitecustomize = runfile("sitecustomize.py")
        self.assertTrue(declared_sitecustomize.is_file())
        marker = self.root / "stage-one-sitecustomize-ran"
        probe_launcher = runfile(_EXACT_LAUNCHERS_UNDER_TEST[0]).resolve()
        probe_interpreter = (
            pathlib.Path(f"{probe_launcher}.runfiles")
            / "_main"
            / "_phase4_exact_small_oracle_validator_py.venv"
            / "bin"
            / "python3"
        )
        probe_environment = os.environ.copy()
        probe_environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
        probe = subprocess.run(
            [str(probe_interpreter), "-I", "-B", "-c", "pass"],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
            env=probe_environment,
        )
        self.assertEqual(probe.returncode, 0, probe.stderr)
        self.assertTrue(marker.is_file())
        marker.unlink()
        for target in _EXACT_LAUNCHERS_UNDER_TEST:
            with self.subTest(target=target):
                canonical_launcher = runfile(target).resolve()
                broad_sitecustomize = canonical_launcher.parent / "sitecustomize.py"
                self.assertTrue(broad_sitecustomize.is_file())
                self.assertTrue(os.path.samefile(broad_sitecustomize, declared_sitecustomize))
                environment = os.environ.copy()
                environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
                completed = subprocess.run(
                    launcher_help_command(runfile(target)),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                    env=environment,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertFalse(marker.exists())

    def test_public_launcher_ignores_malformed_enclosing_runfiles_entries(self) -> None:
        target = "phase4_confirmatory_exact_small_oracle_validator"
        canonical_launcher = runfile(target).resolve()
        bundled_runfiles = pathlib.Path(f"{canonical_launcher}.runfiles")
        for mutation in (
            "missing",
            "unmapped",
            "non-interpreter",
            "foreign-manifest",
            "foreign-repo-mapping",
        ):
            with self.subTest(mutation=mutation):
                shadow = self.root / f"{target}-{mutation}.runfiles"
                shutil.copytree(bundled_runfiles, shadow, symlinks=True)
                standard_library = shadow / _PYTHON_REPOSITORY / "lib" / "python3.13"
                if mutation == "missing":
                    (standard_library / "argparse.py").unlink()
                elif mutation == "unmapped":
                    (standard_library / "sitecustomize.py").symlink_to(
                        os.readlink(standard_library / "argparse.py")
                    )
                elif mutation == "non-interpreter":
                    module = shadow / "_main" / "tools" / "validate_phase4_raw_evidence.py"
                    interpreter = (
                        shadow
                        / "_main"
                        / "_phase4_confirmatory_exact_small_oracle_validator_py.venv"
                        / "bin"
                        / "python3"
                    )
                    module.unlink()
                    module.symlink_to(interpreter.resolve())
                else:
                    metadata = "MANIFEST" if mutation == "foreign-manifest" else "_repo_mapping"
                    suffix = ".runfiles_manifest" if metadata == "MANIFEST" else ".repo_mapping"
                    foreign = runfile("phase4_exact_small_oracle_validator").resolve()
                    (shadow / metadata).unlink()
                    (shadow / metadata).symlink_to(pathlib.Path(f"{foreign}{suffix}"))

                completed = subprocess.run(
                    launcher_help_command(shadow / "_main" / target),
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertNotIn("requires its compiled launcher", completed.stderr)


if __name__ == "__main__":
    unittest.main()
