"""Decision-core tests for the frozen Phase 4 matrix aggregator."""

from __future__ import annotations

import copy
import os
import pathlib
import tempfile
import unittest
from collections.abc import Mapping
from typing import Any
from unittest import mock

from tools import aggregate_phase4_matrix as aggregator
from tools import validate_phase4_statistical_protocol as protocol

_COMMIT = "a" * 40


def _binding(kind: str, ordinal: int) -> dict[str, Any]:
    return {
        "kind": kind,
        "path": f"synthetic/{kind}/{ordinal:04d}.json",
        "size_bytes": 1,
        "sha256": f"{ordinal + 1:064x}",
        "artifact_checksum": ordinal + 1,
        "source_envelope_checksum": ordinal + 2,
    }


def _evidence_files() -> list[dict[str, Any]]:
    counts = {
        "raw": 100,
        "same_run": 78,
        "report": 100,
        "capture": 100,
        "operational": 100,
        "exact_snapshot": 3,
        "exact_oracle": 3,
        "fixed_query": 1,
        "stress_probe": 1,
        "stress": 1,
    }
    result: list[dict[str, Any]] = []
    ordinal = 0
    for kind, count in counts.items():
        for _ in range(count):
            result.append(_binding(kind, ordinal))
            ordinal += 1
    return sorted(result, key=lambda row: row["path"])


def _outcome(cost: int) -> dict[str, int]:
    return {
        "selected_net_count": 10,
        "total_overuse_units": 0,
        "total_intrinsic_cost": cost,
    }


def _guardrails(*, exact_scope: bool, selected_scope: bool, imported: bool) -> dict[str, Any]:
    return {
        "equal_selected_and_overuse_cost_nonregression": {
            "status": "evaluated",
            "passed": True,
        },
        "selected_net_nonregression": (
            {"status": "evaluated", "passed": True}
            if selected_scope
            else {
                "status": "not_applicable",
                "reason": "selected_net_nonregression_scope_is_heldout_and_imported",
            }
        ),
        "imported_nonregression": (
            {"status": "evaluated", "passed": True}
            if imported
            else {
                "status": "not_applicable",
                "reason": "imported_guardrails_apply_only_to_imported_cells",
            }
        ),
        "same_run_exact_rejection": (
            {"status": "evaluated", "passed": True}
            if exact_scope
            else {
                "status": "not_applicable",
                "reason": "exact_rejection_scope_is_exact_heldout_and_imported",
            }
        ),
    }


def _timing() -> Mapping[str, Any]:
    return protocol.timing_summary(
        [
            {
                "repetition": repetition,
                "order": "AB" if repetition % 2 == 0 else "BA",
                "baseline_outer_ns": 100,
                "candidate_outer_ns": 90,
            }
            for repetition in range(20)
        ]
    )


def _success_cell(
    case_id: int,
    pool: int,
    meta: Mapping[str, Any],
    *,
    winning_families: set[int],
) -> dict[str, Any]:
    role = meta["role"]
    family = (
        0
        if 1000 <= case_id <= 1007
        else 1
        if 1100 <= case_id <= 1107
        else 2
        if 1200 <= case_id <= 1207
        else None
    )
    candidate_cost = 90 if family in winning_families else 100
    baseline = _outcome(100)
    candidate = _outcome(candidate_cost)
    row: dict[str, Any] = {
        "schema_version": 1,
        "case_id": case_id,
        "requested_pool_size": pool,
        "role": role,
        "evidence_requirement": meta["evidence_requirement"],
        "in_noncalibration_closure": meta["in_noncalibration_closure"],
        "authority_bindings": [
            {
                "kind": "synthetic",
                "path": f"synthetic/{case_id}-{pool}.json",
                "artifact_checksum": case_id + pool,
                "source_envelope_checksum": case_id + pool + 1,
            }
        ],
        "baseline_outcome": baseline,
        "candidate_outcome": candidate,
        "comparison": aggregator._comparison_name(protocol.compare_outcomes(candidate, baseline)),
        "guardrails": _guardrails(
            exact_scope=role in {"exact", "heldout", "imported"},
            selected_scope=role in {"heldout", "imported"},
            imported=role == "imported",
        ),
        "timing_diagnostic": _timing(),
        "exact_small": (
            {
                "snapshot_artifact_checksum": case_id + 10,
                "snapshot_source_envelope_checksum": case_id + 11,
                "oracle_artifact_checksum": case_id + 12,
                "oracle_source_envelope_checksum": case_id + 13,
                "production_is_optimal": True,
            }
            if role == "exact"
            else {
                "status": "not_applicable",
                "reason": "cell_is_not_exact_small",
            }
        ),
        "cell_checksum": 0,
    }
    row["cell_checksum"] = aggregator._cell_checksum(row)
    return row


