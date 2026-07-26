"""Publish the frozen H=4096 same-run Corpus-v2 operational development cell."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import phase4_confirmatory_h4096_same_run_operational_authority as authority
from tools import project_phase4_operational_evidence as projection_validator
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw
from tools import (
    validate_phase4_confirmatory_h4096_same_run_decision_telemetry as h4096_telemetry,
)
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as telemetry_validator

_RAW_AUTHORITY = "phase4_confirmatory_same_run_raw_evidence_v2"
_TELEMETRY_AUTHORITY = "phase4_confirmatory_same_run_decision_telemetry_v2"
_ARTIFACT_DOMAIN = (
    "APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-ARTIFACT-V2"
)
_SOURCE_DOMAIN = "APGAR-PHASE4-CONFIRMATORY-SAME-RUN-OPERATIONAL-MEASUREMENT-PUBLICATION-SOURCE-V2"


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory H=4096 same-run operational Raw input must be an object"
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
    sidecar: Mapping[str, Any],
    capture: Mapping[str, Any],
) -> dict[str, Any]:
    """Project inputs already authenticated under the H=4096 authority."""
    publication = operational_validator.project_confirmatory_same_run_document(
        raw,
        sidecar,
        capture,
    )
    publication["raw_authority_binding"]["authority"] = _RAW_AUTHORITY
    publication["same_run_telemetry_binding"]["authority"] = _TELEMETRY_AUTHORITY
    publication["artifact_checksum"] = 0
    publication["source_envelope_checksum"] = 0
    publication["artifact_checksum"] = _publication_checksum(publication)
    publication["source_envelope_checksum"] = _publication_source_checksum(publication)
    return publication


def validate_publication_against_expected(
    expected: Mapping[str, Any],
    publication: Mapping[str, Any],
) -> None:
    projection_validator.assert_exact_projection(expected, publication)
    if publication["artifact_checksum"] != _publication_checksum(publication):
        raise operational_validator.EvidenceError(
            "H=4096 same-run publication artifact checksum is invalid"
        )
    if publication["source_envelope_checksum"] != _publication_source_checksum(publication):
        raise operational_validator.EvidenceError(
            "H=4096 same-run publication source envelope checksum is invalid"
        )


def _validated_inputs(
    raw: Any,
    sidecar: Any,
    capture: Any,
    *,
    expected_commit: str,
    testing: bool,
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]:
    authority.require_frozen_authority()
    h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
        raw,
        allow_unstamped=testing,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    validated_sidecar = h4096_telemetry.validate_confirmatory_h4096_join(
        raw_document,
        sidecar,
        allow_unstamped=testing,
        expected_commit=expected_commit,
    )
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
    return raw_document, validated_sidecar, validated_capture


def project_document(
    raw: Any,
    sidecar: Any,
    capture: Any,
    *,
    expected_commit: str,
    testing: bool = False,
) -> dict[str, Any]:
    """Authenticate and build the domain-separated H=4096 publication."""
    raw_document, validated_sidecar, validated_capture = _validated_inputs(
        raw,
        sidecar,
        capture,
        expected_commit=expected_commit,
        testing=testing,
    )
    return _project_validated_document(
        raw_document,
        validated_sidecar,
        validated_capture,
    )


def validate_join(
    raw: Any,
    sidecar: Any,
    capture: Any,
    publication: Any,
    *,
    expected_commit: str,
    testing: bool = False,
) -> None:
    """Validate one complete H=4096 same-run operational publication."""
    raw_document, validated_sidecar, validated_capture = _validated_inputs(
        raw,
        sidecar,
        capture,
        expected_commit=expected_commit,
        testing=testing,
    )
    expected = _project_validated_document(
        raw_document,
        validated_sidecar,
        validated_capture,
    )
    if not isinstance(publication, Mapping):
        raise operational_validator.EvidenceError(
            "H=4096 same-run operational publication must be an object"
        )
    validate_publication_against_expected(expected, publication)


def main(argv: Sequence[str] | None = None, *, testing: bool = False) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw", required=True, type=pathlib.Path)
    parser.add_argument("--same-run-telemetry", required=True, type=pathlib.Path)
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
        h4096_raw.validate_confirmatory_h4096_same_run_document_v2(
            raw,
            allow_unstamped=testing,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)

        sidecar = telemetry_validator.read_document(options.same_run_telemetry)
        validated_sidecar = h4096_telemetry.validate_confirmatory_h4096_join(
            raw_document,
            sidecar,
            allow_unstamped=testing,
            expected_commit=options.expected_commit,
        )

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
        publication = _project_validated_document(
            raw_document,
            validated_sidecar,
            capture,
        )
        if options.validate is not None:
            validated_publication = operational_validator.read_publication(options.validate)
            validate_publication_against_expected(
                publication,
                validated_publication,
            )
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
            f"Phase 4 confirmatory H=4096 same-run operational publication failed: {error}",
            file=sys.stderr,
        )
        return 1
    if options.validate is not None:
        print(
            "validated one Phase 4 confirmatory H=4096 same-run operational measurement publication"
        )
    return 0


if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_h4096_same_run_operational_measurement_validator_py")
    raise SystemExit(main())
