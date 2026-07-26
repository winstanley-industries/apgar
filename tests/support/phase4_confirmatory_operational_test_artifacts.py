"""Pure synthetic operational artifacts for closed Corpus-v2 validator tests.

The helpers construct only serialized validation inputs. They never invoke an
evidence runner, launch a replay worker, read a board fixture, construct a
representative case, or execute an allocator.
"""

from __future__ import annotations

import copy
import stat
from collections.abc import Mapping
from types import ModuleType
from typing import Any

from tests.support import phase4_confirmatory_h4096_test_artifacts as raw_artifacts
from tools import capture_phase4_operational_measurement as capture_tool
from tools import validate_phase4_operational_measurement as operational_validator
from tools import validate_phase4_raw_evidence as raw_validator

_COMPILER_IDENTITY = "synthetic-test-compiler"
_CONTROLLER_IDENTITY = 5_001
_CAPTURE_RUN_IDENTITY = 5_002
_MEASUREMENT_SCOPE = (
    "controller_monotonic_fork_to_exact_wait4_reap_including_exec_setup_warmup_"
    "replay_serialization_release"
)


def _candidate_session(semantics: Mapping[str, Any]) -> dict[str, Any]:
    counters = {
        "completed_regeneration_epochs": 0,
        "planning_expanded_resource_visits": 0,
        "requested_columns": 0,
        "route_queries": 0,
        "route_work_units": 0,
        "policy_projection_visits": 0,
        "generated_candidate_bytes": 0,
        "rejection_record_bytes": 0,
        "transient_result_bytes": 0,
        "admitted_candidates": 0,
        "duplicate_candidates": 0,
        "rejected_columns": 0,
        "novel_retained_candidates": 0,
        "changed_selections": 0,
    }
    witness: dict[str, Any] = {
        "session_checksum": semantics["algorithm_session_checksum"],
        "board_content_hash": semantics["board_content_hash"],
        "workload_checksum": semantics["workload_checksum"],
        "capacity_model_checksum": semantics["capacity_model_checksum"],
        "preparation_checksum": semantics["preparation_checksum"],
        "maximum_regeneration_epochs": semantics["candidate_regeneration_epochs"],
        "terminal_reason": 0,
        "counters": counters,
        "epoch_record_count": 0,
        "epoch_association_checksum": 0,
        "final_pool_manifest_checksum": semantics["final_pool_manifest_checksum"],
        "final_rejection_manifest_checksum": semantics["final_rejection_manifest_checksum"],
    }
    session: dict[str, Any] = {
        "component_wall_nanoseconds": 0,
        "validation_and_source_inspection_wall_nanoseconds": 0,
        "initial_price_state_wall_nanoseconds": 0,
        "initial_selection_and_resource_accumulation_wall_nanoseconds": 0,
        "targeted_regeneration_planning_wall_nanoseconds": 0,
        "targeted_regeneration_price_update_wall_nanoseconds": 0,
        "targeted_regeneration_selection_and_target_planning_wall_nanoseconds": 0,
        "targeted_regeneration_execution_wall_nanoseconds": 0,
        "successor_correlation_wall_nanoseconds": 0,
        "terminal_multi_world_component_wall_nanoseconds": 0,
        "terminal_multi_world_price_update_wall_nanoseconds": 0,
        "final_manifest_and_assembly_wall_nanoseconds": 0,
        "unclassified_serial_wall_nanoseconds": 0,
        "planning_expanded_resource_visits": 0,
        "regeneration_plans": [],
        "regeneration_epochs": [],
        "terminal_multi_world": {
            "component_wall_nanoseconds": 0,
            "validation_and_source_preflight_wall_nanoseconds": 0,
            "selection_and_resource_accumulation_wall_nanoseconds": 0,
            "price_update_and_snapshot_wall_nanoseconds": 0,
            "terminal_retention_and_assembly_wall_nanoseconds": 0,
            "unclassified_serial_wall_nanoseconds": 0,
        },
        "replay_witness": witness,
    }
    witness["epoch_association_checksum"] = operational_validator._epoch_association(session)
    return session


