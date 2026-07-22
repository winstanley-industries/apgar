"""Build and rebuild-validate the bounded Phase 4 fixed-query control artifact."""

from __future__ import annotations

import argparse
import copy
import json
import pathlib
import re
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import project_phase4_operational_evidence as operational_projector
from tools import validate_phase4_per_net_report as report_validator
from tools import validate_phase4_raw_evidence as raw_validator

_U64_MAX = (1 << 64) - 1
_MAXIMUM_ARTIFACT_BYTES = 4 * 1024 * 1024
_MAXIMUM_NESTING_DEPTH = 64
_COMMIT = re.compile(r"[0-9a-f]{40}")
_FIXED_INITIAL_QUERIES = 1024
_PER_QUERY_WORK_UNITS = 1_000_000_000
_FIXED_INITIAL_WORK_UNITS = _FIXED_INITIAL_QUERIES * _PER_QUERY_WORK_UNITS
_EXECUTED_CASES = ((2002, 256, 4), (2003, 128, 8), (2004, 64, 16))
_DESCRIPTOR_ONLY_CASES = (
    (2000, 1, 1024, 8203613321943675931),
    (2001, 1024, 1, 11000562598404360444),
)


class ControlError(ValueError):
    """A fixed-query authority or publication artifact is invalid."""


def _checked_product(left: int, right: int, label: str) -> int:
    result = left * right
    if result > _U64_MAX:
        raise ControlError(f"{label} exceeds uint64")
    return result


def _requested_pair_count(net_count: int, pool_size: int) -> int:
    per_net = _checked_product(pool_size, pool_size - 1, "candidate pair product") // 2
    return _checked_product(net_count, per_net, "requested within-net candidate pairs")


def _canonical_payload(document: Mapping[str, Any]) -> str:
    payload = {
        key: value
        for key, value in document.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    return json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":"))


def compute_artifact_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-FIXED-QUERY-CONTROL-ARTIFACT-V1")
    hashed.string(_canonical_payload(document))
    return hashed.finish()


