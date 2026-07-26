"""Publish the frozen H=4096 ordinary Corpus-v2 operational development cell."""

from __future__ import annotations

import argparse
import copy
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import phase4_confirmatory_h4096_operational_authority as authority
from tools import project_phase4_operational_evidence as projection_validator
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator

_RAW_AUTHORITY = "phase4_confirmatory_raw_evidence_v2"
_ARTIFACT_DOMAIN = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2"
_SOURCE_DOMAIN = "APGAR-PHASE4-CONFIRMATORY-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2"


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory H=4096 ordinary operational Raw input must be an object"
        )
    return value


def _publication_checksum(value: Mapping[str, Any]) -> int:
    payload = {
        key: item
        for key, item in value.items()
        if key not in {"artifact_checksum", "source_envelope_checksum"}
    }
    hashed = raw_validator.StableHashBuilder()
    hashed.string(_ARTIFACT_DOMAIN)
    hashed.string(operational_validator._canonical(payload))
    return hashed.finish()


def _publication_source_checksum(value: Mapping[str, Any]) -> int:
    hashed = raw_validator.StableHashBuilder()
    hashed.string(_SOURCE_DOMAIN)
    hashed.u32(value["schema_version"])
    hashed.string(value["source_commit"])
    hashed.boolean(value["source_stamped"])
    hashed.boolean(value["source_tree_dirty"])
    hashed.u64(value["artifact_checksum"])
    return hashed.finish()


def _project_validated_document(
    raw: Mapping[str, Any],
    capture: Mapping[str, Any],
) -> dict[str, Any]:
    """Project inputs already authenticated under the H=4096 authority."""
    config = raw["config"]
    if (
        raw.get("raw_evidence_schema_version", 1) != 1
        or raw.get("wire_schema_version", 1) != 1
        or not authority.has_exact_config(config)
    ):
        raise operational_validator.EvidenceError(
            "confirmatory H=4096 ordinary operational authority is restricted to "
            "Raw/Wire 1 (10200,8)"
        )
    pair, arms = operational_validator._joined_repetition_zero_arms(raw, capture)
    baseline_semantics = pair["result"]["baseline"]["semantics"]
    candidate_semantics = pair["result"]["candidate"]["semantics"]
    if (
        baseline_semantics.get("corpus_version") != authority.CORPUS_VERSION
        or candidate_semantics.get("corpus_version") != authority.CORPUS_VERSION
        or baseline_semantics.get("budget_checksum") != authority.PAIRED_SEMANTIC_BUDGET_CHECKSUM
        or candidate_semantics.get("budget_checksum") != authority.PAIRED_SEMANTIC_BUDGET_CHECKSUM
    ):
        raise operational_validator.EvidenceError(
            "confirmatory H=4096 ordinary operational replay selected another corpus or budget"
        )
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "campaign_id": "phase4_confirmatory_corpus_v2",
        "eligible_input_to_phase4_aggregation": True,
        "standalone_decision_eligible": False,
        "statistical_timing_eligible": False,
        "coverage_complete": False,
        "cell_operational_telemetry_complete": True,
        "cell_role": "calibration",
        "raw_authority_binding": {
            "authority": _RAW_AUTHORITY,
            "raw_evidence_schema_version": 1,
            "wire_schema_version": 1,
            "corpus_version": authority.CORPUS_VERSION,
            "corpus_checksum": raw["corpus_checksum"],
            "cell_plan_checksum": raw["cell_plan_checksum"],
            "environment_checksum": raw["environment"]["environment_checksum"],
            "authority_run_identity": raw["authority_run_identity"],
            "controller_identity": raw["controller_identity"],
            "raw_artifact_checksum": raw["artifact_checksum"],
            "raw_source_envelope_checksum": raw["source_envelope_checksum"],
        },
        "cell_config": copy.deepcopy(config),
        "cell_identity": {
            "case_id": baseline_semantics["case_id"],
            "descriptor_fingerprint": baseline_semantics["descriptor_fingerprint"],
            "case_checksum": baseline_semantics["case_checksum"],
            "board_content_hash": baseline_semantics["board_content_hash"],
            "workload_checksum": baseline_semantics["workload_checksum"],
            "capacity_model_checksum": baseline_semantics["capacity_model_checksum"],
            "budget_checksum": baseline_semantics["budget_checksum"],
            "workload_net_count": baseline_semantics["workload_net_count"],
            "root_seed": baseline_semantics["root_seed"],
        },
        "reproducibility_provenance": copy.deepcopy(capture["reproducibility_provenance"]),
        "capture_binding": {
            "controller_identity": capture["controller_identity"],
            "capture_run_identity": capture["capture_run_identity"],
            "capture_artifact_checksum": capture["artifact_checksum"],
            "capture_source_envelope_checksum": capture["source_envelope_checksum"],
        },
        "arms": arms,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    result["artifact_checksum"] = _publication_checksum(result)
    result["source_envelope_checksum"] = _publication_source_checksum(result)
    return result


