"""Strict validator and frozen arithmetic for Phase 4 decision protocol v1."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys
from collections.abc import Mapping, Sequence
from fractions import Fraction
from typing import Any

from tools import validate_phase4_workload_net_roster_manifest as roster_validator
from tools.validate_phase4_raw_evidence import StableHashBuilder

_ROOT = pathlib.Path(__file__).resolve().parent.parent
_PROTOCOL = _ROOT / "schemas/benchmark/phase4_statistical_decision_protocol_v1.json"
_MANIFEST = _ROOT / "schemas/benchmark/phase4_representative_manifest_v1.json"
_ROSTER_MANIFEST = _ROOT / "schemas/benchmark/phase4_workload_net_roster_manifest_v1.json"
_MAX_BYTES = 256 * 1024
_MAX_DEPTH = 64
_CORPUS_CHECKSUM = 7311872938254494931
_SUCCESS_CASES = (
    (100, 8019315640326555851, (4,)),
    (101, 6282934470436762953, (4,)),
    (102, 15237650574036737121, (4,)),
    (200, 7606926012288823053, (4, 8, 16)),
    (201, 6785363568674608751, (4, 8, 16)),
    (210, 16619045146680576462, (4, 8, 16)),
    (211, 4307496802892609496, (4, 8, 16)),
    (220, 2412286233731448079, (4, 8, 16)),
    (221, 7711941648473078637, (4, 8, 16)),
    (1000, 15168059261654682115, (4, 8, 16)),
    (1001, 17982047200241829329, (4, 8, 16)),
    (1002, 12842072968640087763, (4, 8, 16)),
    (1003, 748221261688208573, (4, 8, 16)),
    (1004, 7809530797303947283, (4, 8, 16)),
    (1005, 11369748036396858849, (4, 8, 16)),
    (1006, 16382179578884687203, (4, 8, 16)),
    (1007, 5578087683802520485, (4, 8, 16)),
    (1100, 17064449010472703203, (4, 8, 16)),
    (1101, 11347823812296864429, (4, 8, 16)),
    (1102, 17509199292925069155, (4, 8, 16)),
    (1103, 10735519199761686321, (4, 8, 16)),
    (1104, 13840894844114907611, (4, 8, 16)),
    (1105, 18428681881191374613, (4, 8, 16)),
    (1106, 13539415826061509211, (4, 8, 16)),
    (1107, 3263168350504626049, (4, 8, 16)),
    (1200, 11391728102114583562, (4, 8, 16)),
    (1201, 9940811621395116444, (4, 8, 16)),
    (1202, 1270559019391015762, (4, 8, 16)),
    (1203, 11535097256105186392, (4, 8, 16)),
    (1204, 14902389270997626626, (4, 8, 16)),
    (1205, 12200879459693019140, (4, 8, 16)),
    (1206, 7266717595463850122, (4, 8, 16)),
    (1207, 1743725769046491000, (4, 8, 16)),
    (2002, 9392411121646056775, (4,)),
    (2003, 10053408626838512557, (8,)),
    (2004, 10180029229858426087, (16,)),
    (3000, 3990076980468972638, (4,)),
    (4000, 7587879808058813459, (4, 8, 16)),
)
_FAMILIES = (
    (0, "portal_channels_v1", tuple(range(1000, 1008)), False),
    (1, "pin_field_crossbar_v1", tuple(range(1100, 1108)), True),
    (2, "fragmented_maze_v1", tuple(range(1200, 1208)), False),
)
_ROLE_COUNTS = (
    ("exact", 3),
    ("calibration", 18),
    ("heldout", 72),
    ("fixed_query", 5),
    ("stress", 3),
    ("imported", 3),
)


class ProtocolError(ValueError):
    """Stable malformed-protocol diagnostic."""


def _reject_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_constant(value: str) -> None:
    raise ProtocolError(f"non-finite JSON number: {value}")


def _check_depth(root: Any) -> None:
    stack = [(root, 1)]
    while stack:
        value, depth = stack.pop()
        if depth > _MAX_DEPTH:
            raise ProtocolError("JSON nesting exceeds 64 levels")
        if isinstance(value, dict):
            stack.extend((child, depth + 1) for child in value.values())
        elif isinstance(value, list):
            stack.extend((child, depth + 1) for child in value)


def _canonical(value: Mapping[str, Any]) -> str:
    return json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def _artifact_checksum(value: Mapping[str, Any]) -> int:
    payload = {key: item for key, item in value.items() if key != "artifact_checksum"}
    hashed = StableHashBuilder()
    hashed.string("APGAR-PHASE4-STATISTICAL-DECISION-PROTOCOL-V1")
    hashed.string(_canonical(payload))
    return hashed.finish()


def _role(case_id: int) -> str:
    if 100 <= case_id <= 102:
        return "exact"
    if case_id in {200, 201, 210, 211, 220, 221}:
        return "calibration"
    if 1000 <= case_id <= 1207:
        return "heldout"
    if 2000 <= case_id <= 2004:
        return "fixed_query"
    if 3000 <= case_id <= 3002:
        return "stress"
    if case_id == 4000:
        return "imported"
    raise ProtocolError(f"unknown protocol case {case_id}")


def expanded_cells() -> list[tuple[int, int, str, str]]:
    """Return canonical (case, pool, role, evidence-kind) expansion."""
    rows = [
        (case, pool, _role(case), "raw_success")
        for case, _, pools in _SUCCESS_CASES
        for pool in pools
    ]
    rows.extend(
        (
            (2000, 1024, "fixed_query", "descriptor_only_excluded"),
            (2001, 1, "fixed_query", "descriptor_only_excluded"),
            (3001, 4, "stress", "compiled_work_bound"),
            (3002, 4, "stress", "compiled_work_bound"),
        )
    )
    return sorted(rows)


def _cell_groups() -> list[dict[str, Any]]:
    diagnostic = [
        "phase4_raw_evidence_v1",
        "phase4_per_net_report_publication_join_v1",
        "phase4_operational_projection_v1",
    ]
    decision = diagnostic + ["phase4_same_run_decision_telemetry_v1"]
    return [
        {
            "group": "exact",
            "case_ids": [100, 101, 102],
            "pools": [4],
            "expansion": "cartesian",
            "evidence_requirement": "raw_success",
            "decision_use": "exact_small_oracle",
            "required_artifacts": decision + ["phase4_exact_small_oracle_v1"],
            "in_noncalibration_closure": True,
        },
        {
            "group": "calibration",
            "case_ids": [200, 201, 210, 211, 220, 221],
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "raw_success",
            "decision_use": "calibration_only",
            "required_artifacts": diagnostic,
            "in_noncalibration_closure": False,
        },
        {
            "group": "heldout",
            "case_ids": list(range(1000, 1008)) + list(range(1100, 1108)) + list(range(1200, 1208)),
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "raw_success",
            "decision_use": "primary_inference_and_guardrails",
            "required_artifacts": decision,
            "in_noncalibration_closure": True,
        },
        {
            "group": "fixed_query_excluded",
            "case_pool_cells": [[2000, 1024], [2001, 1]],
            "expansion": "explicit",
            "evidence_requirement": "descriptor_only_excluded",
            "decision_use": "fixed_query_control",
            "required_artifacts": ["phase4_fixed_query_control_v1"],
            "in_noncalibration_closure": True,
        },
        {
            "group": "fixed_query_raw",
            "case_pool_cells": [[2002, 4], [2003, 8], [2004, 16]],
            "expansion": "explicit",
            "evidence_requirement": "raw_success",
            "decision_use": "fixed_query_control",
            "required_artifacts": diagnostic + ["phase4_fixed_query_control_v1"],
            "in_noncalibration_closure": True,
        },
        {
            "group": "stress_raw",
            "case_pool_cells": [[3000, 4]],
            "expansion": "explicit",
            "evidence_requirement": "raw_success",
            "decision_use": "stress_scalability",
            "required_artifacts": diagnostic + ["phase4_stress_evidence_v1"],
            "in_noncalibration_closure": True,
        },
        {
            "group": "stress_work_bound",
            "case_pool_cells": [[3001, 4], [3002, 4]],
            "expansion": "explicit",
            "evidence_requirement": "compiled_work_bound",
            "decision_use": "stress_scalability",
            "required_artifacts": ["phase4_stress_evidence_v1"],
            "in_noncalibration_closure": True,
        },
        {
            "group": "imported",
            "case_ids": [4000],
            "pools": [4, 8, 16],
            "expansion": "cartesian",
            "evidence_requirement": "raw_success",
            "decision_use": "imported_veto",
            "required_artifacts": decision,
            "in_noncalibration_closure": True,
        },
    ]


def _expand_groups(groups: Sequence[Mapping[str, Any]]) -> list[tuple[int, int, str, str]]:
    rows: list[tuple[int, int, str, str]] = []
    role_by_group = {
        "fixed_query_excluded": "fixed_query",
        "fixed_query_raw": "fixed_query",
        "stress_raw": "stress",
        "stress_work_bound": "stress",
    }
    for group in groups:
        role = role_by_group.get(group["group"], group["group"])
        if group["expansion"] == "cartesian":
            cells = ((case, pool) for case in group["case_ids"] for pool in group["pools"])
        elif group["expansion"] == "explicit":
            cells = (tuple(cell) for cell in group["case_pool_cells"])
        else:
            raise ProtocolError("unknown cell-group expansion")
        rows.extend((case, pool, role, group["evidence_requirement"]) for case, pool in cells)
    return sorted(rows)


def expected_protocol() -> dict[str, Any]:
    value: dict[str, Any] = {
        "schema_version": 1,
        "protocol_state": {"protocol_frozen": True, "decision_not_evaluated": True},
        "manifest_binding": {
            "schema_version": 1,
            "corpus_checksum": _CORPUS_CHECKSUM,
            "successful_cases": [
                {"case_id": case, "descriptor_fingerprint": fingerprint, "pools": list(pools)}
                for case, fingerprint, pools in _SUCCESS_CASES
            ],
            "excluded_fixed_query_cases": [
                {"case_id": 2000, "descriptor_fingerprint": 8203613321943675931, "pool": 1024},
                {"case_id": 2001, "descriptor_fingerprint": 11000562598404360444, "pool": 1},
            ],
            "work_bound_stress_cases": [
                {"case_id": 3001, "descriptor_fingerprint": 5505549972392664092, "pool": 4},
                {"case_id": 3002, "descriptor_fingerprint": 6801270323093200014, "pool": 4},
            ],
        },
        "matrix": {
            "logical_cell_count": 104,
            "noncalibration_closure_cell_count": 86,
            "raw_success_eligible_cell_count": 100,
            "noncalibration_raw_success_eligible_cell_count": 82,
            "role_cell_counts": [{"role": role, "count": count} for role, count in _ROLE_COUNTS],
            "cell_groups": _cell_groups(),
        },
        "families": [
            {
                "family_id": family,
                "name": name,
                "heldout_case_ids": list(cases),
                "globally_coupled_conflict_graph": coupled,
            }
            for family, name, cases, coupled in _FAMILIES
        ],
        "outcome": {
            "comparison": "lexicographic",
            "fields": [
                {"name": "selected_net_count", "direction": "maximize"},
                {"name": "total_overuse_units", "direction": "minimize"},
                {"name": "total_intrinsic_cost", "direction": "minimize"},
            ],
        },
        "inference": {
            "experimental_unit": "one_of_eight_heldout_cases_per_family",
            "primary_pool": 8,
            "sensitivity_pools": [4, 16],
            "sensitivity_can_rescue": False,
            "repetitions": 20,
            "repetitions_are_samples": False,
            "pool_strata_are_samples": False,
            "test": "one_sided_exact_sign_candidate_wins_greater_than_losses_ties_discarded",
            "sign_p_value": "exact_reduced_rational",
            "family_ids": [0, 1, 2],
            "multiplicity": "holm_bonferroni",
            "alpha": {"numerator": 1, "denominator": 20},
            "holm_tie_break": "family_id_ascending",
            "minimum_qualifying_primary_families": 2,
            "required_qualifying_family_id": 1,
            "publish": ["wins", "losses", "ties", "tie_bounded_win_fraction"],
            "tie_bounded_win_fraction": {
                "denominator": "eight_heldout_cases",
                "lower_numerator": "wins",
                "upper_numerator": "wins_plus_ties",
                "endpoints": "inclusive",
                "rounding": "none_exact_rational",
            },
            "clopper_pearson": {
                "sides": 2,
                "confidence_numerator": 19,
                "confidence_denominator": 20,
                "population": "non_tie_win_probability",
                "rounding": "outward",
                "scale": "parts_per_billion",
                "zero_non_ties": "tagged_unavailable",
            },
            "pooling": "forbidden",
            "missing_or_failed_evidence": "incomplete_never_loss",
        },
        "guardrails": {
            "heldout_all_pools_selected_net_regressions": 0,
            "imported_all_pools_selected_net_regressions": 0,
            "imported_each_pool_overuse_must_not_exceed_baseline": True,
            "imported_each_pool_mean_selected_intrinsic_cost_max_ratio": {
                "numerator": 11,
                "denominator": 10,
            },
            "baseline_zero_selected_mean_rule": (
                "not_applicable_if_candidate_selects_more_otherwise_both_costs_zero"
            ),
            "equal_selected_and_overuse_total_intrinsic_cost_must_not_exceed_baseline": True,
            "exact_validation_rejection_columns_per_arm": 0,
            "exact_rejection_scope_groups": ["exact", "heldout", "imported"],
            "exact_rejection_authority": "future_same_run_decision_telemetry_only",
            "diagnostic_rerun_can_decide": False,
            "pooling_offsets": "forbidden",
        },
        "timing": {
            "decision_role": "diagnostic_only",
            "primary_scope": "parent_outer_elapsed_nanoseconds",
            "scoped_diagnostics": ["prepared_elapsed_nanoseconds", "cold_elapsed_nanoseconds"],
            "retain_repetitions": 20,
            "outlier_deletion": False,
            "per_pair": [
                "candidate_minus_baseline_nanoseconds",
                "candidate_over_baseline_ratio_ppm_round_half_up",
            ],
            "zero_denominator": "forbidden",
            "overall_median": "round_half_up_of_middle_two_ratio_ppm",
            "median_ci_order_statistics_one_based": [6, 15],
            "median_ci_coverage_ppm": 958611,
            "order_strata": ["AB", "BA"],
            "publish_order_stratum_medians": True,
            "direction_claim_when_strata_disagree": False,
        },
        "completion_requirements": [
            "exact_small_exhaustive_oracle",
            "fixed_query_controls",
            "stress_ladder",
            "same_run_decision_telemetry",
            "complete_stage_timing",
            "cpu_gpu_utilization_context",
            "compatible_batch_fill",
            "prepared_view_cache_behavior",
            "complete_toolchain_hardware_provenance",
        ],
        "artifact_checksum": 0,
    }
    value["artifact_checksum"] = _artifact_checksum(value)
    return value


def _cross_validate_manifest(
    representative_path: pathlib.Path = _MANIFEST,
    roster_path: pathlib.Path = _ROSTER_MANIFEST,
) -> None:
    try:
        manifest = json.loads(
            representative_path.read_text(encoding="utf-8"),
            object_pairs_hook=_reject_pairs,
            parse_constant=_reject_constant,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProtocolError(f"cannot read representative manifest: {error}") from error
    if (
        not isinstance(manifest, dict)
        or manifest.get("schema_version") != 1
        or manifest.get("corpus_checksum") != _CORPUS_CHECKSUM
    ):
        raise ProtocolError("representative manifest identity differs from frozen protocol")
    actual = tuple(
        (row["case_id"], row["descriptor_fingerprint"], tuple(row["requested_pool_sizes"]))
        for row in manifest["cases"]
        if row["build_status"] == "success"
    )
    failed = tuple(
        (
            row["case_id"],
            row["descriptor_fingerprint"],
            tuple(row["requested_pool_sizes"]),
            row["build_status"],
        )
        for row in manifest["cases"]
        if row["build_status"] != "success"
    )
    if actual != _SUCCESS_CASES or failed != (
        (3001, 5505549972392664092, (4,), "compiled_work_bound"),
        (3002, 6801270323093200014, (4,), "compiled_work_bound"),
    ):
        raise ProtocolError("representative manifest roster differs from frozen protocol")
    try:
        roster = roster_validator.validate(roster_path, representative_path)
    except roster_validator.ManifestError as error:
        raise ProtocolError(
            f"workload-net roster manifest differs from frozen protocol: {error}"
        ) from error
    exclusions = tuple(
        (row["case_id"], row["descriptor_fingerprint"], row["disposition"])
        for row in roster["excluded_cases"]
    )
    if exclusions != (
        (2000, 8203613321943675931, "descriptor_only_unsupported_pool"),
        (2001, 11000562598404360444, "descriptor_only_unsupported_pool"),
        (3001, 5505549972392664092, "compiled_work_bound"),
        (3002, 6801270323093200014, "compiled_work_bound"),
    ):
        raise ProtocolError("workload-net roster exclusions differ from frozen protocol")


def validate_document(value: Any) -> Mapping[str, Any]:
    _cross_validate_manifest()
    if not isinstance(value, dict):
        raise ProtocolError("protocol must be a JSON object")
    _check_depth(value)
    expected = expected_protocol()
    if not _exact_equal(value, expected):
        raise ProtocolError("protocol does not exactly reconstruct the frozen v1 literal")
    if value["artifact_checksum"] != _artifact_checksum(value):
        raise ProtocolError("artifact_checksum does not authenticate protocol")
    cells = expanded_cells()
    if len(cells) != 104 or len(set(cells)) != 104:
        raise ProtocolError("protocol expansion is not 104 unique logical cells")
    if sum(row[3] == "raw_success" for row in cells) != 100:
        raise ProtocolError("protocol expansion is not 100 Raw-success cells")
    if sum(row[2] != "calibration" for row in cells) != 86:
        raise ProtocolError("protocol expansion is not 86 closure cells")
    if _expand_groups(value["matrix"]["cell_groups"]) != cells:
        raise ProtocolError("checksummed cell groups do not expand to the canonical roster")
    return value


def _exact_equal(actual: Any, expected: Any) -> bool:
    if type(actual) is not type(expected):
        return False
    if isinstance(expected, dict):
        return tuple(actual) == tuple(expected) and all(
            _exact_equal(actual[key], expected[key]) for key in expected
        )
    if isinstance(expected, list):
        return len(actual) == len(expected) and all(
            _exact_equal(left, right) for left, right in zip(actual, expected, strict=True)
        )
    return actual == expected


def read_protocol(path: pathlib.Path = _PROTOCOL) -> Mapping[str, Any]:
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ProtocolError(f"cannot read protocol: {error}") from error
    if len(data) > _MAX_BYTES:
        raise ProtocolError("protocol exceeds 256 KiB")
    if data.startswith(b"\xef\xbb\xbf") or not data.endswith(b"\n") or data.endswith(b"\n\n"):
        raise ProtocolError("protocol must be UTF-8 without BOM and end in one LF")
    try:
        text = data.decode("utf-8")
        value = json.loads(text, object_pairs_hook=_reject_pairs, parse_constant=_reject_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ProtocolError(f"invalid protocol JSON: {error}") from error
    validated = validate_document(value)
    if text != _canonical(validated) + "\n":
        raise ProtocolError("protocol is not canonical compact JSON")
    return validated


def compare_outcomes(candidate: Mapping[str, int], baseline: Mapping[str, int]) -> int:
    """Return 1/0/-1 for the frozen candidate win/tie/loss relation."""
    fields = (("selected_net_count", 1), ("total_overuse_units", -1), ("total_intrinsic_cost", -1))
    for field, direction in fields:
        left, right = candidate[field], baseline[field]
        if (
            isinstance(left, bool)
            or isinstance(right, bool)
            or not isinstance(left, int)
            or not isinstance(right, int)
            or left < 0
            or right < 0
        ):
            raise ProtocolError(f"{field} must be a nonnegative integer")
        if left != right:
            return direction if left > right else -direction
    return 0


def exact_sign_p(wins: int, losses: int) -> Fraction:
    if (
        isinstance(wins, bool)
        or isinstance(losses, bool)
        or not isinstance(wins, int)
        or not isinstance(losses, int)
        or wins < 0
        or losses < 0
    ):
        raise ProtocolError("sign-test counts must be nonnegative integers")
    n = wins + losses
    if n == 0:
        return Fraction(1, 1)
    return Fraction(sum(math.comb(n, k) for k in range(wins, n + 1)), 1 << n)


def holm_qualifying_families(counts: Mapping[int, tuple[int, int, int]]) -> tuple[int, ...]:
    if set(counts) != {0, 1, 2}:
        raise ProtocolError("Holm input must contain family IDs 0, 1, and 2")
    for family, values in counts.items():
        if (
            len(values) != 3
            or any(
                isinstance(value, bool) or not isinstance(value, int) or value < 0
                for value in values
            )
            or sum(values) != 8
        ):
            raise ProtocolError(f"family {family} must have exactly eight held-out units")
    ordered = sorted((exact_sign_p(counts[f][0], counts[f][1]), f) for f in counts)
    qualified: list[int] = []
    for rank, (p_value, family) in enumerate(ordered):
        threshold = Fraction(1, 20 * (3 - rank))
        if p_value > threshold:
            break
        qualified.append(family)
    return tuple(sorted(qualified))


def tie_bounded_win_fraction(wins: int, losses: int, ties: int) -> tuple[Fraction, Fraction]:
    values = (wins, losses, ties)
    if (
        any(isinstance(value, bool) or not isinstance(value, int) or value < 0 for value in values)
        or sum(values) != 8
    ):
        raise ProtocolError("tie-bounded fraction requires exactly eight held-out units")
    return Fraction(wins, 8), Fraction(wins + ties, 8)


def clopper_pearson_ppb(wins: int, losses: int) -> Mapping[str, Any]:
    """Return tightly outward-rounded exact 95% CP bounds or tagged unavailability."""
    if (
        isinstance(wins, bool)
        or isinstance(losses, bool)
        or not isinstance(wins, int)
        or not isinstance(losses, int)
        or wins < 0
        or losses < 0
        or wins + losses > 8
    ):
        raise ProtocolError(
            "Clopper-Pearson counts must be nonnegative integers totaling at most 8"
        )
    n = wins + losses
    if n == 0:
        return {"status": "unavailable", "reason": "zero_non_ties"}

    scale = 1_000_000_000
    common_denominator = scale**n

    def upper_tail(numerator: int) -> int:
        complement = scale - numerator
        return sum(
            math.comb(n, k) * numerator**k * complement ** (n - k) for k in range(wins, n + 1)
        )

    def lower_tail(numerator: int) -> int:
        complement = scale - numerator
        return sum(math.comb(n, k) * numerator**k * complement ** (n - k) for k in range(wins + 1))

    lower = 0
    if wins != 0:
        low, high = 0, scale
        while low < high:
            middle = (low + high + 1) // 2
            if 40 * upper_tail(middle) <= common_denominator:
                low = middle
            else:
                high = middle - 1
        lower = low

    upper = scale
    if wins != n:
        low, high = 0, scale
        while low < high:
            middle = (low + high) // 2
            if 40 * lower_tail(middle) <= common_denominator:
                high = middle
            else:
                low = middle + 1
        upper = low
    return {"status": "available", "lower_ppb": lower, "upper_ppb": upper}


def timing_summary(rows: Sequence[Mapping[str, Any]]) -> Mapping[str, Any]:
    if len(rows) != 20:
        raise ProtocolError("timing summary requires exactly 20 repetitions")
    ratios: list[int] = []
    strata: dict[str, list[int]] = {"AB": [], "BA": []}
    differences: list[int] = []
    for expected_repetition, row in enumerate(rows):
        repetition, order = row["repetition"], row["order"]
        baseline, candidate = row["baseline_outer_ns"], row["candidate_outer_ns"]
        if (
            isinstance(repetition, bool)
            or not isinstance(repetition, int)
            or repetition != expected_repetition
        ):
            raise ProtocolError("timing repetitions must be in canonical 0..19 order")
        expected_order = "AB" if repetition % 2 == 0 else "BA"
        if (
            order != expected_order
            or isinstance(baseline, bool)
            or isinstance(candidate, bool)
            or not isinstance(baseline, int)
            or not isinstance(candidate, int)
            or baseline <= 0
            or candidate <= 0
        ):
            raise ProtocolError("timing row has invalid order or elapsed time")
        ratio = (2 * candidate * 1_000_000 + baseline) // (2 * baseline)
        ratios.append(ratio)
        strata[order].append(ratio)
        differences.append(candidate - baseline)
    if any(len(values) != 10 for values in strata.values()):
        raise ProtocolError("timing requires ten AB and ten BA rows")

    def median(values: Sequence[int]) -> int:
        ordered = sorted(values)
        return (ordered[len(ordered) // 2 - 1] + ordered[len(ordered) // 2] + 1) // 2

    ordered = sorted(ratios)
    ab, ba = median(strata["AB"]), median(strata["BA"])
    return {
        "differences_ns": differences,
        "ratios_ppm": ratios,
        "median_ratio_ppm": median(ratios),
        "median_ci_ppm": [ordered[5], ordered[14]],
        "ab_median_ratio_ppm": ab,
        "ba_median_ratio_ppm": ba,
        "direction_claim_allowed": (ab - 1_000_000) * (ba - 1_000_000) > 0,
    }


def guardrail_cell(
    candidate: Mapping[str, int], baseline: Mapping[str, int], *, imported: bool
) -> bool:
    """Apply the frozen per-cell non-regression guardrails without pooling."""
    compare_outcomes(candidate, baseline)  # validates the three fields
    cs, bs = candidate["selected_net_count"], baseline["selected_net_count"]
    co, bo = candidate["total_overuse_units"], baseline["total_overuse_units"]
    cc, bc = candidate["total_intrinsic_cost"], baseline["total_intrinsic_cost"]
    if cs < bs or (cs == bs and co == bo and cc > bc):
        return False
    if imported:
        if co > bo:
            return False
        if bs == 0:
            if cs == 0:
                return bc == 0 and cc == 0
            return bc == 0
        if cs == 0 or 10 * cc * bs > 11 * bc * cs:
            return False
    return True


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("protocol", nargs="?", type=pathlib.Path, default=_PROTOCOL)
    args = parser.parse_args(argv)
    try:
        read_protocol(args.protocol)
    except ProtocolError as error:
        print(f"phase4 statistical protocol validation failed: {error}", file=sys.stderr)
        return 1
    print("phase4 statistical decision protocol v1 validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