def compute_source_envelope_checksum(document: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-FIXED-QUERY-CONTROL-SOURCE-ENVELOPE-V1")
    hashed.u32(document["schema_version"])
    hashed.string(document["source_commit"])
    hashed.boolean(document["source_stamped"])
    hashed.boolean(document["source_tree_dirty"])
    hashed.u64(document["artifact_checksum"])
    return hashed.finish()


def _manifest_case(case_id: int) -> Mapping[str, Any]:
    _, cases, _ = raw_validator._representative_manifest()
    try:
        return cases[case_id]
    except KeyError as error:
        raise ControlError(f"fixed-query case {case_id} is absent from the manifest") from error


def _validate_case_shape(case_id: int, net_count: int, pool_size: int) -> Mapping[str, Any]:
    manifest = _manifest_case(case_id)
    if (
        manifest["descriptor_fingerprint"] == 0
        or manifest["workload_net_count"] != net_count
        or manifest["requested_pool_sizes"] != [pool_size]
    ):
        raise ControlError(f"fixed-query case {case_id} differs from its frozen descriptor")
    if _checked_product(net_count, pool_size, "initial route queries") != _FIXED_INITIAL_QUERIES:
        raise ControlError(f"fixed-query case {case_id} does not have 1024 initial queries")
    return manifest


def _common_configuration(config: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "schema_version": config["schema_version"],
        "preparation_worker_count": config["preparation_worker_count"],
        "repetitions": config["repetitions"],
        "maximum_setup_elapsed_nanoseconds": config["maximum_setup_elapsed_nanoseconds"],
        "external_budget": copy.deepcopy(config["external_budget"]),
        "corpus_limits": copy.deepcopy(config["corpus_limits"]),
    }


def _sum_candidate_pairs(report: Mapping[str, Any], arm: int) -> int:
    per_net = report["arms"][arm]["diagnostic"]["telemetry"]["per_net"]
    result = sum(row["candidate_pair_count"] for row in per_net)
    if result > _U64_MAX:
        raise ControlError("observed within-net candidate pairs exceed uint64")
    return result


def _source_binding(
    raw: Mapping[str, Any], report: Mapping[str, Any], operational: Mapping[str, Any]
) -> dict[str, Any]:
    return {
        "raw_artifact_checksum": raw["artifact_checksum"],
        "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        "report_artifact_checksum": report["artifact_checksum"],
        "report_source_envelope_checksum": report["source_envelope_checksum"],
        "operational_artifact_checksum": operational["artifact_checksum"],
        "operational_source_envelope_checksum": operational["source_envelope_checksum"],
        "raw_cell_plan_checksum": raw["cell_plan_checksum"],
        "environment_checksum": raw["environment"]["environment_checksum"],
        "authority_run_identity": raw["authority_run_identity"],
        "controller_identity": raw["controller_identity"],
    }


def _executed_control(
    raw: Mapping[str, Any], report: Mapping[str, Any], operational: Mapping[str, Any]
) -> dict[str, Any]:
    config = raw["config"]
    case_id = config["case_id"]
    expected = {entry[0]: entry[1:] for entry in _EXECUTED_CASES}.get(case_id)
    if expected is None:
        raise ControlError(f"case {case_id} is not an executed fixed-query control")
    net_count, pool_size = expected
    manifest = _validate_case_shape(case_id, net_count, pool_size)
    if config["requested_pool_size"] != pool_size:
        raise ControlError(f"case {case_id} uses the wrong requested pool size")

    baseline = raw["attempts"][0]["baseline"]["record"]["semantics"]
    candidate = raw["attempts"][0]["candidate"]["record"]["semantics"]
    initial_queries = _checked_product(net_count, pool_size, "initial route queries")
    initial_work = _checked_product(initial_queries, _PER_QUERY_WORK_UNITS, "initial route work")
    whole_queries = _checked_product(net_count, pool_size + 2, "whole-trial route queries")
    whole_work = _checked_product(whole_queries, _PER_QUERY_WORK_UNITS, "whole-trial route work")
    for label, semantics in (("baseline", baseline), ("candidate", candidate)):
        if (
            semantics["case_id"] != case_id
            or semantics["workload_net_count"] != net_count
            or semantics["requested_pool_size"] != pool_size
            or semantics["opportunity"]["route_queries"] != whole_queries
            or semantics["opportunity"]["route_work_units"] != whole_work
        ):
            raise ControlError(f"case {case_id} {label} has a noncanonical query shape")
    if (
        candidate["preparation_route_queries"] > initial_queries
        or candidate["preparation_route_work_units"] > initial_work
        or candidate["preparation_route_work_units"]
        > candidate["preparation_route_queries"] * _PER_QUERY_WORK_UNITS
    ):
        raise ControlError(f"case {case_id} candidate preparation exceeds its fixed cap")

    return {
        "case_id": case_id,
        "descriptor_fingerprint": manifest["descriptor_fingerprint"],
        "workload_net_count": net_count,
        "requested_pool_size": pool_size,
        "evidence_status": "raw_report_operational_success",
        "initial_candidate_preparation": {
            "opportunity_route_queries": initial_queries,
            "opportunity_route_work_units": initial_work,
            "actual_route_queries": candidate["preparation_route_queries"],
            "actual_route_work_units": candidate["preparation_route_work_units"],
        },
        "diversity_accounting": {
            "scope": "sum_of_within_net_unordered_candidate_pairs",
            "requested_pool_pair_opportunity": _requested_pair_count(net_count, pool_size),
            "observed_baseline_final_pool_pairs": _sum_candidate_pairs(report, 0),
            "observed_candidate_final_pool_pairs": _sum_candidate_pairs(report, 1),
            "cross_net_candidate_pairs_included": False,
        },
        "whole_trial": {
            "equal_between_arms": True,
            "route_query_opportunity": whole_queries,
            "route_work_opportunity": whole_work,
            "baseline_actual_route_queries": baseline["actual"]["route_queries"],
            "baseline_actual_route_work_units": baseline["actual"]["route_work_units"],
            "candidate_actual_route_queries": candidate["actual"]["route_queries"],
            "candidate_actual_route_work_units": candidate["actual"]["route_work_units"],
        },
        "source_binding": _source_binding(raw, report, operational),
    }


def _descriptor_only_control(
    case_id: int, net_count: int, pool_size: int, descriptor_fingerprint: int
) -> dict[str, Any]:
    if (
        descriptor_fingerprint == 0
        or _checked_product(net_count, pool_size, "initial route queries") != _FIXED_INITIAL_QUERIES
    ):
        raise ControlError(f"fixed-query case {case_id} has an invalid frozen descriptor")
    return {
        "case_id": case_id,
        "descriptor_fingerprint": descriptor_fingerprint,
        "workload_net_count": net_count,
        "requested_pool_size": pool_size,
        "evidence_status": "descriptor_only_excluded",
        "initial_candidate_preparation": {
            "opportunity_route_queries": _FIXED_INITIAL_QUERIES,
            "opportunity_route_work_units": _FIXED_INITIAL_WORK_UNITS,
        },
        "diversity_accounting": {
            "scope": "requested_within_net_unordered_candidate_pairs_only",
            "requested_pool_pair_opportunity": _requested_pair_count(net_count, pool_size),
            "cross_net_candidate_pairs_included": False,
        },
        "execution_measurement": {
            "status": "unavailable",
            "reason": "descriptor-only control is excluded from case construction and execution",
        },
    }


def project_document(
    authorities: Mapping[int, tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]],
) -> dict[str, Any]:
    """Derive the fixed-query artifact from fully validated joined authorities."""
    if tuple(authorities.keys()) != tuple(case[0] for case in _EXECUTED_CASES):
        raise ControlError("executed authorities must be ordered cases 2002, 2003, and 2004")
    first_raw = authorities[2002][0]
    source = (
        first_raw["source_commit"],
        first_raw["source_stamped"],
        first_raw["source_tree_dirty"],
    )
    environment = first_raw["environment"]
    common_config = _common_configuration(first_raw["config"])
    controls: list[dict[str, Any]] = [
        _descriptor_only_control(*case) for case in _DESCRIPTOR_ONLY_CASES
    ]
    for case_id, _, _ in _EXECUTED_CASES:
        raw, report, operational = authorities[case_id]
        if (
            (raw["source_commit"], raw["source_stamped"], raw["source_tree_dirty"]) != source
            or raw["environment"] != environment
            or _common_configuration(raw["config"]) != common_config
        ):
            raise ControlError(
                "executed fixed-query cells must share source, environment, and caps"
            )
        controls.append(_executed_control(raw, report, operational))

    whole_opportunities = [row["whole_trial"]["route_query_opportunity"] for row in controls[2:]]
    if whole_opportunities != [1536, 1280, 1152] or len(set(whole_opportunities)) != 3:
        raise ControlError("whole-trial opportunities do not expose the frozen shape difference")
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": source[0],
        "source_stamped": source[1],
        "source_tree_dirty": source[2],
        "standalone_decision_eligible": False,
        "phase4_complete": False,
        "fixed_query_coverage_complete": True,
        "control_definition": {
            "comparison_scope": "initial_candidate_preparation_opportunity_only",
            "fixed_route_query_opportunity": _FIXED_INITIAL_QUERIES,
            "maximum_work_units_per_query": _PER_QUERY_WORK_UNITS,
            "fixed_route_work_opportunity": _FIXED_INITIAL_WORK_UNITS,
            "whole_trial_equal_across_shapes": False,
            "whole_trial_reason": "two regeneration epochs add two route queries per net",
            "diversity_scope": "within_net_only_no_cross_net_pairs",
        },
        "common_execution_configuration": common_config,
        "host_environment": copy.deepcopy(environment),
        "controls": controls,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = compute_artifact_checksum(result)
    result["source_envelope_checksum"] = compute_source_envelope_checksum(result)
    return result