def _cells(*, winning_families: set[int] = {0, 1}) -> list[dict[str, Any]]:
    metadata = aggregator._group_metadata()
    fixed = _binding("fixed_query", 1000)
    stress = _binding("stress", 1001)
    result: list[dict[str, Any]] = []
    for case_id, pool in sorted(metadata):
        meta = metadata[(case_id, pool)]
        evidence = meta["evidence_requirement"]
        if evidence in {"raw_success", "same_run_raw_success"}:
            result.append(
                _success_cell(
                    case_id,
                    pool,
                    meta,
                    winning_families=winning_families,
                )
            )
        elif evidence == "descriptor_only_excluded":
            result.append(
                aggregator._special_cell(
                    case_id=case_id,
                    pool=pool,
                    role=meta["role"],
                    evidence_requirement=evidence,
                    in_noncalibration_closure=meta["in_noncalibration_closure"],
                    binding=fixed,
                    reason="descriptor_only_fixed_query_control",
                )
            )
        else:
            result.append(
                aggregator._special_cell(
                    case_id=case_id,
                    pool=pool,
                    role=meta["role"],
                    evidence_requirement=evidence,
                    in_noncalibration_closure=meta["in_noncalibration_closure"],
                    binding=stress,
                    reason="compiled_work_bound_stress_control",
                )
            )
    return result


def _aggregate(cells: list[dict[str, Any]]) -> dict[str, Any]:
    evidence = _evidence_files()
    fixed = next(binding for binding in evidence if binding["kind"] == "fixed_query")
    stress = next(binding for binding in evidence if binding["kind"] == "stress")
    return aggregator.aggregate_validated_cells(
        expected_commit=_COMMIT,
        cells=cells,
        evidence_files=evidence,
        host_environment={"schema_version": 1, "environment_checksum": 1},
        reproducibility_provenance={"schema_version": 1, "provenance_checksum": 2},
        fixed_binding=fixed,
        stress_binding=stress,
    )


def _bundle_authorities() -> list[tuple[pathlib.PurePosixPath, str]]:
    result: list[tuple[pathlib.PurePosixPath, str]] = []
    for (case_id, pool), meta in sorted(aggregator._group_metadata().items()):
        evidence = meta["evidence_requirement"]
        if evidence not in {"raw_success", "same_run_raw_success"}:
            continue
        cell = pathlib.PurePosixPath("cells", f"{case_id:04d}", f"k{pool}")
        result.append((cell / "raw.json", "raw"))
        if evidence == "same_run_raw_success":
            result.append((cell / "same-run.json", "same_run"))
        result.extend(
            (
                (cell / "per-net-report.json", "report"),
                (cell / "operational-capture.json", "capture"),
                (cell / "operational-publication.json", "operational"),
            )
        )
        if meta["role"] == "exact":
            result.extend(
                (
                    (cell / "exact-snapshot.json", "exact_snapshot"),
                    (cell / "exact-oracle.json", "exact_oracle"),
                )
            )
    result.extend(
        (
            (pathlib.PurePosixPath("special/fixed-query.json"), "fixed_query"),
            (
                pathlib.PurePosixPath("special/stress-work-bound-probe.json"),
                "stress_probe",
            ),
            (pathlib.PurePosixPath("special/stress.json"), "stress"),
        )
    )
    return result


def _write_synthetic_bundle(root: pathlib.Path) -> None:
    marker = aggregator._root_marker(_COMMIT)
    (root / ".phase4-root.json").write_text(
        aggregator._canonical(marker) + "\n",
        encoding="utf-8",
    )
    for relative, _ in _bundle_authorities():
        path = root / relative.as_posix()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.touch()


