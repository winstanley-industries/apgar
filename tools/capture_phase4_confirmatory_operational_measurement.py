"""Capture the frozen ordinary Corpus-v2 operational development cell."""

from __future__ import annotations

import argparse
import os
import sys
from collections.abc import Sequence
from types import ModuleType

from tools import capture_phase4_operational_measurement as capture_tool
from tools import phase4_confirmatory_operational_authority as authority
from tools import validate_phase4_operational_measurement as measurement_validator


def _parser(
    *,
    testing: bool,
    authority_module: ModuleType = authority,
) -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--corpus-version", required=True, type=capture_tool._strict_positive)
    parser.add_argument(
        "--raw-wire-schema-version",
        required=True,
        type=capture_tool._strict_positive,
    )
    parser.add_argument("--case-id", required=True, type=capture_tool._strict_positive)
    parser.add_argument("--pool-size", required=True, type=capture_tool._strict_positive)
    parser.add_argument(
        "--workers",
        type=capture_tool._strict_positive,
        default=authority_module.WORKERS,
    )
    parser.add_argument(
        "--setup-ns",
        type=capture_tool._strict_positive,
        default=authority_module.SETUP_NS,
    )
    parser.add_argument(
        "--prepared-ns",
        type=capture_tool._strict_positive,
        default=authority_module.PREPARED_NS,
    )
    parser.add_argument(
        "--cold-ns",
        type=capture_tool._strict_positive,
        default=authority_module.COLD_NS,
    )
    parser.add_argument(
        "--address-space-bytes",
        type=capture_tool._strict_positive,
        default=authority_module.ADDRESS_SPACE_BYTES,
    )
    parser.add_argument(
        "--peak-host-bytes",
        type=capture_tool._strict_positive,
        default=authority_module.PEAK_HOST_BYTES,
    )
    parser.add_argument(
        "--maximum-nets",
        type=capture_tool._strict_positive,
        default=authority_module.MAXIMUM_NETS,
    )
    parser.add_argument(
        "--maximum-compiled-nodes",
        type=capture_tool._strict_positive,
        default=authority_module.MAXIMUM_COMPILED_NODES,
    )
    parser.add_argument(
        "--maximum-compiled-host-bytes",
        type=capture_tool._strict_positive,
        default=authority_module.MAXIMUM_COMPILED_HOST_BYTES,
    )
    parser.add_argument(
        "--maximum-active-regions",
        type=capture_tool._strict_positive,
        default=authority_module.MAXIMUM_ACTIVE_REGIONS,
    )
    parser.add_argument(
        "--maximum-board-entities",
        type=capture_tool._strict_positive,
        default=authority_module.MAXIMUM_BOARD_ENTITIES,
    )
    if testing:
        parser.add_argument("--testing-allow-unstamped", action="store_true")
    parser.add_argument("--apgar-commit", required=True)
    return parser


def _require_scope(
    options: argparse.Namespace,
    *,
    authority_module: ModuleType = authority,
    authority_label: str = "ordinary",
) -> None:
    require_frozen_authority = getattr(
        authority_module,
        "require_frozen_authority",
        None,
    )
    if require_frozen_authority is not None:
        require_frozen_authority()
    config = {
        "schema_version": 1,
        "case_id": options.case_id,
        "requested_pool_size": options.pool_size,
        "preparation_worker_count": options.workers,
        "repetitions": authority_module.REPETITIONS,
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
        options.corpus_version != authority_module.CORPUS_VERSION
        or options.raw_wire_schema_version != authority_module.RAW_WIRE_SCHEMA_VERSION
        or not authority_module.has_exact_config(config)
    ):
        raise capture_tool.CaptureError(
            f"confirmatory {authority_label} operational capture is restricted to the frozen "
            f"Corpus {authority_module.CORPUS_VERSION}, "
            f"Raw/Wire {authority_module.RAW_WIRE_SCHEMA_VERSION}, "
            f"({authority_module.CASE_ID},{authority_module.POOL_SIZE}) configuration"
        )


def main(
    argv: Sequence[str] | None = None,
    *,
    testing: bool = False,
    authority_module: ModuleType = authority,
    authority_label: str = "ordinary",
) -> int:
    arguments = list(sys.argv[1:] if argv is None else argv)
    try:
        capture_tool._check_duplicate_options(arguments)
    except capture_tool.CaptureError as error:
        print(f"Phase 4 confirmatory operational capture failed: {error}", file=sys.stderr)
        return 2
    parser = _parser(testing=testing, authority_module=authority_module)
    options = parser.parse_args(arguments)
    try:
        _require_scope(
            options,
            authority_module=authority_module,
            authority_label=authority_label,
        )
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
            worker_runfile = authority_module.TEST_WORKER
            options.publication_invocation = authority_module.TEST_INVOCATION
            options.worker_target = authority_module.TEST_WORKER_TARGET
            options.require_clean_source = False
        else:
            options.testing_allow_unstamped = False
            for name in ("LD_PRELOAD", "LD_AUDIT"):
                if os.environ.get(name):
                    raise capture_tool.CaptureError(
                        f"unsafe dynamic-loader injection variable is set: {name}"
                    )
            worker_runfile = authority_module.PRODUCTION_WORKER
            options.publication_invocation = authority_module.PRODUCTION_INVOCATION
            options.worker_target = authority_module.PRODUCTION_WORKER_TARGET
            options.require_clean_source = True
        options.worker_environment = {
            "PATH": os.defpath,
            "LANG": "C",
            "LC_ALL": "C",
        }
        options.worker = authority_module.resolve_bundled_worker(worker_runfile)
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
            expected_corpus_version=authority_module.CORPUS_VERSION,
            expected_publication_invocation=options.publication_invocation,
            expected_worker_target=options.worker_target,
            expected_worker_sha256=authority_module.sha256_file(options.worker),
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
    from tools.phase4_confirmatory_operational_launcher_handshake import require_launcher

    require_launcher("phase4_confirmatory_operational_capture_py")
    raise SystemExit(main())