def validate_publication_against_expected(
    expected: Mapping[str, Any],
    publication: Mapping[str, Any],
) -> None:
    projection_validator.assert_exact_projection(expected, publication)
    if publication["artifact_checksum"] != _publication_checksum(publication):
        raise operational_validator.EvidenceError(
            "H=4096 ordinary publication artifact checksum is invalid"
        )
    if publication["source_envelope_checksum"] != _publication_source_checksum(publication):
        raise operational_validator.EvidenceError(
            "H=4096 ordinary publication source envelope checksum is invalid"
        )


def _validated_inputs(
    raw: Any,
    capture: Any,
    *,
    expected_commit: str,
    testing: bool,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    authority.require_frozen_authority()
    h4096_raw.validate_confirmatory_h4096_document(
        raw,
        allow_unstamped=testing,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    worker = authority.resolve_bundled_worker(
        authority.TEST_WORKER if testing else authority.PRODUCTION_WORKER
    )
    validated_capture = operational_validator.validate_capture(
        capture,
        expected_commit=None if testing else expected_commit,
        expected_corpus_version=authority.CORPUS_VERSION,
        expected_publication_invocation=(
            authority.TEST_INVOCATION if testing else authority.PRODUCTION_INVOCATION
        ),
        expected_worker_target=(
            authority.TEST_WORKER_TARGET if testing else authority.PRODUCTION_WORKER_TARGET
        ),
        expected_worker_sha256=authority.sha256_file(worker),
    )
    return raw_document, validated_capture


def project_document(
    raw: Any,
    capture: Any,
    *,
    expected_commit: str,
    testing: bool = False,
) -> dict[str, Any]:
    """Authenticate and build the domain-separated H=4096 publication."""
    raw_document, validated_capture = _validated_inputs(
        raw,
        capture,
        expected_commit=expected_commit,
        testing=testing,
    )
    return _project_validated_document(raw_document, validated_capture)


def validate_join(
    raw: Any,
    capture: Any,
    publication: Any,
    *,
    expected_commit: str,
    testing: bool = False,
) -> None:
    """Validate one complete H=4096 ordinary operational publication."""
    raw_document, validated_capture = _validated_inputs(
        raw,
        capture,
        expected_commit=expected_commit,
        testing=testing,
    )
    expected = _project_validated_document(raw_document, validated_capture)
    if not isinstance(publication, Mapping):
        raise operational_validator.EvidenceError(
            "H=4096 ordinary operational publication must be an object"
        )
    validate_publication_against_expected(expected, publication)


def main(argv: Sequence[str] | None = None, *, testing: bool = False) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--capture", required=True, type=pathlib.Path)
    parser.add_argument("--expected-commit", required=True)
    parser.add_argument("--validate", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path)
    options = parser.parse_args(argv)
    try:
        if options.validate is not None and options.output is not None:
            raise operational_validator.EvidenceError(
                "--validate and --output are mutually exclusive"
            )

        authority.require_frozen_authority()
        raw = raw_validator.read_document(options.raw)
        h4096_raw.validate_confirmatory_h4096_document(
            raw,
            allow_unstamped=testing,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)

        worker = authority.resolve_bundled_worker(
            authority.TEST_WORKER if testing else authority.PRODUCTION_WORKER
        )
        capture = operational_validator.read_capture(
            options.capture,
            expected_commit=None if testing else options.expected_commit,
            expected_corpus_version=authority.CORPUS_VERSION,
            expected_publication_invocation=(
                authority.TEST_INVOCATION if testing else authority.PRODUCTION_INVOCATION
            ),
            expected_worker_target=(
                authority.TEST_WORKER_TARGET if testing else authority.PRODUCTION_WORKER_TARGET
            ),
            expected_worker_sha256=authority.sha256_file(worker),
        )
        publication = _project_validated_document(raw_document, capture)
        if options.validate is not None:
            validated_publication = operational_validator.read_publication(options.validate)
            validate_publication_against_expected(publication, validated_publication)
        else:
            encoded = operational_validator._canonical(publication) + "\n"
            encoded_bytes = encoded.encode("utf-8")
            if len(encoded_bytes) > operational_validator._MAXIMUM_PUBLICATION_BYTES:
                raise operational_validator.EvidenceError("publication exceeds 32 MiB")
            if options.output is None:
                sys.stdout.write(encoded)
            else:
                operational_validator._write_publication_no_replace(
                    options.output,
                    encoded_bytes,
                )
    except (
        operational_validator.CaptureError,
        operational_validator.EvidenceError,
        raw_validator.EvidenceError,
        OSError,
        ValueError,
    ) as error:
        print(
            f"Phase 4 confirmatory H=4096 ordinary operational publication failed: {error}",
            file=sys.stderr,
        )
        return 1
    if options.validate is not None:
        print("validated one Phase 4 confirmatory H=4096 ordinary operational publication")
    return 0


if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_h4096_operational_measurement_validator_py")
    raise SystemExit(main())
