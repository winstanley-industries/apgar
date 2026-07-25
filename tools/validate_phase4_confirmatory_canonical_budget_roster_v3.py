"""Strict validator for the inactive Phase 4 Corpus-v2 H=4096 budget roster."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_representative_manifest_v2 as authorities
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_ROSTER = _ROOT / "schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v3.json"
_SCHEMA_VERSION = 3
_AUTHORITY = "phase4_confirmatory_canonical_algorithm_budget_roster_v3"
_CONFIGURATION_AUTHORITY = "phase4_confirmatory_corpus_v2_h4096"
_CORPUS_VERSION = authorities._CORPUS_VERSION
_CORPUS_CHECKSUM = authorities._CORPUS_CHECKSUM
_REPRESENTATIVE_MANIFEST_SCHEMA_VERSION = 2
_REPRESENTATIVE_MANIFEST_CHECKSUM = authorities._REPRESENTATIVE_MANIFEST_CHECKSUM
_WORKLOAD_ROSTER_MANIFEST_SCHEMA_VERSION = 2
_WORKLOAD_ROSTER_MANIFEST_CHECKSUM = authorities._ROSTER_MANIFEST_CHECKSUM
_SUPERSEDED_ROSTER_SCHEMA_VERSION = 2
_SUPERSEDED_ROSTER_CHECKSUM = authorities._BUDGET_ROSTER_CHECKSUM
_ROSTER_CHECKSUM = 18429170436700418962
_CELL_COUNT = 102
_ROOT_FIELDS = (
    "schema_version",
    "authority",
    "corpus_version",
    "corpus_checksum",
    "representative_manifest_schema_version",
    "representative_manifest_checksum",
    "workload_roster_manifest_schema_version",
    "workload_roster_manifest_checksum",
    "supersedes",
    "configuration_authority",
    "configuration",
    "cell_count",
    "roster_checksum",
    "canonical_algorithm_budgets",
)
_SUPERSEDES_FIELDS = ("schema_version", "roster_checksum")
_CONFIGURATION_FIELDS = (
    "present_step_per_overuse_unit",
    "superseded_history_step_per_overuse_unit",
    "history_step_per_overuse_unit",
    "baseline_and_candidate_price_configs_equal",
    "all_other_canonical_algorithm_budget_fields_unchanged",
    "query_work_and_external_opportunity_unchanged",
)


class BudgetRosterV3Error(ValueError):
    """Stable malformed-budget-roster diagnostic."""


def _expected_configuration() -> dict[str, Any]:
    return {
        "present_step_per_overuse_unit": 1,
        "superseded_history_step_per_overuse_unit": 2250,
        "history_step_per_overuse_unit": 4096,
        "baseline_and_candidate_price_configs_equal": True,
        "all_other_canonical_algorithm_budget_fields_unchanged": True,
        "query_work_and_external_opportunity_unchanged": True,
    }


def budget_map(roster: Mapping[str, Any]) -> dict[tuple[int, int], int]:
    return {
        (row["case_id"], pool["pool"]): pool["checksum"]
        for row in roster["canonical_algorithm_budgets"]
        for pool in row["pool_checksums"]
    }


def compute_roster_checksum(roster: Mapping[str, Any]) -> int:
    entries = sorted(budget_map(roster).items())
    configuration = roster["configuration"]
    supersedes = roster["supersedes"]
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V3")
    hashed.u32(roster["schema_version"])
    hashed.string(roster["authority"])
    hashed.u32(roster["corpus_version"])
    hashed.u64(roster["corpus_checksum"])
    hashed.u32(roster["representative_manifest_schema_version"])
    hashed.u64(roster["representative_manifest_checksum"])
    hashed.u32(roster["workload_roster_manifest_schema_version"])
    hashed.u64(roster["workload_roster_manifest_checksum"])
    hashed.u32(supersedes["schema_version"])
    hashed.u64(supersedes["roster_checksum"])
    hashed.string(roster["configuration_authority"])
    hashed.u64(configuration["present_step_per_overuse_unit"])
    hashed.u64(configuration["superseded_history_step_per_overuse_unit"])
    hashed.u64(configuration["history_step_per_overuse_unit"])
    hashed.boolean(configuration["baseline_and_candidate_price_configs_equal"])
    hashed.boolean(configuration["all_other_canonical_algorithm_budget_fields_unchanged"])
    hashed.boolean(configuration["query_work_and_external_opportunity_unchanged"])
    hashed.u64(roster["cell_count"])
    for (case_id, pool), checksum in entries:
        hashed.u32(case_id)
        hashed.u32(pool)
        hashed.u64(checksum)
    return hashed.finish()


def validate_roster(path: pathlib.Path = _ROSTER) -> Mapping[str, Any]:
    representative, _ = authorities.validate_authorities()
    document = authorities._read(path, "confirmatory canonical budget roster v3")
    authorities._fields(document, _ROOT_FIELDS, "confirmatory canonical budget roster v3")
    if document["authority"] != _AUTHORITY:
        raise BudgetRosterV3Error("roster.authority differs from the frozen authority")
    if document["configuration_authority"] != _CONFIGURATION_AUTHORITY:
        raise BudgetRosterV3Error(
            "roster.configuration_authority differs from the frozen authority"
        )
    for field, expected in (
        ("schema_version", _SCHEMA_VERSION),
        ("corpus_version", _CORPUS_VERSION),
        (
            "representative_manifest_schema_version",
            _REPRESENTATIVE_MANIFEST_SCHEMA_VERSION,
        ),
        (
            "workload_roster_manifest_schema_version",
            _WORKLOAD_ROSTER_MANIFEST_SCHEMA_VERSION,
        ),
        ("cell_count", _CELL_COUNT),
    ):
        if authorities._u32(document[field], f"roster.{field}") != expected:
            raise BudgetRosterV3Error(f"roster.{field} differs from the frozen authority")
    for field, expected in (
        ("corpus_checksum", _CORPUS_CHECKSUM),
        ("representative_manifest_checksum", _REPRESENTATIVE_MANIFEST_CHECKSUM),
        ("workload_roster_manifest_checksum", _WORKLOAD_ROSTER_MANIFEST_CHECKSUM),
    ):
        if authorities._u64(document[field], f"roster.{field}") != expected:
            raise BudgetRosterV3Error(f"roster.{field} differs from the frozen authority")
    supersedes = document["supersedes"]
    configuration = document["configuration"]
    for value, fields, label in (
        (supersedes, _SUPERSEDES_FIELDS, "roster.supersedes"),
        (
            configuration,
            _CONFIGURATION_FIELDS,
            "roster.configuration",
        ),
    ):
        if not isinstance(value, dict):
            raise BudgetRosterV3Error(f"{label} must be an object")
        authorities._fields(value, fields, label)
    if (
        authorities._u32(supersedes["schema_version"], "roster.supersedes.schema_version")
        != _SUPERSEDED_ROSTER_SCHEMA_VERSION
        or authorities._u64(
            supersedes["roster_checksum"],
            "roster.supersedes.roster_checksum",
        )
        != _SUPERSEDED_ROSTER_CHECKSUM
    ):
        raise BudgetRosterV3Error("roster.supersedes differs from the frozen authority")
    expected_configuration = _expected_configuration()
    for field in (
        "present_step_per_overuse_unit",
        "superseded_history_step_per_overuse_unit",
        "history_step_per_overuse_unit",
    ):
        if (
            authorities._u64(configuration[field], f"roster.configuration.{field}")
            != expected_configuration[field]
        ):
            raise BudgetRosterV3Error("roster.configuration differs from the frozen authority")
    for field in (
        "baseline_and_candidate_price_configs_equal",
        "all_other_canonical_algorithm_budget_fields_unchanged",
        "query_work_and_external_opportunity_unchanged",
    ):
        if (
            not isinstance(configuration[field], bool)
            or configuration[field] is not expected_configuration[field]
        ):
            raise BudgetRosterV3Error("roster.configuration differs from the frozen authority")

    budgets = authorities._array(
        document["canonical_algorithm_budgets"],
        "roster.canonical_algorithm_budgets",
    )
    if len(budgets) != len(authorities._CASE_ROWS):
        raise BudgetRosterV3Error("roster must contain exactly 40 ordered budget rows")
    indexed: dict[tuple[int, int], int] = {}
    for index, (raw_budget, expected) in enumerate(
        zip(budgets, authorities._CASE_ROWS, strict=True)
    ):
        label = f"roster.canonical_algorithm_budgets[{index}]"
        if not isinstance(raw_budget, dict):
            raise BudgetRosterV3Error(f"{label} must be an object")
        authorities._fields(raw_budget, authorities._BUDGET_FIELDS, label)
        case_id, pools, _ = expected
        if authorities._u32(raw_budget["case_id"], f"{label}.case_id") != case_id:
            raise BudgetRosterV3Error(f"{label} has the wrong case")
        raw_pools = authorities._array(
            raw_budget["pool_checksums"],
            f"{label}.pool_checksums",
        )
        if len(raw_pools) != len(pools):
            raise BudgetRosterV3Error(f"{label} has the wrong pool count")
        for pool_index, (raw_pool, expected_pool) in enumerate(zip(raw_pools, pools, strict=True)):
            pool_label = f"{label}.pool_checksums[{pool_index}]"
            if not isinstance(raw_pool, dict):
                raise BudgetRosterV3Error(f"{pool_label} must be an object")
            authorities._fields(raw_pool, authorities._POOL_FIELDS, pool_label)
            pool = authorities._u32(raw_pool["pool"], f"{pool_label}.pool")
            checksum = authorities._u64(raw_pool["checksum"], f"{pool_label}.checksum")
            if pool != expected_pool or checksum == 0:
                raise BudgetRosterV3Error(f"{pool_label} differs from the frozen cell roster")
            indexed[(case_id, pool)] = checksum

    prior_budgets = {
        (row["case_id"], pool["pool"]): pool["checksum"]
        for row in representative["canonical_algorithm_budgets"]
        for pool in row["pool_checksums"]
    }
    if (
        len(indexed) != _CELL_COUNT
        or set(indexed) != set(prior_budgets)
        or any(indexed[cell] == prior_budgets[cell] for cell in indexed)
    ):
        raise BudgetRosterV3Error(
            "roster must replace all 102 and only the Manifest-v2 canonical budget cells"
        )
    checksum = authorities._u64(document["roster_checksum"], "roster.roster_checksum")
    if checksum != _ROSTER_CHECKSUM:
        raise BudgetRosterV3Error("roster checksum differs from the frozen v3 authority")
    if checksum != compute_roster_checksum(document):
        raise BudgetRosterV3Error("roster checksum does not authenticate its complete preimage")
    return document


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roster", nargs="?", type=pathlib.Path, default=_ROSTER)
    args = parser.parse_args(argv)
    try:
        validate_roster(args.roster)
    except (BudgetRosterV3Error, authorities.AuthorityError) as error:
        print(f"phase4 H=4096 budget-roster validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 Corpus-v2 H=4096 canonical budget roster v3 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