def _aggregate_synthetic_bundle(
    root: pathlib.Path,
    *,
    drift_environment_path: str | None = None,
) -> dict[str, Any]:
    authorities = _bundle_authorities()
    ordinals = {relative.as_posix(): index + 1 for index, (relative, _) in enumerate(authorities)}
    metadata = aggregator._group_metadata()
    environment = {"schema_version": 1, "environment_checksum": 41}
    provenance = {"schema_version": 1, "provenance_checksum": 42}

    def fake_read(
        _root: pathlib.Path,
        relative: pathlib.PurePosixPath,
        kind: str,
    ) -> tuple[Mapping[str, Any], dict[str, Any]]:
        name = relative.as_posix()
        if kind == "root_marker":
            document: Mapping[str, Any] = aggregator._root_marker(_COMMIT)
            ordinal = 10_000
        else:
            ordinal = ordinals[name]
            if kind == "raw":
                case_id = int(relative.parts[1])
                pool = int(relative.parts[2][1:])
                selected_environment = (
                    {"schema_version": 1, "environment_checksum": 99}
                    if name == drift_environment_path
                    else environment
                )
                document = {
                    "config": {
                        "case_id": case_id,
                        "requested_pool_size": pool,
                    },
                    "environment": selected_environment,
                }
            elif kind == "operational":
                document = {"reproducibility_provenance": provenance}
            elif kind == "fixed_query":
                document = {
                    "fixed_query_coverage_complete": True,
                    "phase4_complete": False,
                }
            elif kind == "stress":
                document = {
                    "stress_ladder_coverage_complete": True,
                    "phase4_complete": False,
                }
            else:
                document = {}
        binding = {
            "kind": kind,
            "path": name,
            "size_bytes": 1,
            "sha256": f"{ordinal:064x}",
            "artifact_checksum": ordinal,
            "source_envelope_checksum": ordinal + 1,
        }
        return document, binding

    def fake_validate_success(**arguments: Any) -> dict[str, Any]:
        raw = arguments["raw"]
        case_id = raw["config"]["case_id"]
        pool = raw["config"]["requested_pool_size"]
        meta = metadata[(case_id, pool)]
        row = _success_cell(case_id, pool, meta, winning_families={0, 1})
        row["authority_bindings"] = [
            aggregator._binding_reference(binding) for binding in arguments["bindings"]
        ]
        row["cell_checksum"] = aggregator._cell_checksum(row)
        return row

    with (
        mock.patch.object(aggregator, "_read_authority", side_effect=fake_read),
        mock.patch.object(
            aggregator,
            "validate_success_cell_documents",
            side_effect=fake_validate_success,
        ),
        mock.patch.object(aggregator.fixed_query, "validate_artifact"),
        mock.patch.object(aggregator.stress, "validate_artifact"),
        mock.patch.object(
            aggregator.operational_v1,
            "project_document",
            side_effect=lambda raw: {"config": raw["config"]},
        ),
    ):
        return aggregator.aggregate_evidence_root(root, expected_commit=_COMMIT)