def _profile(semantics: Mapping[str, Any], lifecycle: Mapping[str, Any]) -> dict[str, Any]:
    baseline = semantics["arm"] == 0
    preparation = None
    candidate_session = None
    baseline_components = None
    if baseline:
        baseline_components = {
            "component_wall_nanoseconds": 1,
            "validation_and_initialization_wall_nanoseconds": 0,
            "scheduling_and_policy_projection_wall_nanoseconds": 0,
            "candidate_generation_wall_nanoseconds": 0,
            "exact_admission_and_store_publication_wall_nanoseconds": 0,
            "incremental_resource_accumulation_wall_nanoseconds": 0,
            "sweep_selection_and_resource_replay_wall_nanoseconds": 0,
            "price_update_wall_nanoseconds": 0,
            "final_assembly_wall_nanoseconds": 0,
            "unclassified_serial_wall_nanoseconds": 1,
        }
    else:
        preparation = {
            "component_wall_nanoseconds": 1,
            "validation_and_scheduling_wall_nanoseconds": 0,
            "base_worker_wave_wall_nanoseconds": 1,
            "alternative_policy_wall_nanoseconds": 0,
            "alternative_worker_wave_wall_nanoseconds": 0,
            "route_and_candidate_build_worker_sum_nanoseconds": 1,
            "exact_admission_and_store_publication_wall_nanoseconds": 0,
            "publication_correlation_and_pool_materialization_wall_nanoseconds": 0,
            "unclassified_serial_wall_nanoseconds": 0,
            "base_jobs_dispatched": semantics["workload_net_count"],
            "alternative_jobs_dispatched": 0,
        }
        candidate_session = _candidate_session(semantics)
    result: dict[str, Any] = {
        "schema_version": 1,
        "execution": {
            "semantics": copy.deepcopy(semantics),
            "case_build_elapsed_nanoseconds": 1,
            "prepared_elapsed_nanoseconds": 1,
            "cold_elapsed_nanoseconds": 2,
            "preparer_lifecycle": copy.deepcopy(lifecycle),
        },
        "case_build": {
            "case_source": 0,
            "fixture_import_applicability": {"status": 1, "reason": 7},
            "synthetic_materialization_applicability": {"status": 0, "reason": 0},
            "compile_probe_applicability": {"status": 0, "reason": 0},
            "descriptor_validation_and_bound_preflight_wall_nanoseconds": 0,
            "fixture_identity_and_import_wall_nanoseconds": 0,
            "synthetic_geometry_and_board_materialization_wall_nanoseconds": 0,
            "geometry_compilation_probe_wall_nanoseconds": 0,
            "workload_geometry_compilation_wall_nanoseconds": 0,
            "capacity_and_case_assembly_wall_nanoseconds": 0,
            "component_wall_nanoseconds": 1,
            "unclassified_and_release_wall_nanoseconds": 1,
        },
        "process_cpu": {"status": 2, "reason": 10},
        "peak_host_memory": {"status": 2, "reason": 11},
        "compatible_batch_formation_and_fill": {"status": 1, "reason": 1},
        "compact_readback": {"status": 1, "reason": 2},
        "prepared_view_cache_and_cache_misses": {"status": 1, "reason": 3},
        "initial_device_upload": {"status": 1, "reason": 4},
        "gpu_utilization": {"status": 1, "reason": 5},
        "peak_device_memory": {"status": 1, "reason": 6},
        "contender_transient_release_tail_wall_nanoseconds": 0,
        "unclassified_prepared_scope_wall_nanoseconds": 0,
        "unclassified_cold_scope_exit_wall_nanoseconds": 0,
        "baseline": baseline_components,
        "preparation": preparation,
        "candidate_session": candidate_session,
        "profile_checksum": 0,
    }
    result["profile_checksum"] = operational_validator._profile_checksum(result)
    return result


def _authority(semantics: Mapping[str, Any], lifecycle: Mapping[str, Any]) -> dict[str, Any]:
    candidate_session = _candidate_session(semantics) if semantics["arm"] == 1 else None
    witness = (
        copy.deepcopy(candidate_session["replay_witness"])
        if candidate_session is not None
        else None
    )
    result: dict[str, Any] = {
        "schema_version": 1,
        "semantics": copy.deepcopy(semantics),
        "preparer_lifecycle": copy.deepcopy(lifecycle),
        "recomputed_full_preimage_session_checksum": semantics["algorithm_session_checksum"],
        "candidate_session_witness": witness,
        "authority_checksum": 0,
    }
    result["authority_checksum"] = operational_validator._authority_checksum(result)
    return result


def rehash_worker(worker: dict[str, Any]) -> None:
    """Recompute one synthetic worker's artifact and source envelopes."""
    payload_checksum_name = "profile_checksum" if worker["kind"] == 0 else "authority_checksum"
    worker["artifact_checksum"] = capture_tool._worker_artifact_checksum(
        worker["kind"],
        worker["compiler_identity"],
        worker["payload"][payload_checksum_name],
    )
    worker["source_envelope_checksum"] = capture_tool._worker_source_checksum(worker)


