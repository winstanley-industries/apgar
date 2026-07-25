"""Publish the frozen same-run Corpus-v2 operational development cell."""

from __future__ import annotations

import argparse
import pathlib
import sys
from collections.abc import Mapping, Sequence
from typing import Any

from tools import phase4_confirmatory_same_run_operational_authority as authority
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator
from tools import validate_phase4_same_run_decision_telemetry as same_run_validator


def _raw_document(value: Any) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise raw_validator.EvidenceError(
            "confirmatory same-run operational Raw input must be an object"
        )
    return value


def _require_scope(raw: Mapping[str, Any]) -> None:
    config = raw["config"]
    if (
        raw.get("raw_evidence_schema_version") != 2
        or raw.get("wire_schema_version") != 2
        or not authority.has_exact_config(config)
    ):
        raise raw_validator.EvidenceError(
            "confirmatory same-run operational publication is restricted to Raw/Wire 2 (10100,4)"
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
    """Validate one complete confirmatory same-run operational publication."""
    raw_validator.validate_confirmatory_same_run_document_v2(
        raw,
        allow_unstamped=testing,
        expected_commit=expected_commit,
    )
    raw_document = _raw_document(raw)
    _require_scope(raw_document)
    validated_sidecar = same_run_validator.validate_confirmatory_join(
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
    operational_validator.validate_confirmatory_same_run_publication(
        raw_document,
        validated_sidecar,
        validated_capture,
        publication,
    )


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

        raw = raw_validator.read_document(options.raw)
        raw_validator.validate_confirmatory_same_run_document_v2(
            raw,
            allow_unstamped=testing,
            expected_commit=options.expected_commit,
        )
        raw_document = _raw_document(raw)
        _require_scope(raw_document)

        sidecar = same_run_validator.read_document(options.same_run_telemetry)
        validated_sidecar = same_run_validator.validate_confirmatory_join(
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
        publication = operational_validator.project_confirmatory_same_run_document(
            raw_document,
            validated_sidecar,
            capture,
        )
        if options.validate is not None:
            validated_publication = operational_validator.read_publication(options.validate)
            operational_validator.validate_confirmatory_same_run_publication_against_expected(
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
            f"Phase 4 confirmatory same-run operational publication failed: {error}",
            file=sys.stderr,
        )
        return 1
    if options.validate is not None:
        print("validated one Phase 4 confirmatory same-run operational measurement publication")
    return 0


if __name__ == "__main__":
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_same_run_operational_measurement_validator_py")
    raise SystemExit(main())