class AggregatePhase4MatrixTest(unittest.TestCase):
    def test_two_primary_families_including_coupled_family_pass(self) -> None:
        result = _aggregate(_cells())
        self.assertTrue(result["phase4_complete"])
        self.assertEqual(result["phase4_exit_status"], "passed")
        self.assertEqual(
            result["family_decision"]["qualifying_family_ids"],
            [0, 1],
        )
        self.assertTrue(result["guardrail_summary"]["passed"])
        self.assertEqual(len(result["cells"]), 104)
        self.assertEqual(len(result["evidence_files"]), 487)
        self.assertNotEqual(result["artifact_checksum"], 0)
        self.assertNotEqual(result["source_envelope_checksum"], 0)

    def test_two_uncoupled_winning_families_cannot_replace_family_one(self) -> None:
        result = _aggregate(_cells(winning_families={0, 2}))
        self.assertFalse(result["phase4_complete"])
        self.assertFalse(result["family_decision"]["passed"])
        self.assertEqual(result["family_decision"]["qualifying_family_ids"], [0, 2])

    def test_sensitivity_pools_cannot_rescue_primary_ties(self) -> None:
        cells = _cells(winning_families={0, 1})
        for row in cells:
            if row["role"] == "heldout" and row["requested_pool_size"] == 8:
                row["candidate_outcome"] = copy.deepcopy(row["baseline_outcome"])
                row["comparison"] = "tie"
                row["cell_checksum"] = aggregator._cell_checksum(row)
        result = _aggregate(cells)
        self.assertFalse(result["family_decision"]["passed"])
        self.assertEqual(result["family_decision"]["qualifying_family_ids"], [])

    def test_authentic_exact_rejection_is_a_guardrail_failure(self) -> None:
        cells = _cells()
        row = next(
            item for item in cells if item["case_id"] == 100 and item["requested_pool_size"] == 4
        )
        row["guardrails"]["same_run_exact_rejection"]["passed"] = False
        row["cell_checksum"] = aggregator._cell_checksum(row)
        result = _aggregate(cells)
        self.assertFalse(result["phase4_complete"])
        self.assertEqual(
            result["guardrail_summary"]["same_run_exact_rejection_failures"],
            [[100, 4]],
        )

    def test_unscoped_equal_cost_guardrail_is_not_silently_dropped(self) -> None:
        cells = _cells()
        row = next(
            item
            for item in cells
            if item["role"] == "calibration" and item["requested_pool_size"] == 4
        )
        row["guardrails"]["equal_selected_and_overuse_cost_nonregression"]["passed"] = False
        row["cell_checksum"] = aggregator._cell_checksum(row)
        result = _aggregate(cells)
        self.assertFalse(result["phase4_complete"])
        self.assertEqual(
            result["guardrail_summary"]["equal_selected_and_overuse_cost_failures"],
            [[row["case_id"], 4]],
        )

    def test_missing_reordered_or_duplicate_evidence_is_incomplete_not_loss(self) -> None:
        cells = _cells()
        with self.assertRaisesRegex(aggregator.MatrixError, "missing, duplicated, or reordered"):
            _aggregate(cells[:-1])
        evidence = _evidence_files()
        evidence[1]["path"] = evidence[0]["path"]
        fixed = next(binding for binding in evidence if binding["kind"] == "fixed_query")
        stress = next(binding for binding in evidence if binding["kind"] == "stress")
        with self.assertRaisesRegex(aggregator.MatrixError, "registry"):
            aggregator.aggregate_validated_cells(
                expected_commit=_COMMIT,
                cells=cells,
                evidence_files=evidence,
                host_environment={},
                reproducibility_provenance={},
                fixed_binding=fixed,
                stress_binding=stress,
            )

    def test_cell_or_root_mutation_cannot_reuse_checksums(self) -> None:
        cells = _cells()
        changed = copy.deepcopy(cells)
        changed[0]["comparison"] = "candidate_loss"
        with self.assertRaisesRegex(aggregator.MatrixError, "normalized cell"):
            _aggregate(changed)
        result = _aggregate(cells)
        changed_result = copy.deepcopy(result)
        changed_result["phase4_complete"] = False
        self.assertNotEqual(
            result["artifact_checksum"],
            aggregator._decision_checksum(changed_result),
        )

    def test_oversized_decision_is_rejected_by_the_bounded_authority_reader(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = pathlib.Path(temporary) / "decision.json"
            path.write_bytes(b" " * (aggregator._MAXIMUM_DECISION_BYTES + 1))
            with self.assertRaisesRegex(aggregator.MatrixError, "input bound"):
                aggregator.read_decision(path)

    def test_required_fifo_is_rejected_without_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            os.mkfifo(root / "raw.json")
            with self.assertRaisesRegex(aggregator.MatrixError, "regular non-symlink"):
                aggregator._read_authority(
                    root,
                    pathlib.PurePosixPath("raw.json"),
                    "raw",
                )

    def test_inventory_rejects_unknown_top_level_and_extra_empty_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            raw = root / "cells/0100/k4/raw.json"
            fixed = root / "special/fixed-query.json"
            raw.parent.mkdir(parents=True)
            fixed.parent.mkdir(parents=True)
            raw.touch()
            fixed.touch()
            expected = {
                "cells/0100/k4/raw.json",
                "special/fixed-query.json",
            }
            aggregator._verify_inventory(root, expected)
            extra = root / "cells/9999"
            extra.mkdir()
            with self.assertRaisesRegex(aggregator.MatrixError, "extra_directories"):
                aggregator._verify_inventory(root, expected)
            extra.rmdir()
            (root / "foreign").touch()
            with self.assertRaisesRegex(aggregator.MatrixError, "unknown operator state"):
                aggregator._verify_inventory(root, expected)

    def test_decision_output_cannot_poison_its_evidence_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            (root / "matrix").mkdir()
            (root / "cells").mkdir()
            canonical = root / "matrix/decision-publication.json"
            aggregator._validate_output_location(root, canonical)
            with self.assertRaisesRegex(aggregator.MatrixError, "must be matrix"):
                aggregator._validate_output_location(root, root / "cells/decision.json")
            with self.assertRaisesRegex(aggregator.MatrixError, "must be matrix"):
                aggregator._validate_output_location(root, root / "decision.json")
            aggregator._validate_output_location(
                root,
                root.parent / f"{root.name}-decision.json",
            )

    def test_complete_filesystem_adapter_and_discriminating_failures(self) -> None:
        self.assertEqual(len(_bundle_authorities()), 487)
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            _write_synthetic_bundle(root)
            result = _aggregate_synthetic_bundle(root)
            self.assertEqual(result["coverage"]["logical_cell_count"], 104)
            self.assertEqual(len(result["evidence_files"]), 487)

            missing = root / "cells/0100/k4/raw.json"
            missing.unlink()
            with self.assertRaisesRegex(aggregator.MatrixError, "inventory differs"):
                _aggregate_synthetic_bundle(root)
            missing.touch()

            wrong_disposition = root / "cells/0200/k4/same-run.json"
            wrong_disposition.touch()
            with self.assertRaisesRegex(aggregator.MatrixError, "inventory differs"):
                _aggregate_synthetic_bundle(root)
            wrong_disposition.unlink()

            with self.assertRaisesRegex(aggregator.MatrixError, "execution environment"):
                _aggregate_synthetic_bundle(
                    root,
                    drift_environment_path="cells/0200/k4/raw.json",
                )


if __name__ == "__main__":
    unittest.main()