def _worker(
    *,
    kind: int,
    payload: Mapping[str, Any],
    raw: Mapping[str, Any],
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "schema_version": 1,
        "kind": kind,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "compiler_identity": _COMPILER_IDENTITY,
        "payload": copy.deepcopy(payload),
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    rehash_worker(result)
    return result


def _measured_process(arm: int, address_space_bytes: int) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "role": "measured_baseline" if arm == 0 else "measured_candidate",
        "controller_identity": _CONTROLLER_IDENTITY,
        "dispatch_ordinal": arm + 1,
        "process_instance_identity": 6_101 + arm,
        "configured_address_space_limit_bytes": address_space_bytes,
        "outer_wall_nanoseconds": 1,
        "user_cpu_nanoseconds": 1,
        "system_cpu_nanoseconds": 0,
        "total_cpu_nanoseconds": 1,
        "peak_host_bytes": 1,
        "raw_wait_status": 0,
        "process_exit_code": 0,
        "terminating_signal": 0,
        "watchdog_kill_sent": False,
        "isolated_exec": True,
        "measurement_scope": _MEASUREMENT_SCOPE,
    }


def _authority_process(arm: int, address_space_bytes: int) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "role": "authority_baseline" if arm == 0 else "authority_candidate",
        "controller_identity": _CONTROLLER_IDENTITY,
        "dispatch_ordinal": arm + 3,
        "process_instance_identity": 6_201 + arm,
        "configured_address_space_limit_bytes": address_space_bytes,
        "raw_wait_status": 0,
        "process_exit_code": 0,
        "terminating_signal": 0,
        "watchdog_kill_sent": False,
        "isolated_exec": True,
        "resource_measurements": {
            "status": "not_used",
            "reason": "unmeasured_full_preimage_authority_replay",
        },
    }


def _provenance(
    authority_module: ModuleType,
    worker_path: Any,
) -> dict[str, Any]:
    worker_digest = authority_module.sha256_file(worker_path)
    worker_stat = worker_path.stat()
    unavailable = {"status": "unavailable", "reason": "cgroup_file_unavailable"}
    result: dict[str, Any] = {
        "schema_version": 1,
        "publication_invocation": authority_module.TEST_INVOCATION,
        "bazel_release": "synthetic",
        "worker_target": authority_module.TEST_WORKER_TARGET,
        "worker_sha256": worker_digest,
        "worker_file_identity": {
            "device": worker_stat.st_dev,
            "inode": worker_stat.st_ino,
            "size_bytes": worker_stat.st_size,
            "mode": stat.S_IMODE(worker_stat.st_mode),
            "mtime_nanoseconds": worker_stat.st_mtime_ns,
            "sha256": worker_digest,
        },
        "compiler_identity": _COMPILER_IDENTITY,
        "cplusplus_standard": "c++20",
        "python_implementation": "CPython",
        "python_version": "synthetic",
        "toolchain_files": [
            {"name": "bazel_version_file", "sha256": "1" * 64, "size_bytes": 1},
            {"name": "bazel_configuration", "sha256": "2" * 64, "size_bytes": 1},
            {"name": "module_definition", "sha256": "3" * 64, "size_bytes": 1},
            {"name": "module_lock", "sha256": "4" * 64, "size_bytes": 1},
        ],
        "host": {
            "os": "Linux",
            "kernel": "synthetic",
            "architecture": "x86_64",
            "os_release_id": "synthetic",
            "os_release_version": "1",
            "os_release_sha256": "5" * 64,
            "libc": ["synthetic", "1"],
            "cpu_vendor": "synthetic",
            "cpu_model_name": "synthetic",
            "cpu_family": "synthetic",
            "cpu_model": "synthetic",
            "cpu_stepping": "synthetic",
            "cpu_microcode": "synthetic",
            "online_cpu_count": 1,
            "affinity_cpu_ids": [0],
            "affinity_cpu_count": 1,
            "page_size_bytes": 4_096,
            "total_host_memory_bytes": 1,
            "cgroup_namespace_identity": {
                "device": 1,
                "inode": 1,
                "symlink_target": "cgroup:[1]",
            },
            "cgroup_mount_identity": {
                "device": 1,
                "inode": 1,
                "symlink_target": "",
            },
            "cgroup_ancestry_scope": "namespace_visible_unified_v2_leaf_to_root",
            "cgroup_unified_path": "/",
            "cgroup_cpu_max_ancestry": [{"path": "/", "control": copy.deepcopy(unavailable)}],
            "cgroup_cpuset_effective": copy.deepcopy(unavailable),
            "cgroup_memory_max_ancestry": [{"path": "/", "control": copy.deepcopy(unavailable)}],
            "monotonic_clock": "CLOCK_MONOTONIC",
        },
        "backend": "cpu_only",
        "gpu": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_gpu_dispatch",
        },
        "device_memory": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_device_memory",
        },
        "initial_device_upload": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_device_upload",
        },
        "compact_readback": {
            "status": "not_applicable",
            "reason": "cpu_execution_has_no_compact_device_readback",
        },
        "compatible_batch_fill": {
            "status": "not_applicable",
            "reason": "cpu_direct_jobs_not_compatibility_batches",
        },
        "prepared_view_cache": {
            "status": "not_applicable",
            "reason": "precompiled_case_owned_views_have_no_runtime_prepared_view_cache",
        },
        "provenance_checksum": 0,
    }
    rehash_provenance(result)
    return result


