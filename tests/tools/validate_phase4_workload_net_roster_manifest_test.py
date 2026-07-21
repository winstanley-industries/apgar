"""Adversarial tests for the Phase 4 workload-net roster manifest."""

from __future__ import annotations

import copy
import json
import pathlib
import tempfile
import unittest

from tools import validate_phase4_workload_net_roster_manifest as validator


class WorkloadNetRosterManifestTest(unittest.TestCase):
    def setUp(self) -> None:
        self.source_manifest = (
            pathlib.Path(__file__).resolve().parents[2]
            / "schemas/benchmark/phase4_workload_net_roster_manifest_v1.json"
        )
        self.representative = (
            pathlib.Path(__file__).resolve().parents[2]
            / "schemas/benchmark/phase4_representative_manifest_v1.json"
        )
        self.document = json.loads(self.source_manifest.read_text(encoding="utf-8"))
        self.representative_document = json.loads(self.representative.read_text(encoding="utf-8"))

    def _validate(self, document: object, representative: object | None = None) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.json"
            path.write_text(json.dumps(document), encoding="utf-8")
            representative_path = self.representative
            if representative is not None:
                representative_path = pathlib.Path(directory) / "representative.json"
                representative_path.write_text(json.dumps(representative), encoding="utf-8")
            validator.validate(path, representative_path)

    def test_canonical_manifest_passes(self) -> None:
        validator.validate(self.source_manifest, self.representative)

    def test_rejects_duplicate_extra_wrong_type_and_key_order(self) -> None:
        text = self.source_manifest.read_text(encoding="utf-8")
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "duplicate.json"
            path.write_text(text.replace("{", '{"schema_version": 1,', 1), encoding="utf-8")
            with self.assertRaises(validator.ManifestError):
                validator.validate(path, self.representative)

        changed = copy.deepcopy(self.document)
        changed["extra"] = 1
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

        changed = copy.deepcopy(self.document)
        changed["successful_cases"][0]["workload_net_count"] = True
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

        changed = {"corpus_version": self.document["corpus_version"], **self.document}
        changed.pop("corpus_version")
        changed = {"corpus_version": 1, **changed}
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

    def test_rejects_row_count_order_and_nested_extra_key(self) -> None:
        changed = copy.deepcopy(self.document)
        changed["successful_cases"].pop()
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

        changed = copy.deepcopy(self.document)
        changed["successful_cases"][0], changed["successful_cases"][1] = (
            changed["successful_cases"][1],
            changed["successful_cases"][0],
        )
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

        changed = copy.deepcopy(self.document)
        changed["successful_cases"][0]["extra"] = 1
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

    def test_rejects_rechecksummed_synthetic_and_imported_roster_mutation(self) -> None:
        for index in (0, 37):
            changed = copy.deepcopy(self.document)
            changed["successful_cases"][index]["roster_checksum"] += 1
            changed["manifest_checksum"] = validator._manifest_checksum(
                changed["schema_version"],
                changed["corpus_version"],
                changed["corpus_checksum"],
                changed["successful_cases"],
                changed["excluded_cases"],
            )
            with self.assertRaises(validator.ManifestError):
                self._validate(changed)

    def test_rejects_rechecksummed_exclusion_and_manifest_mutation(self) -> None:
        changed = copy.deepcopy(self.document)
        changed["excluded_cases"][2]["descriptor_fingerprint"] += 1
        changed["manifest_checksum"] = validator._manifest_checksum(
            changed["schema_version"],
            changed["corpus_version"],
            changed["corpus_checksum"],
            changed["successful_cases"],
            changed["excluded_cases"],
        )
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

        changed = copy.deepcopy(self.document)
        changed["manifest_checksum"] += 1
        with self.assertRaises(validator.ManifestError):
            self._validate(changed)

    def test_rejects_coordinated_foreign_corpus_and_manifest_rehash(self) -> None:
        representative = copy.deepcopy(self.representative_document)
        changed = copy.deepcopy(self.document)
        foreign_checksum = validator._CORPUS_CHECKSUM + 1
        representative["corpus_checksum"] = foreign_checksum
        changed["corpus_checksum"] = foreign_checksum
        for row in changed["successful_cases"]:
            row["corpus_checksum"] = foreign_checksum
            row["roster_checksum"] = validator._roster_checksum(row)
        changed["manifest_checksum"] = validator._manifest_checksum(
            changed["schema_version"],
            changed["corpus_version"],
            changed["corpus_checksum"],
            changed["successful_cases"],
            changed["excluded_cases"],
        )
        with self.assertRaises(validator.ManifestError):
            self._validate(changed, representative)

        representative = copy.deepcopy(self.representative_document)
        changed = copy.deepcopy(self.document)
        representative["cases"][0]["descriptor_fingerprint"] += 1
        changed["successful_cases"][0]["descriptor_fingerprint"] += 1
        changed["successful_cases"][0]["roster_checksum"] = validator._roster_checksum(
            changed["successful_cases"][0]
        )
        changed["manifest_checksum"] = validator._manifest_checksum(
            changed["schema_version"],
            changed["corpus_version"],
            changed["corpus_checksum"],
            changed["successful_cases"],
            changed["excluded_cases"],
        )
        with self.assertRaises(validator.ManifestError):
            self._validate(changed, representative)

    def test_rejects_zero_or_oversized_representative_net_counts(self) -> None:
        for case_id, net_count in ((100, 0), (100, 4097), (3001, 0), (3001, 4097)):
            representative = copy.deepcopy(self.representative_document)
            case = next(row for row in representative["cases"] if row["case_id"] == case_id)
            case["workload_net_count"] = net_count
            with self.subTest(case_id=case_id, net_count=net_count):
                with self.assertRaises(validator.ManifestError):
                    self._validate(self.document, representative)

    def test_rejects_json_larger_than_input_bound(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.json"
            path.write_text(" " * (validator._MAX_JSON_BYTES + 1), encoding="utf-8")
            with self.assertRaisesRegex(validator.ManifestError, "exceeds"):
                validator.validate(path, self.representative)


if __name__ == "__main__":
    unittest.main()
