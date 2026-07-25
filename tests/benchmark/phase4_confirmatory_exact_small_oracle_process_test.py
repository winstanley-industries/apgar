"""Adversarial process coverage for the confirmatory exact-small oracle slice."""

from __future__ import annotations

import copy
import json
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from tools import validate_phase4_confirmatory_exact_small_oracle as confirmatory_oracle
from tools import validate_phase4_exact_small_oracle as legacy_oracle
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator

_COMMIT = "a" * 40
_OPTIMALITY_ERROR = "production frozen-pool objective is not exact-optimal"
_PRODUCTION_OBJECTIVE = (6, 3, 112200)
_OPTIMUM_OBJECTIVE = (6, 1, 159000)
_PUBLIC_EXACT_LAUNCHERS = (
    "phase4_exact_small_oracle_validator",
    "phase4_exact_small_oracle_v2_validator",
    "phase4_confirmatory_exact_small_oracle_validator",
)
_PYTHON_REPOSITORY = "rules_python++python+python_3_13_x86_64-unknown-linux-gnu"


def runfile(relative: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / relative


def canonical(value: object) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"


def normalize_clean_source(
    raw: dict[str, object],
    sidecar: dict[str, object] | None = None,
) -> None:
    raw["source_commit"] = _COMMIT
    raw["source_stamped"] = True
    raw["source_tree_dirty"] = False
    raw["source_envelope_checksum"] = raw_validator.compute_source_envelope_checksum(raw)
    if sidecar is None:
        return
    sidecar["source_commit"] = _COMMIT
    sidecar["source_stamped"] = True
    sidecar["source_tree_dirty"] = False
    sidecar["raw_source_envelope_checksum"] = raw["source_envelope_checksum"]
    sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(sidecar)
    sidecar["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
        sidecar
    )


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


def report_command(raw: dict[str, object], *, confirmatory: bool) -> list[str]:
    if confirmatory:
        return [
            str(runfile("phase4_confirmatory_same_run_per_net_report_test_runner")),
            "--corpus_version=2",
            "--raw_wire_schema_version=2",
            *join_arguments(raw),
        ]
    return [
        str(runfile("phase4_per_net_report_test_runner")),
        *join_arguments(raw),
    ]


def snapshot_command(
    raw: dict[str, object],
    report: dict[str, object],
    *,
    confirmatory: bool,
) -> list[str]:
    if confirmatory:
        command = [
            str(runfile("phase4_confirmatory_exact_small_snapshot_test_runner")),
            "--corpus_version=2",
            "--raw_evidence_schema_version=2",
            "--raw_wire_schema_version=2",
        ]
    else:
        command = [str(runfile("phase4_exact_small_snapshot_test_runner"))]
    return [
        *command,
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


def checked_json(command: list[str], *, timeout: int = 180) -> tuple[dict[str, object], str]:
    completed = subprocess.run(
        command,
        check=False,
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if completed.returncode != 0:
        raise AssertionError(completed.stderr)
    return json.loads(completed.stdout), completed.stdout


def make_false_guardrail(sidecar: dict[str, object]) -> dict[str, object]:
    changed = copy.deepcopy(sidecar)
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
    selected_arm["telemetry"]["telemetry_checksum"] = same_run_validator.compute_telemetry_checksum(
        selected_arm["telemetry"]
    )
    selected_arm["capture_checksum"] = same_run_validator.compute_arm_capture_checksum(selected_arm)
    selected_attempt["capture_checksum"] = same_run_validator.compute_pair_capture_checksum(
        selected_attempt
    )
    changed["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(changed)
    changed["source_envelope_checksum"] = same_run_validator.compute_source_envelope_checksum(
        changed
    )
    return changed


def reauthenticate_oracle(artifact: dict[str, object]) -> None:
    artifact["artifact_checksum"] = confirmatory_oracle.compute_artifact_checksum(artifact)
    artifact["source_envelope_checksum"] = confirmatory_oracle.compute_source_envelope_checksum(
        artifact
    )


def reauthenticate_report_snapshot(
    report: dict[str, object],
    snapshot: dict[str, object],
) -> None:
    telemetry = report["arms"][1]["diagnostic"]["telemetry"]
    telemetry["telemetry_checksum"] = report_validator.compute_telemetry_checksum(telemetry)
    report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(report)
    report["source_envelope_checksum"] = report_validator.compute_report_source_envelope_checksum(
        report
    )
    snapshot["per_net_candidate_telemetry_checksum"] = telemetry["telemetry_checksum"]
    snapshot["per_net_report_artifact_checksum"] = report["artifact_checksum"]
    snapshot["per_net_report_source_envelope_checksum"] = report["source_envelope_checksum"]
    snapshot["artifact_checksum"] = legacy_oracle.compute_snapshot_artifact_checksum(snapshot)
    snapshot["source_envelope_checksum"] = legacy_oracle.compute_snapshot_source_envelope(snapshot)


def structural_only_oracle_fixture(
    raw: dict[str, object],
    sidecar: dict[str, object],
    report: dict[str, object],
    snapshot: dict[str, object],
    *,
    optimum: tuple[int, int, int],
    optimum_count: int,
    witness_overused_resource_count: int,
    witness_rows: list[dict[str, object]],
) -> dict[str, object]:
    """Build serializer-only structure; this is never a publication result."""
    objective = dict(
        zip(
            (
                "selected_net_count",
                "total_overuse_units",
                "total_intrinsic_base_cost",
            ),
            optimum,
            strict=True,
        )
    )
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
            "schema_version": snapshot["schema_version"],
            "corpus_version": 2,
            "raw_artifact_checksum": snapshot["raw_cell_artifact_checksum"],
            "raw_source_envelope_checksum": snapshot["raw_source_envelope_checksum"],
            "per_net_report_artifact_checksum": snapshot["per_net_report_artifact_checksum"],
            "per_net_report_source_envelope_checksum": snapshot[
                "per_net_report_source_envelope_checksum"
            ],
            "artifact_checksum": snapshot["artifact_checksum"],
            "source_envelope_checksum": snapshot["source_envelope_checksum"],
        },
        "candidate_semantic_checksum": snapshot["candidate_semantic_checksum"],
        "cartesian_product": snapshot["cartesian_product"],
        "production_objective": copy.deepcopy(objective),
        "optimum_objective": copy.deepcopy(objective),
        "production_overused_resource_count": witness_overused_resource_count,
        "canonical_witness_overused_resource_count": witness_overused_resource_count,
        "production_is_optimal": True,
        "optimum_count": optimum_count,
        "canonical_witness": copy.deepcopy(witness_rows),
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

        cls.raw_path = cls.root / "raw.json"
        cls.sidecar_path = cls.root / "same-run.json"
        raw, _ = checked_json(
            [
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
                f"--same_run_telemetry_output={cls.sidecar_path}",
            ]
        )
        cls.raw = raw
        cls.sidecar = same_run_validator.read_document(cls.sidecar_path)
        normalize_clean_source(cls.raw, cls.sidecar)
        cls.raw_path.write_text(canonical(cls.raw), encoding="utf-8")
        cls.sidecar_path.write_text(canonical(cls.sidecar), encoding="utf-8")

        cls.report, report_bytes = checked_json(report_command(cls.raw, confirmatory=True))
        cls.report_path = cls.root / "report.json"
        cls.report_path.write_text(report_bytes, encoding="utf-8")

        snapshot = snapshot_command(cls.raw, cls.report, confirmatory=True)
        cls.snapshot, cls.first_snapshot_bytes = checked_json(snapshot)
        second_snapshot, cls.second_snapshot_bytes = checked_json(snapshot)
        if second_snapshot != cls.snapshot:
            raise AssertionError("confirmatory snapshot documents are not deterministic")
        cls.snapshot_path = cls.root / "snapshot.json"
        cls.snapshot_path.write_text(cls.first_snapshot_bytes, encoding="utf-8")

        cls.validated_snapshot = legacy_oracle._parse_snapshot(
            cls.snapshot,
            corpus_version=2,
            allowed_case_ids=frozenset({10100}),
        )
        capacity = cls.validated_snapshot["capacity"]
        overrides = {
            legacy_oracle._resource_key(override["resource"]): override["capacity_units"]
            for override in capacity["overrides"]
        }
        cls.production_objective, cls.production_overused_resource_count = legacy_oracle._score(
            legacy_oracle._production_choices(cls.validated_snapshot),
            capacity["default_capacity_units"],
            overrides,
        )
        cls.optimum_objective, cls.optimum_count, cls.canonical_witness = (
            legacy_oracle.enumerate_exact_oracle(cls.validated_snapshot)
        )
        _, cls.witness_overused_resource_count = legacy_oracle._score(
            cls.canonical_witness,
            capacity["default_capacity_units"],
            overrides,
        )
        cls.canonical_witness_rows = [
            {
                "net": cls.validated_snapshot["pools"][index]["net"],
                "candidate_id": None if candidate is None else candidate["id"],
            }
            for index, candidate in enumerate(cls.canonical_witness)
        ]
        cls.structural_only_artifact_fixture = structural_only_oracle_fixture(
            cls.raw,
            cls.sidecar,
            cls.report,
            cls.snapshot,
            optimum=cls.optimum_objective,
            optimum_count=cls.optimum_count,
            witness_overused_resource_count=cls.witness_overused_resource_count,
            witness_rows=cls.canonical_witness_rows,
        )

        legacy_raw, _ = checked_json(
            [
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
        )
        cls.legacy_raw = legacy_raw
        normalize_clean_source(cls.legacy_raw)
        cls.legacy_report, _ = checked_json(report_command(cls.legacy_raw, confirmatory=False))
        cls.legacy_snapshot, _ = checked_json(
            snapshot_command(cls.legacy_raw, cls.legacy_report, confirmatory=False)
        )

    @classmethod
    def tearDownClass(cls) -> None:
        cls.temporary.cleanup()

    def test_real_pipeline_deterministically_fails_closed_on_nonoptimality(self) -> None:
        self.assertEqual(self.first_snapshot_bytes, self.second_snapshot_bytes)
        self.assertEqual(self.snapshot["config"]["case_id"], 10100)
        self.assertEqual(self.snapshot["config"]["requested_pool_size"], 4)
        self.assertEqual(len(self.snapshot["pools"]), 6)
        self.assertLessEqual(self.snapshot["cartesian_product"], 4096)

        for _ in range(2):
            with self.assertRaises(raw_validator.EvidenceError) as raised:
                confirmatory_oracle.validate_publication(
                    self.raw,
                    self.sidecar,
                    self.report,
                    self.snapshot,
                    expected_commit=_COMMIT,
                )
            self.assertEqual(str(raised.exception), _OPTIMALITY_ERROR)

        commands = [
            validator_command(
                self.raw_path,
                self.sidecar_path,
                self.report_path,
                self.snapshot_path,
            )
            for _ in range(2)
        ]
        completed = [
            subprocess.run(
                command,
                check=False,
                text=True,
                capture_output=True,
                timeout=30,
            )
            for command in commands
        ]
        expected_stderr = (
            f"Phase 4 confirmatory exact-small publication failed: {_OPTIMALITY_ERROR}\n"
        )
        self.assertEqual(completed[0].returncode, 1)
        self.assertEqual(completed[1].returncode, 1)
        self.assertEqual(completed[0].stdout, "")
        self.assertEqual(completed[1].stdout, "")
        self.assertEqual(completed[0].stderr, expected_stderr)
        self.assertEqual(completed[1].stderr, expected_stderr)

    def test_exact_enumeration_records_production_gap_and_unique_witness(self) -> None:
        self.assertEqual(self.production_objective, _PRODUCTION_OBJECTIVE)
        self.assertEqual(self.production_overused_resource_count, 3)
        self.assertEqual(self.optimum_objective, _OPTIMUM_OBJECTIVE)
        self.assertEqual(self.witness_overused_resource_count, 1)
        self.assertEqual(self.optimum_count, 1)
        observed_witness = tuple(
            (
                row["net"]["id"],
                row["net"]["generation"],
                row["candidate_id"]["high"],
                row["candidate_id"]["low"],
            )
            for row in self.canonical_witness_rows
        )
        self.assertEqual(
            observed_witness,
            (
                (1000, 0, 3497063051094027350, 16821193555804857679),
                (1001, 0, 17864995403255075666, 9207615963977360659),
                (1002, 0, 11178711562351457748, 14127062338892660817),
                (1003, 0, 12106538769350876463, 8429441268841098814),
                (1004, 0, 4335188433132766260, 8345570630987037113),
                (1005, 0, 14094642696369552083, 14501634967366069430),
            ),
        )
        production_choices = legacy_oracle._production_choices(self.validated_snapshot)
        self.assertEqual(
            [
                index
                for index, (production, optimum) in enumerate(
                    zip(production_choices, self.canonical_witness, strict=True)
                )
                if production["id"] != optimum["id"]
            ],
            [0, 2],
        )

    def test_snapshot_runner_scope_is_closed_before_stdout(self) -> None:
        valid = snapshot_command(self.raw, self.report, confirmatory=True)

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
            replace("case_id", "10101"),
            replace("case_id", "10102"),
            replace("case_id", "10200"),
            replace("case_id", "11000"),
            replace("case_id", "12002"),
            replace("case_id", "13000"),
            replace("case_id", "14000"),
            replace("pool_size", "8"),
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

        production = list(valid)
        production[0] = str(runfile("phase4_confirmatory_exact_small_snapshot_runner"))
        completed = subprocess.run(
            production,
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 2, completed.stderr)
        self.assertEqual(completed.stdout, "")

    def test_snapshot_runner_rejects_incomplete_authorities_before_work(self) -> None:
        valid = snapshot_command(self.raw, self.report, confirmatory=True)

        def replace(command: list[str], name: str, value: int) -> list[str]:
            prefix = f"--{name}="
            return [
                f"{prefix}{value}" if argument.startswith(prefix) else argument
                for argument in command
            ]

        raw_without_artifact = copy.deepcopy(self.raw)
        raw_without_artifact["artifact_checksum"] = 0
        zero_raw = replace(valid, "raw_cell_artifact_checksum", 0)
        zero_raw = replace(
            zero_raw,
            "raw_source_envelope_checksum",
            raw_validator.compute_source_envelope_checksum(raw_without_artifact),
        )

        report_without_artifact = copy.deepcopy(self.report)
        report_without_artifact["artifact_checksum"] = 0
        zero_report = replace(valid, "per_net_report_artifact_checksum", 0)
        zero_report = replace(
            zero_report,
            "per_net_report_source_envelope_checksum",
            report_validator.compute_report_source_envelope_checksum(report_without_artifact),
        )

        mutations = [zero_raw, zero_report]
        for field in (
            "pair_attempt_checksum",
            "paired_semantic_checksum",
            "paired_artifact_checksum",
            "baseline_semantic_checksum",
            "baseline_arm_artifact_checksum",
            "candidate_semantic_checksum",
            "candidate_arm_artifact_checksum",
        ):
            mutations.append(replace(valid, field, 0))

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

    def test_cross_authority_documents_and_rechecksummed_substitutions_reject(self) -> None:
        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle.validate_publication(
                self.legacy_raw,
                self.sidecar,
                self.report,
                self.snapshot,
                expected_commit=_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            legacy_oracle.validate_publication(
                self.raw,
                self.report,
                self.snapshot,
                expected_commit=_COMMIT,
            )
        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle.validate_publication(
                self.raw,
                self.sidecar,
                self.report,
                self.legacy_snapshot,
                expected_commit=_COMMIT,
            )

        foreign_sidecar = copy.deepcopy(self.sidecar)
        foreign_sidecar["raw_cell_artifact_checksum"] ^= 1
        foreign_sidecar["artifact_checksum"] = same_run_validator.compute_cell_capture_checksum(
            foreign_sidecar
        )
        foreign_sidecar["source_envelope_checksum"] = (
            same_run_validator.compute_source_envelope_checksum(foreign_sidecar)
        )
        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle.validate_publication(
                self.raw,
                foreign_sidecar,
                self.report,
                self.snapshot,
                expected_commit=_COMMIT,
            )

        foreign_report = copy.deepcopy(self.report)
        foreign_report["raw_cell_artifact_checksum"] ^= 1
        foreign_report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_report
        )
        foreign_report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_report)
        )
        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle.validate_publication(
                self.raw,
                self.sidecar,
                foreign_report,
                self.snapshot,
                expected_commit=_COMMIT,
            )

        foreign_snapshot = copy.deepcopy(self.snapshot)
        foreign_snapshot["raw_cell_artifact_checksum"] ^= 1
        foreign_snapshot["artifact_checksum"] = legacy_oracle.compute_snapshot_artifact_checksum(
            foreign_snapshot
        )
        foreign_snapshot["source_envelope_checksum"] = (
            legacy_oracle.compute_snapshot_source_envelope(foreign_snapshot)
        )
        with self.assertRaises(raw_validator.EvidenceError):
            confirmatory_oracle.validate_publication(
                self.raw,
                self.sidecar,
                self.report,
                foreign_snapshot,
                expected_commit=_COMMIT,
            )

    def test_rechecksummed_false_final_pool_diagnostics_reject_before_optimality(self) -> None:
        telemetry = self.report["arms"][1]["diagnostic"]["telemetry"]["per_net"]
        mutations: list[tuple[str, dict[str, object], dict[str, object]]] = []

        for field in (
            "unique_geometry_signature_count",
            "unique_resource_signature_count",
        ):
            report = copy.deepcopy(self.report)
            snapshot = copy.deepcopy(self.snapshot)
            row = next(
                row
                for row in report["arms"][1]["diagnostic"]["telemetry"]["per_net"]
                if row[field] > 1
            )
            row[field] -= 1
            reauthenticate_report_snapshot(report, snapshot)
            mutations.append((field, report, snapshot))

        report = copy.deepcopy(self.report)
        snapshot = copy.deepcopy(self.snapshot)
        overlap_fields = (
            "mean_resource_overlap_ppm",
            "minimum_resource_overlap_ppm",
            "mean_geometric_overlap_ppm",
            "minimum_geometric_overlap_ppm",
        )
        row = next(
            row
            for row in report["arms"][1]["diagnostic"]["telemetry"]["per_net"]
            if row["candidate_pair_count"] != 0 and any(row[field] != 0 for field in overlap_fields)
        )
        for field in overlap_fields:
            row[field] = 0
        reauthenticate_report_snapshot(report, snapshot)
        mutations.append(("overlap_summaries", report, snapshot))

        report = copy.deepcopy(self.report)
        snapshot = copy.deepcopy(self.snapshot)
        row = next(
            row
            for row in report["arms"][1]["diagnostic"]["telemetry"]["per_net"]
            if row["pool_best_intrinsic_cost"] not in (None, 0)
        )
        row["pool_best_intrinsic_cost"] = 0
        reauthenticate_report_snapshot(report, snapshot)
        mutations.append(("pool_best_intrinsic_cost", report, snapshot))

        self.assertTrue(any(row["unique_geometry_signature_count"] > 1 for row in telemetry))
        self.assertTrue(any(row["unique_resource_signature_count"] > 1 for row in telemetry))
        for label, report, snapshot in mutations:
            with self.subTest(label=label):
                with self.assertRaises(raw_validator.EvidenceError) as raised:
                    confirmatory_oracle.validate_publication(
                        self.raw,
                        self.sidecar,
                        report,
                        snapshot,
                        expected_commit=_COMMIT,
                    )
                self.assertEqual(
                    str(raised.exception),
                    "snapshot final-pool diagnostics differ from per-net candidate telemetry",
                )

    def test_fixed_candidate_replay_targets_cross_reject(self) -> None:
        confirmatory_payload = legacy_oracle._candidate_admission_payload(
            self.snapshot,
            corpus_version=2,
        )
        legacy_payload = legacy_oracle._candidate_admission_payload(
            self.legacy_snapshot,
            corpus_version=1,
        )
        confirmatory_replay = str(
            runfile("phase4_confirmatory_exact_small_candidate_admission_replay")
        )
        legacy_replay = str(runfile("phase4_exact_small_candidate_admission_replay"))
        for target, payload, expected in (
            (confirmatory_replay, confirmatory_payload, 0),
            (legacy_replay, legacy_payload, 0),
            (legacy_replay, confirmatory_payload, 1),
            (confirmatory_replay, legacy_payload, 1),
        ):
            completed = subprocess.run(
                [target],
                input=payload,
                check=False,
                capture_output=True,
                timeout=30,
            )
            self.assertEqual(completed.returncode, expected, completed.stderr)
            self.assertEqual(completed.stdout, b"")

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

        completed = subprocess.run(
            validator_command(self.raw_path, sidecar_fifo, report_fifo, snapshot_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
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
            validator_command(self.raw_path, foreign_sidecar_path, report_fifo, snapshot_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertNotIn("per-net report must be a regular file", completed.stderr)

        completed = subprocess.run(
            validator_command(self.raw_path, self.sidecar_path, report_fifo, snapshot_fifo),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn("per-net report", completed.stderr)
        self.assertIn("regular file", completed.stderr)

        foreign_report = copy.deepcopy(self.report)
        foreign_report["raw_cell_artifact_checksum"] ^= 1
        foreign_report["artifact_checksum"] = report_validator.compute_report_artifact_checksum(
            foreign_report
        )
        foreign_report["source_envelope_checksum"] = (
            report_validator.compute_report_source_envelope_checksum(foreign_report)
        )
        foreign_report_path = self.root / "foreign-report.json"
        foreign_report_path.write_text(canonical(foreign_report), encoding="utf-8")
        completed = subprocess.run(
            validator_command(
                self.raw_path,
                self.sidecar_path,
                foreign_report_path,
                snapshot_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertNotIn("exact-small snapshot must be a regular file", completed.stderr)

        completed = subprocess.run(
            validator_command(
                self.raw_path,
                self.sidecar_path,
                self.report_path,
                snapshot_fifo,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertIn("exact-small snapshot", completed.stderr)
        self.assertIn("regular file", completed.stderr)

    def test_strict_output_validation_rejects_rechecksummed_aliases(self) -> None:
        serialized = confirmatory_oracle.serialize_oracle_artifact(
            self.structural_only_artifact_fixture
        )
        self.assertEqual(json.loads(serialized), self.structural_only_artifact_fixture)
        mutations: list[dict[str, object]] = []

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["campaign_id"] = "phase4_confirmatory_corpus_v1"
        mutations.append(changed)

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["coverage_complete"] = 0
        mutations.append(changed)

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["production_is_optimal"] = False
        mutations.append(changed)

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["snapshot_binding"]["corpus_version"] = 1
        mutations.append(changed)

        changed = copy.deepcopy(self.structural_only_artifact_fixture)
        changed["same_run_telemetry_binding"]["raw_artifact_checksum"] ^= 1
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
        self.assertIn(_OPTIMALITY_ERROR, completed.stderr)
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

    def test_public_launchers_promote_the_handshake_above_closed_stdin(self) -> None:
        for target in _PUBLIC_EXACT_LAUNCHERS:
            with self.subTest(target=target):
                completed = subprocess.run(
                    [str(runfile(target)), "--help"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                    preexec_fn=lambda: os.close(0),
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertNotIn("requires its compiled launcher", completed.stderr)

    def test_public_launchers_skip_stage_one_site_initialization(self) -> None:
        declared_sitecustomize = runfile("sitecustomize.py")
        self.assertTrue(declared_sitecustomize.is_file())
        marker = self.root / "stage-one-sitecustomize-ran"
        probe_launcher = runfile(_PUBLIC_EXACT_LAUNCHERS[0]).resolve()
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
        for target in _PUBLIC_EXACT_LAUNCHERS:
            with self.subTest(target=target):
                canonical_launcher = runfile(target).resolve()
                broad_sitecustomize = canonical_launcher.parent / "sitecustomize.py"
                self.assertTrue(broad_sitecustomize.is_file())
                self.assertTrue(os.path.samefile(broad_sitecustomize, declared_sitecustomize))
                environment = os.environ.copy()
                environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
                completed = subprocess.run(
                    [str(runfile(target)), "--help"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                    env=environment,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertFalse(marker.exists())

    def test_public_launchers_ignore_manifest_declared_enclosing_package_init(self) -> None:
        marker = self.root / "enclosing-package-init-ran"
        shadow = self.root / "manifest-declared-superset.runfiles"
        shutil.copytree(
            pathlib.Path(os.environ["TEST_SRCDIR"]),
            shadow,
            symlinks=True,
        )
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
        for target in _PUBLIC_EXACT_LAUNCHERS:
            with self.subTest(target=target):
                environment = os.environ.copy()
                environment["APGAR_PHASE4_ENCLOSING_INIT_MARKER"] = str(marker)
                completed = subprocess.run(
                    [str(main / target), "--help"],
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
                    [str(shadow / "_main" / target), "--help"],
                    check=False,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn("usage:", completed.stdout)
                self.assertNotIn("requires its compiled launcher", completed.stderr)

    def test_false_exact_rejection_guardrail_reaches_the_optimality_gate(self) -> None:
        changed = make_false_guardrail(self.sidecar)
        same_run_validator.validate_confirmatory_join(
            self.raw,
            changed,
            expected_commit=_COMMIT,
        )
        self.assertFalse(same_run_validator.exact_rejection_guardrail_passes(changed))
        with self.assertRaises(raw_validator.EvidenceError) as raised:
            confirmatory_oracle.validate_publication(
                self.raw,
                changed,
                self.report,
                self.snapshot,
                expected_commit=_COMMIT,
            )
        self.assertEqual(str(raised.exception), _OPTIMALITY_ERROR)
        self.assertNotIn("guardrail", str(raised.exception))

        changed_path = self.root / "false-guardrail.json"
        changed_path.write_text(canonical(changed), encoding="utf-8")
        completed = subprocess.run(
            validator_command(
                self.raw_path,
                changed_path,
                self.report_path,
                self.snapshot_path,
            ),
            check=False,
            text=True,
            capture_output=True,
            timeout=30,
        )
        self.assertEqual(completed.returncode, 1)
        self.assertEqual(completed.stdout, "")
        self.assertEqual(
            completed.stderr,
            f"Phase 4 confirmatory exact-small publication failed: {_OPTIMALITY_ERROR}\n",
        )


if __name__ == "__main__":
    unittest.main()