def validate_authorities(
    raw: Mapping[str, Any],
    report: Mapping[str, Any],
    operational: Mapping[str, Any],
    *,
    expected_commit: str,
) -> None:
    raw_validator.validate_document(raw, expected_commit=expected_commit)
    report_validator.validate_join(raw, report, expected_commit=expected_commit)
    operational_projector.validate_projection(raw, operational)


def validate_artifact(
    authorities: Mapping[int, tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]],
    value: Mapping[str, Any],
    *,
    expected_commit: str,
) -> None:
    if _COMMIT.fullmatch(expected_commit) is None:
        raise ControlError("publication requires an independently supplied expected commit")
    for case_id, authority in authorities.items():
        raw, report, operational = authority
        if raw["config"]["case_id"] != case_id:
            raise ControlError("authority map key differs from the Raw case identity")
        validate_authorities(raw, report, operational, expected_commit=expected_commit)
    expected = project_document(authorities)
    _assert_exact(expected, value)
    if value["artifact_checksum"] != compute_artifact_checksum(value):
        raise ControlError("artifact_checksum does not authenticate the fixed-query artifact")
    if value["source_envelope_checksum"] != compute_source_envelope_checksum(value):
        raise ControlError("source_envelope_checksum does not authenticate source provenance")


def _assert_exact(expected: Any, actual: Any, label: str = "fixed-query artifact") -> None:
    if type(actual) is not type(expected):
        raise ControlError(f"{label} has a noncanonical JSON type")
    if isinstance(expected, dict):
        if tuple(actual.keys()) != tuple(expected.keys()):
            raise ControlError(f"{label} fields or field order differ from the rebuilt artifact")
        for key in expected:
            _assert_exact(expected[key], actual[key], f"{label}.{key}")
    elif isinstance(expected, list):
        if len(actual) != len(expected):
            raise ControlError(f"{label} length differs from the rebuilt artifact")
        for index, (expected_child, actual_child) in enumerate(zip(expected, actual, strict=True)):
            _assert_exact(expected_child, actual_child, f"{label}[{index}]")
    elif actual != expected:
        raise ControlError(f"{label} differs from the rebuilt fixed-query artifact")


