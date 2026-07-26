"""Closed authority constants for the H=4096 same-run operational slice."""

from __future__ import annotations

import hashlib
import pathlib
from collections.abc import Mapping
from typing import Any

from tools import validate_phase4_confirmatory_canonical_budget_roster_v3 as budget_v3
from tools import validate_phase4_confirmatory_decision_protocol_v2 as protocol_v2
from tools import validate_phase4_confirmatory_h4096_raw_evidence as h4096_raw

CORPUS_VERSION = 2
RAW_WIRE_SCHEMA_VERSION = 2
CASE_ID = 10_100
POOL_SIZE = 4
WORKERS = 4
REPETITIONS = 20
SETUP_NS = 300_000_000_000
PREPARED_NS = 300_000_000_000
COLD_NS = 300_000_000_000
ADDRESS_SPACE_BYTES = 64 << 30
PEAK_HOST_BYTES = 16 << 30
MAXIMUM_NETS = 4096
MAXIMUM_COMPILED_NODES = 100_000_000
MAXIMUM_COMPILED_HOST_BYTES = 8 << 30
MAXIMUM_ACTIVE_REGIONS = 250_000
MAXIMUM_BOARD_ENTITIES = 100_000
CANONICAL_ALGORITHM_BUDGET_CHECKSUM = 8_829_615_204_625_848_656
CONFIGURATION_AUTHORITY = "phase4_confirmatory_corpus_v2_h4096"

PRODUCTION_WORKER = "phase4_confirmatory_h4096_same_run_operational_replay_worker"
TEST_WORKER = "phase4_confirmatory_h4096_same_run_operational_replay_test_worker"
PRODUCTION_INVOCATION = (
    "bazel --batch run --config=benchmark //:phase4_confirmatory_h4096_same_run_operational_capture"
)
PRODUCTION_WORKER_TARGET = "//:phase4_confirmatory_h4096_same_run_operational_replay_worker"
TEST_INVOCATION = (
    "bazel --batch run --config=benchmark "
    "//:phase4_confirmatory_h4096_same_run_operational_capture_test"
)
TEST_WORKER_TARGET = "//:phase4_confirmatory_h4096_same_run_operational_replay_test_worker"

_AUTHORITY_SUBSTITUTION = {
    "purpose": "same_run_operational_measurement",
    "supersedes": "phase4_confirmatory_same_run_operational_measurement_publication_v1",
    "authority": "phase4_confirmatory_same_run_operational_measurement_publication_v2",
}


def require_frozen_authority() -> None:
    """Authenticate Protocol v2, roster v3, and the exact open cell."""
    try:
        protocol = protocol_v2.read_protocol()
        cells = h4096_raw.confirmatory_h4096_initial_cells(same_run=True)
        roster = budget_v3.validate_roster()
        budgets = budget_v3.budget_map(roster)
    except ValueError as error:
        raise h4096_raw.EvidenceError(
            f"cannot authenticate frozen H=4096 operational authority: {error}"
        ) from error
    rows = [
        row
        for row in protocol["artifact_authority_namespace"]["substitutions"]
        if row["purpose"] == _AUTHORITY_SUBSTITUTION["purpose"]
    ]
    if (
        rows != [_AUTHORITY_SUBSTITUTION]
        or protocol["campaign"]["configuration_authority"] != CONFIGURATION_AUTHORITY
        or cells != frozenset({(CASE_ID, POOL_SIZE)})
    ):
        raise h4096_raw.EvidenceError("frozen H=4096 same-run operational authority drifted")
    budget = protocol["canonical_algorithm_budget_authority"]
    if (
        budget["configuration_authority"] != CONFIGURATION_AUTHORITY
        or budget["configuration"]["history_step_per_overuse_unit"] != 4096
        or budget["configuration"]["present_step_per_overuse_unit"] != 1
        or budget["roster_checksum"] != 18_429_170_436_700_418_962
        or budgets.get((CASE_ID, POOL_SIZE)) != CANONICAL_ALGORITHM_BUDGET_CHECKSUM
    ):
        raise h4096_raw.EvidenceError("frozen H=4096 same-run operational budget authority drifted")


def expected_config() -> dict[str, Any]:
    return {
        "schema_version": 1,
        "case_id": CASE_ID,
        "requested_pool_size": POOL_SIZE,
        "preparation_worker_count": WORKERS,
        "repetitions": REPETITIONS,
        "maximum_setup_elapsed_nanoseconds": SETUP_NS,
        "external_budget": {
            "maximum_prepared_elapsed_nanoseconds": PREPARED_NS,
            "maximum_cold_elapsed_nanoseconds": COLD_NS,
            "maximum_address_space_bytes": ADDRESS_SPACE_BYTES,
            "maximum_peak_host_bytes": PEAK_HOST_BYTES,
        },
        "corpus_limits": {
            "maximum_nets": MAXIMUM_NETS,
            "maximum_compiled_nodes": MAXIMUM_COMPILED_NODES,
            "maximum_compiled_host_bytes": MAXIMUM_COMPILED_HOST_BYTES,
            "maximum_active_regions": MAXIMUM_ACTIVE_REGIONS,
            "maximum_board_entities": MAXIMUM_BOARD_ENTITIES,
        },
    }


def has_exact_config(value: Any) -> bool:
    return isinstance(value, Mapping) and value == expected_config()


def resolve_bundled_worker(relative: str) -> pathlib.Path:
    """Resolve only from the runfiles tree containing this trusted module."""
    module = pathlib.Path(__file__).absolute()
    runfiles_root: pathlib.Path | None = None
    for parent in module.parents:
        if parent.name == "_main" and parent.parent.name.endswith(".runfiles"):
            runfiles_root = parent
            break
    if runfiles_root is None:
        raise OSError(
            "confirmatory H=4096 same-run operational authority requires its Bazel runfiles tree"
        )
    candidate = runfiles_root / relative
    if not candidate.is_file():
        raise OSError(
            f"bundled confirmatory H=4096 same-run operational worker is unavailable: {relative}"
        )
    return candidate


def sha256_file(path: pathlib.Path) -> str:
    hashed = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            hashed.update(chunk)
    return hashed.hexdigest()