def rehash_provenance(provenance: dict[str, Any]) -> None:
    """Recompute one synthetic reproducibility-provenance checksum."""
    payload = {key: item for key, item in provenance.items() if key != "provenance_checksum"}
    hashed = raw_validator.StableHashBuilder()
    hashed.string("APGAR-PHASE4-OPERATIONAL-REPRODUCIBILITY-PROVENANCE-V1")
    hashed.string(capture_tool._canonical(payload))
    provenance["provenance_checksum"] = hashed.finish()


def rehash_capture(capture: dict[str, Any]) -> None:
    """Recompute provenance, artifact, and source checksums after a test mutation."""
    rehash_provenance(capture["reproducibility_provenance"])
    capture["artifact_checksum"] = capture_tool._artifact_checksum(capture)
    capture["source_envelope_checksum"] = capture_tool._source_checksum(capture)


def make_capture(raw: Mapping[str, Any], authority_module: ModuleType) -> dict[str, Any]:
    """Build one four-process operational capture matching Raw repetition zero."""
    worker_path = authority_module.resolve_bundled_worker(authority_module.TEST_WORKER)
    pair = raw["attempts"][0]["result"]
    address_space_bytes = raw["config"]["external_budget"]["maximum_address_space_bytes"]
    arms = []
    for arm, name in enumerate(("baseline", "candidate")):
        record = pair[name]
        semantics = record["semantics"]
        lifecycle = record["preparer_lifecycle"]
        profile = _profile(semantics, lifecycle)
        replay_authority = _authority(semantics, lifecycle)
        arms.append(
            {
                "arm": arm,
                "measured_process": _measured_process(arm, address_space_bytes),
                "measured_worker": _worker(kind=0, payload=profile, raw=raw),
                "authority_process": _authority_process(arm, address_space_bytes),
                "authority_worker": _worker(kind=1, payload=replay_authority, raw=raw),
            }
        )
    result: dict[str, Any] = {
        "schema_version": 1,
        "source_commit": raw["source_commit"],
        "source_stamped": raw["source_stamped"],
        "source_tree_dirty": raw["source_tree_dirty"],
        "standalone_publication_eligible": False,
        "cell_operational_capture_complete": True,
        "controller_identity": _CONTROLLER_IDENTITY,
        "capture_run_identity": _CAPTURE_RUN_IDENTITY,
        "cell_config": copy.deepcopy(raw["config"]),
        "reproducibility_provenance": _provenance(authority_module, worker_path),
        "arms": arms,
        "artifact_checksum": 0,
        "source_envelope_checksum": 0,
    }
    rehash_capture(result)
    return result


def make_operational_inputs(
    *,
    case_id: int,
    pool: int,
    same_run: bool,
    h4096: bool,
    authority_module: ModuleType,
    commit: str = "a" * 40,
) -> tuple[dict[str, Any], dict[str, Any] | None, dict[str, Any]]:
    """Build clean Raw, optional telemetry, and a matching operational capture."""
    raw = raw_artifacts.make_raw(
        case_id,
        pool,
        same_run=same_run,
        h4096=h4096,
        repetitions=20,
        canonical_confirmatory_caps=True,
    )
    raw_artifacts.mark_raw_clean(raw, commit)
    sidecar = raw_artifacts.make_sidecar(raw) if same_run else None
    return raw, sidecar, make_capture(raw, authority_module)