def _reject_duplicate_pairs(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ControlError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _reject_non_json_constant(value: str) -> Any:
    raise ControlError(f"non-JSON numeric constant: {value}")


def _check_nesting(value: Any) -> None:
    pending = [(value, 0)]
    while pending:
        current, depth = pending.pop()
        if depth > _MAXIMUM_NESTING_DEPTH:
            raise ControlError("fixed-query artifact exceeds the nesting-depth bound")
        if isinstance(current, dict):
            pending.extend((child, depth + 1) for child in current.values())
        elif isinstance(current, list):
            pending.extend((child, depth + 1) for child in current)


def read_artifact(path: pathlib.Path) -> Mapping[str, Any]:
    try:
        with path.open("rb") as stream:
            encoded = stream.read(_MAXIMUM_ARTIFACT_BYTES + 1)
        if len(encoded) > _MAXIMUM_ARTIFACT_BYTES:
            raise ControlError(
                f"fixed-query artifact exceeds the {_MAXIMUM_ARTIFACT_BYTES}-byte input bound"
            )
        text = encoded.decode("utf-8")
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_pairs,
            parse_constant=_reject_non_json_constant,
        )
        _check_nesting(value)
        canonical = (
            json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
        )
        if text != canonical:
            raise ControlError("fixed-query artifact must be canonical one-line JSON plus LF")
        if not isinstance(value, dict):
            raise ControlError("fixed-query artifact must be an object")
        return value
    except (OSError, UnicodeError, json.JSONDecodeError, RecursionError, ValueError) as error:
        if isinstance(error, ControlError):
            raise
        raise ControlError(f"cannot read fixed-query artifact {path}: {error}") from error


def encode_artifact(value: Mapping[str, Any]) -> str:
    encoded = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")) + "\n"
    if len(encoded.encode("utf-8")) > _MAXIMUM_ARTIFACT_BYTES:
        raise ControlError(
            f"fixed-query artifact exceeds the {_MAXIMUM_ARTIFACT_BYTES}-byte output bound"
        )
    return encoded


def _load_authorities(
    options: argparse.Namespace,
) -> dict[int, tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]]:
    if _COMMIT.fullmatch(options.expected_commit) is None:
        raise ControlError("publication requires an independently supplied expected commit")
    result: dict[int, tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]] = {}
    for case_id, _, _ in _EXECUTED_CASES:
        raw = raw_validator.read_validated_publication_document(
            getattr(options, f"raw_{case_id}"), expected_commit=options.expected_commit
        )
        report = report_validator.read_report_document(getattr(options, f"report_{case_id}"))
        report_validator.validate_join(raw, report, expected_commit=options.expected_commit)
        operational = operational_projector.read_projection(
            getattr(options, f"operational_{case_id}")
        )
        operational_projector.validate_projection(raw, operational)
        result[case_id] = (raw, report, operational)
    return result


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-commit", required=True)
    for case_id, _, _ in _EXECUTED_CASES:
        parser.add_argument(f"--raw-{case_id}", required=True, type=pathlib.Path)
        parser.add_argument(f"--report-{case_id}", required=True, type=pathlib.Path)
        parser.add_argument(f"--operational-{case_id}", required=True, type=pathlib.Path)
    parser.add_argument("--validate", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        authorities = _load_authorities(options)
        if options.validate is None:
            sys.stdout.write(encode_artifact(project_document(authorities)))
        else:
            validate_artifact(
                authorities,
                read_artifact(options.validate),
                expected_commit=options.expected_commit,
            )
    except (
        ControlError,
        operational_projector.ProjectionError,
        raw_validator.EvidenceError,
        report_validator.EvidenceError,
    ) as error:
        print(f"Phase 4 fixed-query control failed: {error}", file=sys.stderr)
        return 1
    if options.validate is not None:
        print("validated one Phase 4 Fixed-Query Control v1 artifact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
