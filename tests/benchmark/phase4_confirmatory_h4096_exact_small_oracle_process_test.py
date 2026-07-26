"""Adversarial process coverage for the H=4096 exact-small Oracle authority."""

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

from tests.support import phase4_confirmatory_h4096_test_artifacts as artifacts
from tools import validate_phase4_confirmatory_h4096_exact_small_oracle as h4096_oracle
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import validate_phase4_exact_small_oracle as oracle
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_COMMIT = "a" * 40
_OTHER_COMMIT = "b" * 40
_PRODUCTION_LAUNCHER = "phase4_confirmatory_h4096_exact_small_oracle_validator"
_TEST_CLEAN_LAUNCHER = "phase4_confirmatory_h4096_exact_small_oracle_test_clean_validator"
_TEST_DIRTY_LAUNCHER = "phase4_confirmatory_h4096_exact_small_oracle_test_dirty_validator"
_TEST_UNSTAMPED_LAUNCHER = "phase4_confirmatory_h4096_exact_small_oracle_test_unstamped_validator"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def validator_arguments(
    raw: pathlib.Path,
    sidecar: pathlib.Path,
    report: pathlib.Path,
    snapshot: pathlib.Path,
) -> list[str]:
    return [
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


def run_library_validator(
    raw: pathlib.Path,
    sidecar: pathlib.Path,
    report: pathlib.Path,
    snapshot: pathlib.Path,
) -> tuple[int, str, str]:
    stdout = io.StringIO()
    stderr = io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        result = h4096_oracle.main(validator_arguments(raw, sidecar, report, snapshot))
    return result, stdout.getvalue(), stderr.getvalue()


def snapshot_preflight_command(
    raw: dict[str, object],
    report: dict[str, object],
) -> list[str]:
    config = raw["config"]
    attempt = raw["attempts"][0]
    paired = attempt["result"]
    baseline = attempt["baseline"]["record"]
    candidate = attempt["candidate"]["record"]
    return [
        str(runfile("phase4_confirmatory_h4096_exact_small_snapshot_test_runner")),
        "--testing_allow_unstamped=1",
        "--corpus_version=2",
        "--raw_evidence_schema_version=2",
        "--raw_wire_schema_version=2",
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
        f"--per_net_report_artifact_checksum={report['artifact_checksum']}",
        f"--per_net_report_source_envelope_checksum={report['source_envelope_checksum']}",
    ]


def structural_only_artifact(
    raw: dict[str, object],
    sidecar: dict[str, object],
    report: dict[str, object],
) -> dict[str, object]:
    """Build serializer-only structure; it is never a publication result."""
    snapshot_artifact_checksum = 7001
    snapshot_source_envelope_checksum = oracle.compute_snapshot_source_envelope(
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
    witness = [
        {
            "net": {"id": net_id, "generation": generation},
            "candidate_id": {"high": net_id, "low": index + 1},
        }
        for index, (net_id, generation) in enumerate(
            h4096_raw.validated_confirmatory_h4096_workload_roster(10_100)
        )
    ]
    candidate_semantics = raw["attempts"][0]["candidate"]["record"]["semantics"]
    result: dict[str, object] = {
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
            "authority": "phase4_confirmatory_same_run_raw_evidence_v2",
            "raw_evidence_schema_version": 2,
            "wire_schema_version": 2,
            "cell_plan_checksum": raw["cell_plan_checksum"],
            "artifact_checksum": raw["artifact_checksum"],
            "source_envelope_checksum": raw["source_envelope_checksum"],
        },
        "same_run_telemetry_binding": {
            "authority": "phase4_confirmatory_same_run_decision_telemetry_v2",
            "schema_version": sidecar["schema_version"],
            "raw_evidence_schema_version": sidecar["raw_evidence_schema_version"],
            "raw_wire_schema_version": sidecar["raw_wire_schema_version"],
            "telemetry_wire_schema_version": sidecar["telemetry_wire_schema_version"],
            "raw_artifact_checksum": sidecar["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": sidecar["raw_source_envelope_checksum"],
            "artifact_checksum": sidecar["artifact_checksum"],
            "source_envelope_checksum": sidecar["source_envelope_checksum"],
            "exact_rejection_guardrail_passed": (
                telemetry_validator.exact_rejection_guardrail_passes(sidecar)
            ),
        },
        "per_net_report_binding": {
            "authority": "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
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
        "candidate_semantic_checksum": candidate_semantics["semantic_checksum"],
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
    result["artifact_checksum"] = h4096_oracle.compute_artifact_checksum(result)
    result["source_envelope_checksum"] = h4096_oracle.compute_source_envelope_checksum(result)
    return result


class Phase4ConfirmatoryH4096ExactSmallOracleProcessTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = pathlib.Path(cls.temporary.name)

        cls.raw = artifacts.make_raw(
            10_100,
            4,
            same_run=True,
            h4096=True,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.raw, _COMMIT)
        cls.sidecar = artifacts.make_sidecar(cls.raw)
        cls.report = artifacts.make_report(cls.raw)
        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "sidecar.json"
        cls.report_path = cls.root / "report.json"
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")
        cls.report_path.write_text(canonical(cls.report), encoding="utf-8")

        cls.h2250_raw = artifacts.make_raw(
            10_100,
            4,
            same_run=True,
            h4096=False,
            repetitions=20,
            canonical_confirmatory_caps=True,
        )
        artifacts.mark_raw_clean(cls.h2250_raw, _COMMIT)
        cls.h2250_raw_path = cls.root / "h2250-raw.json"
        cls.h2250_raw_path.write_text(canonical(cls.h2250_raw), encoding="utf-8")

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_structural_payload_keeps_schema_one_and_v2_authorities(self) -> None:
        artifact = structural_only_artifact(self.raw, self.sidecar, self.report)
        serialized = h4096_oracle.serialize_oracle_artifact(artifact)
        self.assertEqual(json.loads(serialized), artifact)
        self.assertEqual(artifact["schema_version"], 1)
        self.assertEqual(
            artifact["raw_binding"]["authority"],
            "phase4_confirmatory_same_run_raw_evidence_v2",
        )
        self.assertEqual(
            artifact["same_run_telemetry_binding"]["authority"],
            "phase4_confirmatory_same_run_decision_telemetry_v2",
        )
        self.assertEqual(
            artifact["per_net_report_binding"]["authority"],
            "phase4_confirmatory_same_run_per_net_report_publication_join_v2",
        )
        self.assertEqual(artifact["production_objective"], artifact["optimum_objective"])
        self.assertEqual(artifact["production_objective"]["total_overuse_units"], 1)

        false_guardrail = copy.deepcopy(artifact)
        false_guardrail["same_run_telemetry_binding"]["exact_rejection_guardrail_passed"] = False
        false_guardrail["artifact_checksum"] = h4096_oracle.compute_artifact_checksum(
            false_guardrail
        )
        false_guardrail["source_envelope_checksum"] = h4096_oracle.compute_source_envelope_checksum(
            false_guardrail
        )
        h4096_oracle.validate_oracle_artifact(false_guardrail)

    def test_rechecksummed_h2250_authority_aliases_reject(self) -> None:
        artifact = structural_only_artifact(self.raw, self.sidecar, self.report)
        for field, value in (
            ("raw_binding", "phase4_confirmatory_same_run_raw_evidence_v1"),
            (
                "same_run_telemetry_binding",
                "phase4_confirmatory_same_run_decision_telemetry_v1",
            ),
            (
                "per_net_report_binding",
                "phase4_confirmatory_same_run_per_net_report_publication_join_v1",
            ),
        ):
            with self.subTest(field=field):
                changed = copy.deepcopy(artifact)
                changed[field]["authority"] = value
                changed["artifact_checksum"] = h4096_oracle.compute_artifact_checksum(changed)
                changed["source_envelope_checksum"] = h4096_oracle.compute_source_envelope_checksum(
                    changed
                )
                with self.assertRaises(raw_validator.EvidenceError):
                    h4096_oracle.validate_oracle_artifact(changed)

        with self.assertRaises(raw_validator.EvidenceError):
            h4096_oracle.validate_publication(
                self.h2250_raw,
                artifacts.make_sidecar(self.h2250_raw),
                artifacts.make_report(self.h2250_raw),
                {},
                expected_commit=_COMMIT,
            )

    def test_valid_h4096_inputs_reach_snapshot_boundary(self) -> None:
        candidate_semantics = self.raw["attempts"][0]["candidate"]["record"]["semantics"]
        self.assertEqual(
            candidate_semantics["budget_checksum"],
            h4096_oracle._PAIRED_SEMANTIC_BUDGET_CHECKSUM,
        )
        with self.assertRaisesRegex(raw_validator.EvidenceError, "snapshot"):
            h4096_oracle.validate_publication(
                self.raw,
                self.sidecar,
                self.report,
                {},
                expected_commit=_COMMIT,
            )

    def test_library_entry_obeys_raw_sidecar_report_snapshot_open_order(self) -> None:
        sidecar_fifo = self.root / "sidecar.fifo"
        report_fifo = self.root / "report.fifo"
        snapshot_fifo = self.root / "snapshot.fifo"
        os.mkfifo(sidecar_fifo)
        os.mkfifo(report_fifo)
        os.mkfifo(snapshot_fifo)

        returncode, stdout, stderr = run_library_validator(
            self.h2250_raw_path,
            sidecar_fifo,
            report_fifo,
            snapshot_fifo,
        )
        self.assertEqual(returncode, 1)
        self.assertEqual(stdout, "")
        self.assertNotIn("regular file", stderr)

        returncode, stdout, stderr = run_library_validator(
            self.raw_path,
            sidecar_fifo,
            report_fifo,
            snapshot_fifo,
        )
        self.assertEqual(returncode, 1)
        self.assertEqual(stdout, "")
        self.assertIn("regular file", stderr)

        returncode, stdout, stderr = run_library_validator(
            self.raw_path,
            self.sidecar_path,
            report_fifo,
            snapshot_fifo,
        )
        self.assertEqual(returncode, 1)
        self.assertEqual(stdout, "")
        self.assertIn("per-net report", stderr)
        self.assertIn("regular file", stderr)

        returncode, stdout, stderr = run_library_validator(
            self.raw_path,
            self.sidecar_path,
            self.report_path,
            snapshot_fifo,
        )
        self.assertEqual(returncode, 1)
        self.assertEqual(stdout, "")
        self.assertIn("exact-small snapshot", stderr)
        self.assertIn("regular file", stderr)

    def test_compiled_launcher_is_required_and_snapshot_targets_are_fixtureless(self) -> None:
        completed = subprocess.run(
            [
                str(runfile("phase4_confirmatory_h4096_exact_small_oracle_validator_py")),
                "--help",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("requires its compiled launcher", completed.stderr)

        completed = subprocess.run(
            [
                str(runfile(_TEST_CLEAN_LAUNCHER)),
                "--expected-commit",
                _COMMIT,
                "--help",
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("usage:", completed.stdout)

        for target in (
            "phase4_confirmatory_h4096_exact_small_snapshot_runner",
            "phase4_confirmatory_h4096_exact_small_snapshot_test_runner",
        ):
            with self.subTest(target=target):
                executable = runfile(target).resolve()
                standalone = pathlib.Path(f"{executable}.runfiles")
                fixture = (
                    standalone
                    / "_main"
                    / "tests"
                    / "fixtures"
                    / "phase4_supported_multinet_v1.kicad_pcb"
                )
                self.assertFalse(fixture.exists())

        completed = subprocess.run(
            snapshot_preflight_command(self.raw, self.report),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("preflight-only", completed.stderr)
        self.assertIn("cannot access a board fixture", completed.stderr)

    def test_source_binding_rejects_before_delegation_or_input_access(self) -> None:
        raw_fifo = self.root / "source-binding-raw.fifo"
        os.mkfifo(raw_fifo)
        absent = self.root / "source-binding-input-does-not-exist.json"

        def command(target: str, *expected_commit: str) -> list[str]:
            return [
                str(runfile(target)),
                *expected_commit,
                "--raw",
                str(raw_fifo),
                "--same-run-telemetry",
                str(absent),
                "--report",
                str(absent),
                "--snapshot",
                str(absent),
            ]

        cases = (
            (
                "clean-built-a-expected-b",
                command(
                    _TEST_CLEAN_LAUNCHER,
                    "--expected-commit",
                    _OTHER_COMMIT,
                ),
                "expected commit mismatch",
            ),
            (
                "abbreviated-split-override",
                command(
                    _TEST_CLEAN_LAUNCHER,
                    "--expected-commit",
                    _COMMIT,
                    "--expected-c",
                    _OTHER_COMMIT,
                ),
                "requires exactly one valid --expected-commit",
            ),
            (
                "abbreviated-equals-override",
                command(
                    _TEST_CLEAN_LAUNCHER,
                    "--expected-commit",
                    _COMMIT,
                    f"--expected-c={_OTHER_COMMIT}",
                ),
                "requires exactly one valid --expected-commit",
            ),
            (
                "dirty",
                command(_TEST_DIRTY_LAUNCHER, "--expected-commit", _COMMIT),
                "rejects dirty source",
            ),
            (
                "unstamped",
                command(_TEST_UNSTAMPED_LAUNCHER, "--expected-commit", _COMMIT),
                "rejects unstamped source",
            ),
            (
                "missing-expected-commit",
                command(_TEST_CLEAN_LAUNCHER),
                "requires exactly one valid --expected-commit",
            ),
            (
                "duplicate-expected-commit",
                command(
                    _TEST_CLEAN_LAUNCHER,
                    "--expected-commit",
                    _COMMIT,
                    f"--expected-commit={_COMMIT}",
                ),
                "requires exactly one valid --expected-commit",
            ),
            (
                "malformed-expected-commit",
                command(_TEST_CLEAN_LAUNCHER, "--expected-commit", "A" * 40),
                "requires exactly one valid --expected-commit",
            ),
        )
        for name, invocation, expected_error in cases:
            with self.subTest(name=name):
                completed = subprocess.run(
                    invocation,
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 2)
                self.assertEqual(completed.stdout, "")
                self.assertIn(expected_error, completed.stderr)
                self.assertNotIn("publication failed", completed.stderr)
                self.assertNotIn("regular file", completed.stderr)

        production = subprocess.run(
            command(_PRODUCTION_LAUNCHER, "--expected-commit", _COMMIT),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(production.returncode, 2)
        self.assertEqual(production.stdout, "")
        self.assertIn("exact-small oracle launcher rejects", production.stderr)
        self.assertNotIn("publication failed", production.stderr)
        self.assertNotIn("regular file", production.stderr)

    def test_test_only_entry_cannot_access_inputs_replay_or_emit(self) -> None:
        raw_fifo = self.root / "test-only-preflight-raw.fifo"
        os.mkfifo(raw_fifo)
        absent = self.root / "test-only-preflight-input-does-not-exist.json"
        completed = subprocess.run(
            [
                str(runfile(_TEST_CLEAN_LAUNCHER)),
                "--expected-commit",
                _COMMIT,
                "--raw",
                str(raw_fifo),
                "--same-run-telemetry",
                str(absent),
                "--report",
                str(absent),
                "--snapshot",
                str(absent),
            ],
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(completed.stdout, "")
        self.assertIn("preflight-only and cannot access inputs or emit", completed.stderr)
        self.assertNotIn("regular file", completed.stderr)

        launcher = runfile(_TEST_CLEAN_LAUNCHER).resolve()
        runfiles_main = pathlib.Path(f"{launcher}.runfiles") / "_main"
        self.assertFalse(
            (
                runfiles_main / "phase4_confirmatory_h4096_exact_small_candidate_admission_replay"
            ).exists()
        )


if __name__ == "__main__":
    unittest.main()
