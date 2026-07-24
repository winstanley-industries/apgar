"""Capture the frozen ordinary Corpus-v2 operational development cell."""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence

from tools import capture_phase4_operational_measurement as capture_tool
from tools import phase4_confirmatory_operational_authority as authority
from tools import validate_phase4_operational_measurement as measurement_validator


def _parser(*, testing: bool) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-version", required=True, type=capture_tool._strict_positive)
    parser.add_argument(
        "--raw-wire-schema-version",
        required=True,
        type=capture_tool._strict_positive,
    )
    parser.add_argument("--case-id", required=True, type=capture_tool._strict_positive)
    parser.add_argument("--pool-size", required=True, type=capture_tool._strict_positive)
    parser.add_argument("--workers", type=capture_tool._strict_positive, default=authority.WORKERS)
    parser.add_argument(
        "--setup-ns",
        type=capture_tool._strict_positive,
        default=authority.SETUP_NS,
    )
    parser.add_argument(
        "--prepared-ns",
        type=capture_tool._strict_positive,
        default=authority.PREPARED_NS,
    )
    parser.add_argument(
        "--cold-ns",
        type=capture_tool._strict_positive,
        default=authority.COLD_NS,
    )
    parser.add_argument(
        "--address-space-bytes",
        type=capture_tool._strict_positive,
        default=authority.ADDRESS_SPACE_BYTES,
    )
    parser.add_argument(
        "--peak-host-bytes",
        type=capture_tool._strict_positive,
        default=authority.PEAK_HOST_BYTES,
    )
    parser.add_argument(
        "--maximum-nets",
        type=capture_tool._strict_positive,
        default=authority.MAXIMUM_NETS,
    )
    parser.add_argument(
        "--maximum-compiled-nodes",
        type=capture_tool._strict_positive,
        default=authority.MAXIMUM_COMPILED_NODES,
    )
    parser.add_argument(
        "--maximum-compiled-host-bytes",
        type=capture_tool._strict_positive,
        default=authority.MAXIMUM_COMPILED_HOST_BYTES,
    )
    parser.add_argument(
        "--maximum-active-regions",
        type=capture_tool._strict_positive,
        default=authority.MAXIMUM_ACTIVE_REGIONS,
    )
    parser.add_argument(
        "--maximum-board-entities",
        type=capture_tool._strict_positive,
        default=authority.MAXIMUM_BOARD_ENTITIES,
    )
    if testing:
        parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--apgar-commit", required=True)
    return parser


def _require_scope(options: argparse.Namespace) -> None:
    config = {
        "schema_version": 1,
        "case_id": options.case_id,
        "requested_pool_size": options.pool_size,
        "preparation_worker_count": options.workers,
        "repetitions": authority.REPETITIONS,
        "maximum_setup_elapsed_nanoseconds": options.setup_ns,
        "external_budget": {
            "maximum_prepared_elapsed_nanoseconds": options.prepared_ns,
            "maximum_cold_elapsed_nanoseconds": options.cold_ns,
            "maximum_address_space_bytes": options.address_space_bytes,
            "maximum_peak_host_bytes": options.peak_host_bytes,
        },
        "corpus_limits": {
            "maximum_nets": options.maximum_nets,
            "maximum_compiled_nodes": options.maximum_compiled_nodes,
            "maximum_compiled_host_bytes": options.maximum_compiled_host_bytes,
            "maximum_active_regions": options.maximum_active_regions,
            "maximum_board_entities": options.maximum_board_entities,
        },
    }
    if (
        options.corpus_version != authority.CORPUS_VERSION
        or options.raw_wire_schema_version != authority.RAW_WIRE_SCHEMA_VERSION
        or not authority.has_exact_config(config)
    ):
        raise capture_tool.CaptureError(
            "confirmatory ordinary operational capture is restricted to the frozen "
            "Corpus 2, Raw/Wire 1, (10200,4) configuration"
        )


def main(argv: Sequence[str] | None = None, *, testing: bool = False) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        capture_tool._check_duplicate_options(arguments)
    except capture_tool.CaptureError as error:
        print(f"Phase 4 confirmatory operational capture failed: {error}", file=sys.stderr)
        return 2
    parser = _parser(testing=testing)
    options = parser.parse_args(arguments)
    try:
        _require_scope(options)
        if len(options.apgar_commit) != 40 or any(
            character not in "0123456789abcdef" for character in options.apgar_commit
        ):
            raise capture_tool.CaptureError(
                "--apgar-commit must be 40 lowercase hexadecimal characters"
            )
        if testing:
            if not options.testing_allow_unstamped:
                raise capture_tool.CaptureError(
                    "the test-only capture requires --testing-allow-unstamped"
                )
            worker_runfile = authority.TEST_WORKER
            options.publication_invocation = authority.TEST_INVOCATION
            options.worker_target = authority.TEST_WORKER_TARGET
            options.require_clean_source = False
        else:
            options.testing_allow_unstamped = False
            for name in ("LD_PRELOAD", "LD_AUDIT"):
                if os.environ.get(name):
                    raise capture_tool.CaptureError(
                        f"unsafe dynamic-loader injection variable is set: {name}"
                    )
            worker_runfile = authority.PRODUCTION_WORKER
            options.publication_invocation = authority.PRODUCTION_INVOCATION
            options.worker_target = authority.PRODUCTION_WORKER_TARGET
            options.require_clean_source = True
        options.worker_environment = {
            "PATH": os.defpath,
            "LANG": "C",
            "LC_ALL": "C",
        }
        options.worker = authority.resolve_bundled_worker(worker_runfile)
        options.fixture = None
        if not options.worker.is_file() or not os.access(options.worker, os.X_OK):
            if testing:
                role = "test-only"
            else:
                role = "production"
            raise capture_tool.CaptureError(
                f"confirmatory {role} operational worker is missing or not executable"
            )
        artifact = capture_tool.capture(options)
        measurement_validator.validate_capture(
            artifact,
            expected_commit=None if testing else options.apgar_commit,
            expected_corpus_version=authority.CORPUS_VERSION,
            expected_publication_invocation=options.publication_invocation,
            expected_worker_target=options.worker_target,
            expected_worker_sha256=authority.sha256_file(options.worker),
        )
        encoded = capture_tool._canonical(artifact) + "\n"
        if len(encoded.encode("utf-8")) > capture_tool._MAXIMUM_CAPTURE_BYTES:
            raise capture_tool.CaptureError("operational capture exceeds 32 MiB")
        sys.stdout.write(encoded)
    except (
        capture_tool.CaptureError,
        measurement_validator.EvidenceError,
        OSError,
        OverflowError,
    ) as error:
        print(f"Phase 4 confirmatory operational capture failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
