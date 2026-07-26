"""Strict validator for the Phase 4 Corpus-v2 H=4096 Session-v5 budget roster."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as roster_v3
from tools import validate_phase4_representative_manifest_v2 as authorities
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_ROSTER = _ROOT / "schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v4.json"
_SCHEMA_VERSION = 4
_AUTHORITY = "phase4_confirmatory_canonical_algorithm_budget_roster_v4"
_CONFIGURATION_AUTHORITY = "phase4_confirmatory_corpus_v2_h4096_session_v5"
_CORPUS_VERSION = 2
_CORPUS_CHECKSUM = 4182833841936446798
_REPRESENTATIVE_MANIFEST_SCHEMA_VERSION = 2
_REPRESENTATIVE_MANIFEST_CHECKSUM = 9613362670139358355
_WORKLOAD_ROSTER_MANIFEST_SCHEMA_VERSION = 2
_WORKLOAD_ROSTER_MANIFEST_CHECKSUM = 14986327048461036142
_SUPERSEDED_ROSTER_SCHEMA_VERSION = 3
_SUPERSEDED_ROSTER_AUTHORITY = "phase4_confirmatory_canonical_algorithm_budget_roster_v3"
_SUPERSEDED_ROSTER_CHECKSUM = 18429170436700418962
_SUPERSEDED_CONFIGURATION_AUTHORITY = "phase4_confirmatory_corpus_v2_h4096"
_ROSTER_CHECKSUM = 12316700735749461907
_ROW_COUNT = 40
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
_SUPERSEDES_FIELDS = (
    "schema_version",
    "authority",
    "roster_checksum",
    "configuration_authority",
)
_CONFIGURATION_FIELDS = (
    "present_step_per_overuse_unit",
    "history_step_per_overuse_unit",
    "baseline_and_candidate_price_configs_equal",
    "superseded_candidate_allocation_session_schema_version",
    "candidate_allocation_session_schema_version",
    "superseded_targeted_regeneration_plan_schema_version",
    "targeted_regeneration_plan_schema_version",
    "superseded_targeted_regeneration_execution_schema_version",
    "targeted_regeneration_execution_schema_version",
    "all_non_session_canonical_algorithm_budget_fields_unchanged",
    "all_candidate_session_fields_except_schema_version_unchanged",
    "query_work_and_external_opportunity_unchanged",
)


class BudgetRosterV4Error(ValueError):
    """Stable malformed-budget-roster diagnostic."""


def _expected_configuration() -> dict[str, Any]:
    return {
        "present_step_per_overuse_unit": 1,
        "history_step_per_overuse_unit": 4096,
        "baseline_and_candidate_price_configs_equal": True,
        "superseded_candidate_allocation_session_schema_version": 4,
        "candidate_allocation_session_schema_version": 5,
        "superseded_targeted_regeneration_plan_schema_version": 2,
        "targeted_regeneration_plan_schema_version": 3,
        "superseded_targeted_regeneration_execution_schema_version": 5,
        "targeted_regeneration_execution_schema_version": 6,
        "all_non_session_canonical_algorithm_budget_fields_unchanged": True,
        "all_candidate_session_fields_except_schema_version_unchanged": True,
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
    hashed.string("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V4")
    hashed.u32(roster["schema_version"])
    hashed.string(roster["authority"])
    hashed.u32(roster["corpus_version"])
    hashed.u64(roster["corpus_checksum"])
    hashed.u32(roster["representative_manifest_schema_version"])
    hashed.u64(roster["representative_manifest_checksum"])
    hashed.u32(roster["workload_roster_manifest_schema_version"])
    hashed.u64(roster["workload_roster_manifest_checksum"])
    hashed.u32(supersedes["schema_version"])
    hashed.string(supersedes["authority"])
    hashed.u64(supersedes["roster_checksum"])
    hashed.string(supersedes["configuration_authority"])
    hashed.string(roster["configuration_authority"])
    hashed.u64(configuration["present_step_per_overuse_unit"])
    hashed.u64(configuration["history_step_per_overuse_unit"])
    hashed.boolean(configuration["baseline_and_candidate_price_configs_equal"])
    hashed.u32(configuration["superseded_candidate_allocation_session_schema_version"])
    hashed.u32(configuration["candidate_allocation_session_schema_version"])
    hashed.u32(configuration["superseded_targeted_regeneration_plan_schema_version"])
    hashed.u32(configuration["targeted_regeneration_plan_schema_version"])
    hashed.u32(configuration["superseded_targeted_regeneration_execution_schema_version"])
    hashed.u32(configuration["targeted_regeneration_execution_schema_version"])
    hashed.boolean(configuration["all_non_session_canonical_algorithm_budget_fields_unchanged"])
    hashed.boolean(configuration["all_candidate_session_fields_except_schema_version_unchanged"])
    hashed.boolean(configuration["query_work_and_external_opportunity_unchanged"])
    hashed.u64(roster["cell_count"])
    for (case_id, pool), checksum in entries:
        hashed.u32(case_id)
        hashed.u32(pool)
        hashed.u64(checksum)
    return hashed.finish()


def _validate_fixed_authorities(document: Mapping[str, Any]) -> None:
    authority = authorities._string(document["authority"], "roster.authority")
    if authority != _AUTHORITY:
        raise BudgetRosterV4Error("roster.authority differs from the frozen authority")
    configuration_authority = authorities._string(
        document["configuration_authority"],
        "roster.configuration_authority",
    )
    if configuration_authority != _CONFIGURATION_AUTHORITY:
        raise BudgetRosterV4Error(
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
            raise BudgetRosterV4Error(f"roster.{field} differs from the frozen authority")
    for field, expected in (
        ("corpus_checksum", _CORPUS_CHECKSUM),
        ("representative_manifest_checksum", _REPRESENTATIVE_MANIFEST_CHECKSUM),
        ("workload_roster_manifest_checksum", _WORKLOAD_ROSTER_MANIFEST_CHECKSUM),
    ):
        if authorities._u64(document[field], f"roster.{field}") != expected:
            raise BudgetRosterV4Error(f"roster.{field} differs from the frozen authority")


def _validate_transition(document: Mapping[str, Any]) -> None:
    supersedes = document["supersedes"]
    configuration = document["configuration"]
    for value, fields, label in (
        (supersedes, _SUPERSEDES_FIELDS, "roster.supersedes"),
        (configuration, _CONFIGURATION_FIELDS, "roster.configuration"),
    ):
        if not isinstance(value, dict):
            raise BudgetRosterV4Error(f"{label} must be an object")
        authorities._fields(value, fields, label)

    if (
        authorities._u32(supersedes["schema_version"], "roster.supersedes.schema_version")
        != _SUPERSEDED_ROSTER_SCHEMA_VERSION
        or authorities._string(
            supersedes["authority"],
            "roster.supersedes.authority",
        )
        != _SUPERSEDED_ROSTER_AUTHORITY
        or authorities._u64(
            supersedes["roster_checksum"],
            "roster.supersedes.roster_checksum",
        )
        != _SUPERSEDED_ROSTER_CHECKSUM
        or authorities._string(
            supersedes["configuration_authority"],
            "roster.supersedes.configuration_authority",
        )
        != _SUPERSEDED_CONFIGURATION_AUTHORITY
    ):
        raise BudgetRosterV4Error("roster.supersedes differs from the frozen v3 authority")

    expected = _expected_configuration()
    for field in (
        "present_step_per_overuse_unit",
        "history_step_per_overuse_unit",
    ):
        if (
            authorities._u64(configuration[field], f"roster.configuration.{field}")
            != expected[field]
        ):
            raise BudgetRosterV4Error("roster.configuration differs from the frozen transition")
    for field in (
        "superseded_candidate_allocation_session_schema_version",
        "candidate_allocation_session_schema_version",
        "superseded_targeted_regeneration_plan_schema_version",
        "targeted_regeneration_plan_schema_version",
        "superseded_targeted_regeneration_execution_schema_version",
        "targeted_regeneration_execution_schema_version",
    ):
        if (
            authorities._u32(configuration[field], f"roster.configuration.{field}")
            != expected[field]
        ):
            raise BudgetRosterV4Error("roster.configuration differs from the frozen transition")
    for field in (
        "baseline_and_candidate_price_configs_equal",
        "all_non_session_canonical_algorithm_budget_fields_unchanged",
        "all_candidate_session_fields_except_schema_version_unchanged",
        "query_work_and_external_opportunity_unchanged",
    ):
        if (
            not isinstance(configuration[field], bool)
            or configuration[field] is not expected[field]
        ):
            raise BudgetRosterV4Error("roster.configuration differs from the frozen transition")


def _validate_cells(
    document: Mapping[str, Any],
    predecessor: Mapping[str, Any],
) -> None:
    budgets = authorities._array(
        document["canonical_algorithm_budgets"],
        "roster.canonical_algorithm_budgets",
    )
    predecessor_budgets = predecessor["canonical_algorithm_budgets"]
    if len(budgets) != _ROW_COUNT or len(budgets) != len(predecessor_budgets):
        raise BudgetRosterV4Error("roster must contain exactly 40 ordered v3 budget rows")

    indexed: dict[tuple[int, int], int] = {}
    predecessor_indexed = roster_v3.budget_map(predecessor)
    for index, (raw_budget, predecessor_budget) in enumerate(
        zip(budgets, predecessor_budgets, strict=True)
    ):
        label = f"roster.canonical_algorithm_budgets[{index}]"
        if not isinstance(raw_budget, dict):
            raise BudgetRosterV4Error(f"{label} must be an object")
        authorities._fields(raw_budget, authorities._BUDGET_FIELDS, label)
        case_id = authorities._u32(raw_budget["case_id"], f"{label}.case_id")
        if case_id != predecessor_budget["case_id"]:
            raise BudgetRosterV4Error(f"{label} has the wrong predecessor case")
        raw_pools = authorities._array(
            raw_budget["pool_checksums"],
            f"{label}.pool_checksums",
        )
        predecessor_pools = predecessor_budget["pool_checksums"]
        if len(raw_pools) != len(predecessor_pools):
            raise BudgetRosterV4Error(f"{label} has the wrong predecessor pool count")
        for pool_index, (raw_pool, predecessor_pool) in enumerate(
            zip(raw_pools, predecessor_pools, strict=True)
        ):
            pool_label = f"{label}.pool_checksums[{pool_index}]"
            if not isinstance(raw_pool, dict):
                raise BudgetRosterV4Error(f"{pool_label} must be an object")
            authorities._fields(raw_pool, authorities._POOL_FIELDS, pool_label)
            pool = authorities._u32(raw_pool["pool"], f"{pool_label}.pool")
            checksum = authorities._u64(raw_pool["checksum"], f"{pool_label}.checksum")
            key = (case_id, pool)
            if pool != predecessor_pool["pool"] or checksum == 0:
                raise BudgetRosterV4Error(f"{pool_label} differs from the frozen v3 cell roster")
            if key in indexed:
                raise BudgetRosterV4Error(f"{pool_label} duplicates a canonical cell")
            if checksum == predecessor_pool["checksum"]:
                raise BudgetRosterV4Error(
                    f"{pool_label} reuses its Session-v4 predecessor checksum"
                )
            indexed[key] = checksum

    if (
        len(indexed) != _CELL_COUNT
        or set(indexed) != set(predecessor_indexed)
        or any(indexed[cell] == predecessor_indexed[cell] for cell in indexed)
    ):
        raise BudgetRosterV4Error(
            "roster must replace all 102 and only the roster-v3 canonical budget cells"
        )


def validate_roster(path: pathlib.Path = _ROSTER) -> Mapping[str, Any]:
    predecessor = roster_v3.validate_roster()
    document = authorities._read(path, "confirmatory canonical budget roster v4")
    authorities._fields(document, _ROOT_FIELDS, "confirmatory canonical budget roster v4")
    _validate_fixed_authorities(document)
    _validate_transition(document)
    _validate_cells(document, predecessor)

    checksum = authorities._u64(document["roster_checksum"], "roster.roster_checksum")
    if checksum != _ROSTER_CHECKSUM:
        raise BudgetRosterV4Error("roster checksum differs from the frozen v4 authority")
    if checksum != compute_roster_checksum(document):
        raise BudgetRosterV4Error("roster checksum does not authenticate its complete preimage")
    return document


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("roster", nargs="?", type=pathlib.Path, default=_ROSTER)
    args = parser.parse_args(argv)
    try:
        validate_roster(args.roster)
    except (
        BudgetRosterV4Error,
        roster_v3.BudgetRosterV3Error,
        authorities.AuthorityError,
    ) as error:
        print(
            f"phase4 H=4096 Session-v5 budget-roster validation failed: {error}",
            file=sys.stderr,
        )
        return 1
    print("phase4 Corpus-v2 H=4096 Session-v5 canonical budget roster v4 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
