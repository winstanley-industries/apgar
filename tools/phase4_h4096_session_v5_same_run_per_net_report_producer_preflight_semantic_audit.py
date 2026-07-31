"""Hermetically audit the ADR-067 same-run report-producer boundary."""

from __future__ import annotations

import argparse
import copy
import fnmatch
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from collections import Counter
from collections.abc import Iterable, Iterator, Mapping, Sequence
from typing import Any

_PRODUCER_SOURCE = (
    "src/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_"
    "producer_preflight.cc"
)
_PRODUCER_HEADER = (
    "src/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_"
    "producer_preflight_internal.h"
)
_DIRECT_TEST_SOURCE = (
    "tests/benchmark/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.cc"
)
_RUNNER_SOURCE = (
    "tools/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_"
    "preflight_test_runner.cc"
)
_TEST_SUPPORT_HEADER = "tests/support/google_test.h"

_SOURCE_SHA256 = {
    _PRODUCER_SOURCE: "948dace7d774ab6bbbbdb90974caa498dbbcca88665cf7d0a97a86ac326b7b7e",
    _PRODUCER_HEADER: "ade6378eae302684e209659f275d5c877b325f1ef61ef59107cf84cbc360a32b",
    _DIRECT_TEST_SOURCE: "26cae7db19da08255b55afbfd3ce63a92f2a9703d47958b535d5aa557b35f941",
    _TEST_SUPPORT_HEADER: "d910ace7f6876c62f345109880725b4daa874d1c547e4e68a5aba24e952937df",
    _RUNNER_SOURCE: "74f5cd630c42ffd817025e2001214415d71fd87151e6e46ff4aec149cb74a991",
}

_IDENTITY = "Phase4H4096SessionV5SameRunPerNetReportProducerIdentity"
_BUILDER = "BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity"
_COMPOSITE = "PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer"
_CONTROLLER = "PreflightPhase4ConfirmatoryH4096SessionV5Controller"
_PREFLIGHT_BUILDER = "BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity"
_CELL_BUILDER = "BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell"

_FIELD_TYPES = (
    ("identity_schema_version", "std::uint32_t"),
    ("execution_authority", "Phase4TrialExecutionAuthority"),
    ("configuration_authority", "std::string_view"),
    ("superseded_configuration_authority", "std::string_view"),
    ("protocol_authority", "std::string_view"),
    ("protocol_checksum", "std::uint64_t"),
    ("superseded_protocol_authority", "std::string_view"),
    ("superseded_protocol_checksum", "std::uint64_t"),
    ("budget_roster_authority", "std::string_view"),
    ("budget_roster_checksum", "std::uint64_t"),
    ("superseded_budget_roster_authority", "std::string_view"),
    ("superseded_budget_roster_checksum", "std::uint64_t"),
    ("corpus_version", "std::uint32_t"),
    ("corpus_checksum", "std::uint64_t"),
    ("representative_manifest_schema_version", "std::uint32_t"),
    ("representative_manifest_checksum", "std::uint64_t"),
    ("workload_roster_manifest_schema_version", "std::uint32_t"),
    ("workload_roster_manifest_checksum", "std::uint64_t"),
    ("raw_authority", "std::string_view"),
    ("superseded_raw_authority", "std::string_view"),
    ("retained_raw_predecessor_authority", "std::string_view"),
    ("telemetry_required", "bool"),
    ("telemetry_authority", "std::string_view"),
    ("superseded_telemetry_authority", "std::string_view"),
    ("retained_telemetry_predecessor_authority", "std::string_view"),
    ("report_authority", "std::string_view"),
    ("superseded_report_authority", "std::string_view"),
    ("retained_report_predecessor_authority", "std::string_view"),
    ("case_id", "std::uint32_t"),
    ("requested_pool_size", "std::uint32_t"),
    ("preparation_worker_count", "std::uint32_t"),
    ("repetitions", "std::uint32_t"),
    ("carrier", "Phase4H4096SessionV5Carrier"),
    ("raw_schema_version", "std::uint32_t"),
    ("raw_wire_schema_version", "std::uint32_t"),
    ("raw_pair_attempt_count", "std::uint32_t"),
    ("raw_arm_attempt_count", "std::uint32_t"),
    ("telemetry_schema_version", "std::uint32_t"),
    ("telemetry_wire_schema_version", "std::uint32_t"),
    ("telemetry_pair_capture_count", "std::uint32_t"),
    ("telemetry_arm_capture_count", "std::uint32_t"),
    ("telemetry_workload_net_count_per_arm", "std::uint32_t"),
    ("telemetry_total_per_net_row_count", "std::uint32_t"),
    ("report_schema_version", "std::uint32_t"),
    ("report_raw_wire_schema_version", "std::uint32_t"),
    ("report_reference_repetition", "std::uint32_t"),
    ("report_reference_execution_order", "Phase4TrialOrder"),
    ("report_arm_count", "std::uint32_t"),
    ("workload_net_count_per_arm", "std::uint32_t"),
    ("total_report_per_net_row_count", "std::uint32_t"),
    ("workload_net_roster_checksum", "std::uint64_t"),
    ("report_decision_eligible", "bool"),
    ("candidate_session_schema_version", "std::uint32_t"),
    ("targeted_regeneration_plan_schema_version", "std::uint32_t"),
    ("targeted_regeneration_execution_schema_version", "std::uint32_t"),
    ("baseline_present_step_per_overuse_unit", "std::uint64_t"),
    ("baseline_history_step_per_overuse_unit", "std::uint64_t"),
    ("candidate_present_step_per_overuse_unit", "std::uint64_t"),
    ("candidate_history_step_per_overuse_unit", "std::uint64_t"),
    ("canonical_algorithm_budget_checksum", "std::uint64_t"),
    ("paired_semantic_budget_checksum", "std::uint64_t"),
)
_FIELDS = tuple(name for name, _ in _FIELD_TYPES)

_TEST_CASES = (
    "CanonicalBuilderBindsEveryFrozenIdentityFieldExactly",
    "EveryIdentityFieldIndependentlyRejectsBeforeSourceValidationOrActivation",
    "OrdinaryPredecessorBudgetArtifactAndCardinalityProfilesCrossRejectAtomically",
    "IdentityThenSourceThenCanonicalActivationOrderIsExact",
)
_TEST_SUITE = "Phase4H4096SessionV5SameRunPerNetReportProducerPreflightTest"

_IGNORELIST_SHA256 = "46f903f898f41e3424c773b9de9efc659cc2de3a00cfb214c840ac96e2c22bf7"
_IGNORELIST_BYTES = (
    b"# Ignore all sanitizer instrumentation for libc startup files.\n"
    b"src:external/llvm+/3rd_party/libc/glibc/csu/elf-init-2.31.c\n"
    b"src:external/llvm++musl+musl_libc/src/env/__libc_start_main.c\n"
)
_NEGATIVE_CASES_SHA256 = "5ac2425ed1e546a6007853720d026d67003337af761b58dc2912d06448b8f37b"
_SENTINEL = "P4PAIR-UBSAN-LIVE-PROBE-SENTINEL"

# These are normalized Clang-22 JSON AST identities for the complete
# target-owned declaration surfaces.  Source positions and compiler-assigned
# declaration IDs are removed; declaration kinds, types, defaults, attributes,
# bodies, implicit edges, and call/reference identities remain frozen.
_PRODUCER_TU_FINGERPRINT = "0786529cd848adc5661ace1906d7634a060ca52702ccf1e615f56654804eff2f"
_DIRECT_TEST_TU_FINGERPRINTS = {
    "normal": "fc43d79c63d525e33f6701efa8dbf1d9c59dc9d69f39ef8639b2339e86b14558",
    "asan": "fc43d79c63d525e33f6701efa8dbf1d9c59dc9d69f39ef8639b2339e86b14558",
    "ubsan": "fc43d79c63d525e33f6701efa8dbf1d9c59dc9d69f39ef8639b2339e86b14558",
}
_RUNNER_NORMAL_TU_FINGERPRINT = "056ead9d61e85bfaf34587a9275aa69ba4e106764cf9fa8f89e01e6fe8d483fd"
_RUNNER_FORCED_TU_FINGERPRINT = "7087f3d7494d196a1b0f96e2d01fd92df32e6efc531a7cc8fc89a8e31d31038e"
_RUNNER_MAIN_FINGERPRINT = "39af8869376b87b9e9bdcf61d76a585ec9d75d372882f30953be97e7007a56b1"

_COMPILER = (
    "external/llvm++llvm_toolchain_minimal+llvm-toolchain-minimal-22.1.8-linux-amd64/bin/clang++"
)
_COMPILE_COMMAND_SHA256 = {
    ("production", "normal"): "4a75730a8cc3bf361707a8a09c738f2b684fe33378376d0c6662aa752af4ee3d",
    ("test_support", "normal"): "5aec69abe51523fea073be5103a0d90200b6d69d3ff5557d916cce74d7d63455",
    ("direct_test", "normal"): "7811075e137d31cd3d6a7dd05d0e9e767173cdd8d51686d5c4a7ca814a39fc5d",
    ("runner", "normal"): "4da8a09ef081ff547059d76fce454b0611b2e030d00543db09635deb663bd358",
    ("forced_runner", "normal"): "dc972d14163752fdfb1a6cb89b336981f580e74c0aeda62b51ac44b48e358741",
    ("test_support", "asan"): "d5ca7b9d93226a0ff4ede88ac2d4caf0391d801aafe5f96af8824c7aee8b2cb5",
    ("direct_test", "asan"): "7e1dd678748ecf89991a741f1ab10eda5acb97e8b7c91031d97949cf7bad59bb",
    ("test_support", "ubsan"): "66e54a927f659c8b69336dfa5228b685a07bb6bd0412ff88302e6d766fca4a6d",
    ("direct_test", "ubsan"): "4d61ef07859e8b5498866975e10bc47c640d9297a0de88398d2f3e2b6ff5971c",
}
_TRANSITIVE_HEADER_SHA256 = {
    "production": "7b5e76de25ed1460b7d2c9ea040fe4970c05b822ceb0f24b75cd856485fa2e8b",
    "test_support": "c13d91b837726239a40a7106bb2f20e9ab3b6f210ebd8df782e3cd7772d49eba",
    "direct_test": "3b179887df97d126c29da7e8e64bb0f5fd45194d704291d48e476b4e89838289",
    "runner": "7b5e76de25ed1460b7d2c9ea040fe4970c05b822ceb0f24b75cd856485fa2e8b",
    "forced_runner": "7b5e76de25ed1460b7d2c9ea040fe4970c05b822ceb0f24b75cd856485fa2e8b",
}
_LINK_CONTEXT_SHA256 = {
    "production": "e2f091ad758767c8ed9d0ca144a0b368cfb03ad31a4ffe61c3caaf7f34ed23ed",
    "test_support": "e80cbf264deec36c306060027b1e9dbf80258755f4eef384c9d691098b4aa825",
    "direct_test": "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
    "runner": "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
    "forced_runner": "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945",
}
_PREDEFINED_MACRO_SHA256 = {
    ("production", "normal"): "f6a69eec5565dc24d708537c686dd5261df8998daeb1186957522a4ddb9d73d7",
    ("test_support", "normal"): "f6a69eec5565dc24d708537c686dd5261df8998daeb1186957522a4ddb9d73d7",
    ("direct_test", "normal"): "2db7eb507115d5cba25a3bc2ee4ee4daa89566912556eaf741f0ccd9bc4a1a7a",
    ("runner", "normal"): "0ff284c17235ee4cd23de5161dae78038bdcda8a5fa22a356aea0e58f7246943",
    ("forced_runner", "normal"): "52bbde95a55a63af47849a032c81e3c32f2064f8bf00f2fbca1db6fd10333899",
    ("test_support", "asan"): "2f82e3e7fff44c826349c67b1da219ed40fa5440f16cbf5f7041b2adf050f4c4",
    ("direct_test", "asan"): "820c52c04eb4f24dbe341ba31fe4c8a6f85574f08c5e166621eb83f3c67d57f5",
    ("test_support", "ubsan"): "f6a69eec5565dc24d708537c686dd5261df8998daeb1186957522a4ddb9d73d7",
    ("direct_test", "ubsan"): "2db7eb507115d5cba25a3bc2ee4ee4daa89566912556eaf741f0ccd9bc4a1a7a",
}
_INCLUDE_GRAPH_SHA256 = {
    ("production", "normal"): "3e41af586a2084453438105ec750cb2f9bfd0c16623b9d8f943d193985380776",
    ("test_support", "normal"): "46813694b014c8f6775718a4fd208db7d2fab36086c6eee383e010d35e5be43f",
    ("direct_test", "normal"): "9fca20d01ec9c2bef8c4fa7fe9c4c834a0a4bcc85fc60141e13139a2ccdfddf6",
    ("runner", "normal"): "6d61a35bc5951742b958c681564ac19a6d5c6f4dafef6a6ef633287ad5a25ef5",
    ("forced_runner", "normal"): "6d61a35bc5951742b958c681564ac19a6d5c6f4dafef6a6ef633287ad5a25ef5",
    ("test_support", "asan"): "26f4fc172b1dfc7c11cba8483aef23c1381dfff1bb22470fdf8f1a8176ed1ef0",
    ("direct_test", "asan"): "9726f8a921e3c2bf96b486a821a92724ed5b2eef9372d1f418687a95a2c77ae8",
    ("test_support", "ubsan"): "13e3c5e2ce6c21113f529b0cea8452f034a2b606fcb26eccb39e0ad581675bdb",
    ("direct_test", "ubsan"): "946dc1455703d56350d6008bd3eed83459c4dbe6433adf3e95e1730a01f7e337",
}

_EXPECTED_CONFIGURATION_OPTIONS = {
    "action_env": [],
    "build_test_dwp": False,
    "cc_dotd_files": True,
    "cc_include_scanning": False,
    "collect_code_coverage": False,
    "compilation_mode": "fastbuild",
    "conlyopts": [],
    "copts": [],
    "cpu": "k8",
    "crosstool_top": "@@bazel_tools//tools/cpp:toolchain",
    "cs_fdo_absolute_path": None,
    "cs_fdo_instrument": None,
    "cs_fdo_profile": None,
    "custom_malloc": None,
    "cxxopts": ["-std=c++20"],
    "define": [],
    "disabled_features": [],
    "dynamic_mode": "default",
    "experimental_cc_implementation_deps": True,
    "experimental_cpp_modules": False,
    "experimental_inmemory_dotd_files": True,
    "experimental_save_feature_state": False,
    "experimental_unsupported_and_brittle_include_scanning": False,
    "experimental_use_llvm_covmap": False,
    "extra_execution_platforms": [],
    "extra_toolchains": [],
    "fdo_instrument": None,
    "fdo_optimize": None,
    "fdo_prefetch_hints": None,
    "fdo_profile": None,
    "features": [],
    "fission": [],
    "force_pic": False,
    "host_action_env": [],
    "host_compilation_mode": "opt",
    "host_conlyopts": [],
    "host_copts": [],
    "host_cpu": "k8",
    "host_cxxopts": [],
    "host_features": [],
    "host_linkopts": [],
    "host_per_file_copt": [],
    "host_platform": "@@bazel_tools//tools:host_platform",
    "incompatible_remove_legacy_whole_archive": True,
    "incompatible_strict_action_env": True,
    "incompatible_use_specific_tool_files": True,
    "interface_shared_objects": True,
    "legacy_whole_archive": True,
    "linkopts": [],
    "lto_backend_options": [],
    "lto_index_options": [],
    "memprof_profile": None,
    "per_file_copt": [],
    "per_file_lto_backend_options": [],
    "platforms": ["@@bazel_tools//tools:host_platform"],
    "process_headers_in_dependencies": False,
    "propeller_optimize": None,
    "propeller_optimize_absolute_cc_profile": None,
    "propeller_optimize_absolute_ld_profile": None,
    "proto_profile_path": None,
    "save_temps": False,
    "share_native_deps": True,
    "strip": "sometimes",
    "stripopts": [],
}

_POSITIVE_STAMP = "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-001"
_NEGATIVE_STAMP = "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-NEGATIVE-001"
_STARTUP_PROVENANCE_STAMP = (
    "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-STARTUP-PROVENANCE-001"
)

_VOLATILE_AST_KEYS = frozenset(
    {
        "id",
        "loc",
        "range",
        "previousDecl",
        "parentDeclContextId",
        "referencedMemberDecl",
        "mangledName",
        # Clang emits this as an in-process pointer for MaterializeTemporary
        # bookkeeping.  It is neither a declaration identity nor semantics.
        "temp",
    }
)


class AuditError(ValueError):
    """Raised when one ADR-067 semantic invariant fails closed."""


_VALIDATED_COMPILER_DRIVERS: set[str] = set()
_VALIDATED_COMPILER_CONTEXTS: set[tuple[str, str, str]] = set()


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise AuditError(message)


def _read_json(path: pathlib.Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise AuditError(f"cannot read canonical JSON {path}: {error}") from error


def _objects(value: Any) -> Iterator[Mapping[str, Any]]:
    if isinstance(value, dict):
        yield value
        for child in value.get("inner", ()):  # Clang's structured child order.
            yield from _objects(child)


def _json_stream(text: str, subject: str) -> list[Mapping[str, Any]]:
    decoder = json.JSONDecoder()
    offset = 0
    result: list[Mapping[str, Any]] = []
    while offset < len(text):
        while offset < len(text) and text[offset].isspace():
            offset += 1
        if offset == len(text):
            break
        try:
            value, offset = decoder.raw_decode(text, offset)
        except json.JSONDecodeError as error:
            raise AuditError(f"{subject} emitted malformed structured AST: {error}") from error
        _require(isinstance(value, dict), f"{subject} emitted a non-object AST root")
        result.append(value)
    _require(result, f"{subject} emitted no structured AST")
    return result


def _fingerprint(values: Iterable[Any]) -> str:
    digest = hashlib.sha256()

    def add(value: Any) -> None:
        if isinstance(value, dict):
            digest.update(b"{")
            for key in sorted(value):
                if key in _VOLATILE_AST_KEYS or key.endswith("Id"):
                    continue
                digest.update(key.encode("utf-8"))
                digest.update(b":")
                add(value[key])
            digest.update(b"}")
        elif isinstance(value, list):
            digest.update(b"[")
            for item in value:
                add(item)
            digest.update(b"]")
        else:
            digest.update(json.dumps(value, sort_keys=True).encode("utf-8"))
            digest.update(b";")

    for value in values:
        add(value)
    return digest.hexdigest()


def _normalize_configuration_paths(value: Any) -> Any:
    if isinstance(value, str):
        return re.sub(r"bazel-out/[^/]+/", "bazel-out/<CONFIG>/", value)
    if isinstance(value, list):
        return [_normalize_configuration_paths(item) for item in value]
    if isinstance(value, dict):
        return {key: _normalize_configuration_paths(item) for key, item in value.items()}
    return value


def _canonical_digest(value: Any) -> str:
    encoded = json.dumps(
        _normalize_configuration_paths(value),
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _semantic_base_arguments(arguments: Sequence[str]) -> list[str]:
    result: list[str] = []
    skip_next = False
    for argument in arguments:
        if skip_next:
            skip_next = False
            continue
        if argument in {"-o", "-MF", "-MT", "-MQ"}:
            skip_next = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-fcolor-diagnostics"}:
            continue
        if argument.startswith("-frandom-seed="):
            continue
        result.append(argument)
    return result


def _semantic_arguments(arguments: Sequence[str], ast_filter: str) -> list[str]:
    result = _semantic_base_arguments(arguments)
    result.extend(
        (
            "-fsyntax-only",
            "-Xclang",
            "-ast-dump=json",
            "-Xclang",
            f"-ast-dump-filter={ast_filter}",
        )
    )
    return result


def _validate_compiler_driver(compiler: str, environment: Mapping[str, str]) -> None:
    if compiler in _VALIDATED_COMPILER_DRIVERS:
        return
    completed = subprocess.run(
        [compiler, "--version"],
        capture_output=True,
        check=False,
        text=True,
        encoding="utf-8",
        errors="strict",
        env=dict(environment),
        timeout=30,
    )
    lines = completed.stdout.splitlines()
    _require(
        completed.returncode == 0
        and not completed.stderr
        and lines[:3]
        == [
            "clang version 22.1.8None",
            "Target: x86_64-unknown-linux-gnu",
            "Thread model: posix",
        ],
        "pinned Clang driver version/target identity drifted",
    )
    _VALIDATED_COMPILER_DRIVERS.add(compiler)


def _projection_digest(output: str) -> str:
    normalized = re.sub(r"bazel-out/[^/]+/", "bazel-out/<CONFIG>/", output)
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def _validate_compiler_projections(
    role: str,
    inventory: Mapping[str, Any],
    configuration: str,
    command_digest: str,
) -> None:
    cache_key = (role, configuration, command_digest)
    if cache_key in _VALIDATED_COMPILER_CONTEXTS:
        return
    command = inventory["commands"][0]
    compiler = str(inventory["compiler"])
    environment = dict(command["environment"])
    _validate_compiler_driver(compiler, environment)
    base_arguments = _semantic_base_arguments(command["arguments"])
    projections = (
        (
            "predefined macro",
            ["-dM", "-E"],
            _PREDEFINED_MACRO_SHA256.get((role, configuration)),
        ),
        (
            "include graph",
            ["-M", "-MT", "ADR067-INCLUDE-GRAPH"],
            _INCLUDE_GRAPH_SHA256.get((role, configuration)),
        ),
    )
    for subject, extra_arguments, expected_digest in projections:
        _require(
            expected_digest is not None,
            f"{role} lacks a frozen {configuration} {subject} identity",
        )
        completed = subprocess.run(
            [compiler, *base_arguments, *extra_arguments],
            capture_output=True,
            check=False,
            text=True,
            encoding="utf-8",
            errors="strict",
            env=environment,
            timeout=90,
        )
        _require(
            completed.returncode == 0 and not completed.stderr,
            f"{role} {subject} projection failed: {completed.stderr[:2000]}",
        )
        observed_digest = _projection_digest(completed.stdout)
        _require(
            observed_digest == expected_digest,
            f"{role} exact {subject} identity drifted: {observed_digest}",
        )
    _VALIDATED_COMPILER_CONTEXTS.add(cache_key)


def _dump_ast(inventory: Mapping[str, Any], ast_filter: str) -> list[Mapping[str, Any]]:
    commands = inventory["commands"]
    _require(len(commands) == 1, f"{inventory['label']} must compile exactly one translation unit")
    command = commands[0]
    compiler = inventory["compiler"]
    _require(
        isinstance(compiler, str)
        and compiler.endswith("/bin/clang++")
        and "llvm_toolchain_minimal" in compiler,
        f"{inventory['label']} did not resolve the pinned LLVM clang++ front end",
    )
    environment = dict(command.get("environment", {}))
    completed = subprocess.run(
        [compiler, *_semantic_arguments(command["arguments"], ast_filter)],
        capture_output=True,
        check=False,
        text=True,
        encoding="utf-8",
        errors="strict",
        env=environment,
        timeout=90,
    )
    _require(
        completed.returncode == 0 and not completed.stderr,
        f"{inventory['label']} Clang semantic parse failed: {completed.stderr[:4000]}",
    )
    return _json_stream(completed.stdout, f"{inventory['label']}:{ast_filter}")


def _dump_mutant_ast(
    inventory: Mapping[str, Any],
    source: str,
    ast_filter: str,
) -> tuple[list[Mapping[str, Any]], pathlib.Path]:
    commands = inventory["commands"]
    _require(len(commands) == 1, "negative semantic mutant lost its compile command")
    command = commands[0]
    suffix = pathlib.PurePosixPath(str(command["source"])).suffix or ".cc"
    with tempfile.TemporaryDirectory(prefix="apgar-adr067-semantic-mutant-") as directory:
        mutant_path = pathlib.Path(directory) / f"semantic_mutant{suffix}"
        mutant_path.write_text(source, encoding="utf-8", newline="\n")
        arguments = [
            str(mutant_path) if argument == command["source"] else argument
            for argument in _semantic_arguments(command["arguments"], ast_filter)
        ]
        completed = subprocess.run(
            [inventory["compiler"], *arguments],
            capture_output=True,
            check=False,
            text=True,
            encoding="utf-8",
            errors="strict",
            env=dict(command.get("environment", {})),
            timeout=90,
        )
        _require(
            completed.returncode == 0 and not completed.stderr,
            "ADR067-NEGATIVE-MUTANT-COMPILE: concrete semantic mutant did not compile "
            f"diagnostic-free: {completed.stderr[:4000]}",
        )
        return (
            _json_stream(completed.stdout, f"negative-mutant:{ast_filter}"),
            mutant_path,
        )


def _dump_mutant_producer_ast(
    inventory: Mapping[str, Any],
    source: str,
    header: str,
    ast_filter: str,
) -> tuple[list[Mapping[str, Any]], pathlib.Path, pathlib.Path]:
    commands = inventory["commands"]
    _require(len(commands) == 1, "negative producer mutant lost its compile command")
    command = commands[0]
    with tempfile.TemporaryDirectory(prefix="apgar-adr067-producer-mutant-") as directory:
        mutant_root = pathlib.Path(directory)
        mutant_header = mutant_root / pathlib.PurePosixPath(_PRODUCER_HEADER).name
        mutant_source = mutant_root / pathlib.PurePosixPath(str(command["source"])).name
        mutant_header.write_text(header, encoding="utf-8", newline="\n")
        canonical_include = f'#include "{_PRODUCER_HEADER}"'
        _require(
            source.count(canonical_include) == 1,
            "ADR067-NEGATIVE-MUTANT-COMPILE: producer include identity drifted",
        )
        rewritten_source = source.replace(
            canonical_include,
            f'#include "{mutant_header.as_posix()}"',
            1,
        )
        mutant_source.write_text(rewritten_source, encoding="utf-8", newline="\n")
        arguments = [
            str(mutant_source) if argument == command["source"] else argument
            for argument in _semantic_arguments(command["arguments"], ast_filter)
        ]
        completed = subprocess.run(
            [inventory["compiler"], *arguments],
            capture_output=True,
            check=False,
            text=True,
            encoding="utf-8",
            errors="strict",
            env=dict(command.get("environment", {})),
            timeout=90,
        )
        _require(
            completed.returncode == 0 and not completed.stderr,
            "ADR067-NEGATIVE-MUTANT-COMPILE: concrete producer header/source mutant "
            f"did not compile diagnostic-free: {completed.stderr[:4000]}",
        )
        return (
            _json_stream(completed.stdout, f"negative-producer-mutant:{ast_filter}"),
            mutant_header,
            mutant_source,
        )


def _function_definitions(
    roots: Iterable[Mapping[str, Any]], name: str
) -> tuple[list[Mapping[str, Any]], Mapping[str, Any]]:
    declarations = [
        root
        for root in roots
        if root.get("kind") in {"FunctionDecl", "CXXMethodDecl"} and root.get("name") == name
    ]
    definitions = [
        declaration
        for declaration in declarations
        if any(child.get("kind") == "CompoundStmt" for child in declaration.get("inner", ()))
    ]
    _require(
        len(declarations) == 2,
        f"{name} must have one declaration and one redeclaration; got {len(declarations)}",
    )
    _require(len(definitions) == 1, f"{name} must have exactly one target-owned definition")
    _require(
        definitions[0].get("previousDecl") == declarations[0].get("id"),
        f"{name} definition is outside its canonical Clang redeclaration chain",
    )
    return declarations, definitions[0]


def _body(function: Mapping[str, Any]) -> Mapping[str, Any]:
    bodies = [child for child in function.get("inner", ()) if child.get("kind") == "CompoundStmt"]
    _require(len(bodies) == 1, f"{function.get('name')} must have one body")
    return bodies[0]


def _referenced_names(value: Mapping[str, Any]) -> list[str]:
    return [
        str(node["referencedDecl"]["name"])
        for node in _objects(value)
        if node.get("kind") == "DeclRefExpr"
        and isinstance(node.get("referencedDecl"), dict)
        and "name" in node["referencedDecl"]
    ]


def _source_offset(value: Mapping[str, Any]) -> int | None:
    location: Any = value.get("loc") or value.get("range", {}).get("begin", {})
    while isinstance(location, dict):
        offset = location.get("offset")
        if isinstance(offset, int):
            return offset
        location = location.get("spellingLoc") or location.get("expansionLoc")
    return None


def _declaration_file(value: Mapping[str, Any]) -> str:
    location: Any = value.get("loc") or value.get("range", {}).get("begin", {})
    while isinstance(location, dict):
        file_name = location.get("file")
        if isinstance(file_name, str):
            return file_name.removeprefix("./")
        location = location.get("spellingLoc") or location.get("expansionLoc")
    return ""


def _call_callee_names(call: Mapping[str, Any]) -> list[str]:
    first = call.get("inner", ())[:1]
    return _referenced_names(first[0]) if first else []


def _calls(value: Mapping[str, Any]) -> list[Mapping[str, Any]]:
    return [
        node
        for node in _objects(value)
        if node.get("kind") in {"CallExpr", "CXXOperatorCallExpr", "CXXMemberCallExpr"}
    ]


def _unwrap_compiler_expression(value: Mapping[str, Any]) -> Mapping[str, Any]:
    allowed_wrappers = {
        "CXXBindTemporaryExpr",
        "ExprWithCleanups",
        "ImplicitCastExpr",
        "MaterializeTemporaryExpr",
    }
    current = value
    while current.get("kind") in allowed_wrappers:
        children = current.get("inner", ())
        _require(len(children) == 1, "compiler expression wrapper gained semantic children")
        current = children[0]
    return current


def _strip_comments_and_literals(source: str) -> str:
    pattern = re.compile(
        r"//[^\n]*|/\*.*?\*/|R\"[^\n]*?\(.*?\)[^\n]*?\"|\"(?:\\.|[^\"\\])*\"|"
        # A C++ digit separator (for example 4'963'299) is not a character
        # literal.  Requiring a non-identifier predecessor keeps brace
        # accounting intact across the frozen checksum constants.
        r"(?<![A-Za-z0-9_])'(?:\\.|[^'\\])*'",
        re.DOTALL,
    )
    return pattern.sub(lambda match: "\n" * match.group(0).count("\n"), source)


def _read_source(path: str) -> str:
    try:
        content = pathlib.Path(path).read_bytes()
    except (OSError, UnicodeError) as error:
        raise AuditError(f"cannot read audited source {path}: {error}") from error
    normalized_path = pathlib.PurePosixPath(path).as_posix().removeprefix("./")
    if normalized_path in _SOURCE_SHA256:
        _validate_frozen_source_bytes(normalized_path, content)
    try:
        return content.decode("utf-8")
    except UnicodeError as error:
        raise AuditError(f"audited source is not UTF-8 {path}: {error}") from error


def _validate_frozen_source_bytes(logical_path: str, content: bytes) -> None:
    expected_digest = _SOURCE_SHA256.get(logical_path)
    _require(expected_digest is not None, f"unrecognized target-owned source: {logical_path}")
    _require(
        hashlib.sha256(content).hexdigest() == expected_digest,
        f"audited target-owned source bytes drifted: {logical_path}",
    )


def _matching_brace(text: str, opening: int, subject: str) -> int:
    _require(opening < len(text) and text[opening] == "{", f"{subject} has no opening brace")
    depth = 0
    for offset in range(opening, len(text)):
        if text[offset] == "{":
            depth += 1
        elif text[offset] == "}":
            depth -= 1
            if depth == 0:
                return offset
    raise AuditError(f"{subject} has an unbalanced brace")


def _validate_compile_context(
    role: str, inventory: Mapping[str, Any], expected_configuration: str
) -> None:
    _require(inventory.get("schema_version") == 1, f"{role} inventory schema drifted")
    _require(
        inventory.get("configuration") == expected_configuration,
        f"{role} inventory configuration drifted",
    )
    commands = inventory.get("commands")
    _require(isinstance(commands, list) and len(commands) == 1, f"{role} compile inventory drifted")
    arguments = commands[0].get("arguments")
    _require(isinstance(arguments, list), f"{role} compile arguments are missing")
    flags = [argument for argument in arguments if isinstance(argument, str)]
    _require(inventory.get("compiler") == _COMPILER, f"{role} compiler identity drifted")
    _require(
        commands[0].get("environment")
        == {"PATH": "/bin:/usr/bin:/usr/local/bin", "PWD": "/proc/self/cwd"},
        f"{role} compile environment drifted",
    )
    _require(flags.count("-fPIC") == 1, f"{role} must compile exactly once in PIC mode")
    _require(
        not any(flag in {"-fno-pic", "-fno-PIC", "-fpie", "-fPIE"} for flag in flags),
        f"{role} has conflicting PIC semantics",
    )
    expected_command_digest = _COMPILE_COMMAND_SHA256.get((role, expected_configuration))
    _require(expected_command_digest is not None, f"{role} lacks a frozen compile command identity")
    observed_command_digest = _canonical_digest(_semantic_base_arguments(flags))
    _require(
        observed_command_digest == expected_command_digest,
        f"{role} normalized semantic compile command drifted: {observed_command_digest}",
    )
    _validate_compiler_projections(
        role,
        inventory,
        expected_configuration,
        observed_command_digest,
    )

    _validate_sanitizer_compile_flags(role, flags, expected_configuration)

    forbidden_fragments = (
        "no_sanitize",
        "disable_sanitizer_instrumentation",
        "-finstrument-functions-exclude",
        "-include-pch",
        "-fplugin",
        "-load",
    )
    for flag in flags:
        _require(
            not any(fragment in flag for fragment in forbidden_fragments),
            f"{role} has forbidden semantic compile flag {flag}",
        )


def _validate_sanitizer_compile_flags(
    role: str, flags: Sequence[str], expected_configuration: str
) -> None:
    sanitizer_flags = [flag for flag in flags if flag.startswith("-fsanitize=")]
    ignore_flags = [
        flag
        for flag in flags
        if "sanitize-ignorelist" in flag
        or "sanitize-blacklist" in flag
        or "sanitize-suppress" in flag
    ]
    recovery_flags = [flag for flag in flags if "sanitize-recover" in flag]
    disabling_flags = [
        flag
        for flag in flags
        if flag.startswith("-fno-sanitize=")
        or flag in {"-fno-sanitize-link-runtime", "-fno-sanitize-trap"}
    ]
    _require(not disabling_flags, f"{role} has sanitizer-disabling flags: {disabling_flags}")

    if expected_configuration == "normal":
        _require(not sanitizer_flags, f"{role} normal compilation is sanitizer-instrumented")
        _require(not ignore_flags, f"{role} normal compilation has an ignorelist")
        _require(not recovery_flags, f"{role} normal compilation has recovery controls")
    elif expected_configuration == "asan":
        _require(
            sanitizer_flags == ["-fsanitize=address"],
            f"{role} ASan instrumentation flags drifted: {sanitizer_flags}",
        )
        _require(not ignore_flags, f"{role} ASan compilation has a suppression list")
        _require(not recovery_flags, f"{role} ASan compilation has recovery controls")
    else:
        _require(
            sanitizer_flags == ["-fsanitize=undefined"],
            f"{role} UBSan instrumentation flags drifted: {sanitizer_flags}",
        )
        _require(
            recovery_flags == ["-fno-sanitize-recover=all"],
            f"{role} UBSan fail-live recovery flags drifted: {recovery_flags}",
        )
        _require(
            ignore_flags == ["-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt"],
            f"{role} UBSan ignorelist flag drifted: {ignore_flags}",
        )


def _validate_target_inventory(
    role: str, inventory: Mapping[str, Any], requested_configuration: str
) -> None:
    expected = {
        "production": {
            "label": "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
            "configuration": "normal",
            "sources": [_PRODUCER_SOURCE],
            "headers": [_PRODUCER_HEADER],
            "deps": ["@@//:phase4_h4096_session_v5_execution_preflight"],
            "output": "libphase4_h4096_session_v5_same_run_per_net_report_producer_preflight.a",
            "object": (
                "_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight/"
                "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight.pic.o"
            ),
        },
        "test_support": {
            "label": "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support",
            "configuration": requested_configuration,
            "sources": [_PRODUCER_SOURCE],
            "headers": [_PRODUCER_HEADER],
            "deps": ["@@//:phase4_h4096_session_v5_execution_preflight_test_support"],
            "output": "libphase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support.a",
            "object": (
                "_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support/"
                "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight.pic.o"
            ),
        },
        "direct_test": {
            "label": "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
            "configuration": requested_configuration,
            "sources": sorted([_DIRECT_TEST_SOURCE, _TEST_SUPPORT_HEADER]),
            "headers": [],
            "deps": [
                "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support",
                "@@googletest+//:gtest_main",
                "@@rules_cc+//:link_extra_lib",
            ],
            "output": "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
            "object": (
                "_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test/"
                "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.pic.o"
            ),
        },
        "runner": {
            "label": "//:phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
            "configuration": "normal",
            "sources": [_RUNNER_SOURCE],
            "headers": [],
            "deps": [
                "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
                "@@rules_cc+//:link_extra_lib",
            ],
            "output": "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
            "object": (
                "_objs/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner/"
                "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner.pic.o"
            ),
        },
        "forced_runner": {
            "label": "//:phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
            "configuration": "normal",
            "sources": [_RUNNER_SOURCE],
            "headers": [],
            "deps": [
                "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
                "@@rules_cc+//:link_extra_lib",
            ],
            "output": "phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
            "object": (
                "_objs/phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner/"
                "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner.pic.o"
            ),
        },
    }[role]
    observed_label = inventory.get("label")
    if isinstance(observed_label, str) and observed_label.startswith("@@//"):
        observed_label = observed_label[2:]
    _require(observed_label == expected["label"], f"{role} label inventory drifted")
    for key in ("sources", "headers"):
        _require(inventory.get(key) == expected[key], f"{role} {key} inventory drifted")
    _require(inventory.get("textual_headers") == [], f"{role} gained a textual header")
    _require(
        inventory.get("direct_dependency_labels") == expected["deps"],
        f"{role} direct dependency inventory drifted",
    )
    _require(
        inventory.get("requested_features") == [] and inventory.get("unsupported_features") == [],
        f"{role} feature configuration drifted",
    )
    _require(
        _canonical_digest(inventory.get("transitive_headers")) == _TRANSITIVE_HEADER_SHA256[role],
        f"{role} transitive header inventory drifted",
    )
    _require(
        _canonical_digest(inventory.get("link_context")) == _LINK_CONTEXT_SHA256[role],
        f"{role} CcInfo link context drifted",
    )
    _require(
        inventory.get("target_outputs") == [expected["output"]],
        f"{role} target output inventory drifted",
    )
    _require(
        inventory.get("actual_compile_outputs") == [expected["object"]],
        f"{role} actual native CppCompile output inventory drifted",
    )
    _require(
        _normalize_configuration_paths(inventory.get("target_output_paths"))
        == [f"bazel-out/<CONFIG>/bin/{expected['output']}"],
        f"{role} target output path drifted",
    )
    _validate_compile_context(role, inventory, expected["configuration"])

    attributes = inventory.get("attributes")
    _require(isinstance(attributes, dict), f"{role} attribute inventory is missing")
    common_attributes: dict[str, Any] = {
        "additional_compiler_inputs": [],
        "additional_linker_inputs": [],
        "alwayslink": False,
        "args": [],
        "data": [],
        "defines": [],
        "dynamic_deps": [],
        "env": {},
        "env_inherit": [],
        "features": [],
        "flaky": False,
        "implementation_deps": [],
        "include_prefix": "",
        "includes": [],
        "link_extra_lib": [],
        "linkopts": [],
        "linkstamp": [],
        "linkstatic": False,
        "local": False,
        "local_defines": [],
        "malloc": [],
        "nocopts": "",
        "runtime_deps": [],
        "shard_count": 0,
        "size": "",
        "stamp": -1,
        "strip_include_prefix": "",
        "tags": [],
        "target_compatible_with": [],
        "testonly": role != "production",
    }
    expected_attributes = dict(common_attributes)
    expected_attributes["copts"] = ["-Wall", "-Werror", "-Wextra", "-Wpedantic"]
    if role in {"production", "test_support"}:
        expected_attributes["copts"] += ["-fdata-sections", "-ffunction-sections"]
    if role in {"test_support", "direct_test"} and requested_configuration == "ubsan":
        expected_attributes["copts"] += ["-fno-sanitize-recover=all"]
    if role in {"direct_test", "runner", "forced_runner"}:
        expected_attributes.update(
            {
                "link_extra_lib": ["@@bazel_tools//tools/cpp:link_extra_lib"],
                "linkopts": ["-Wl,--gc-sections"],
                "linkstatic": True,
                "malloc": ["@@bazel_tools//tools/cpp:malloc"],
                "stamp": 0,
            }
        )
    if role == "direct_test":
        expected_attributes.update(
            {
                "env": (
                    {"UBSAN_OPTIONS": "halt_on_error=1"}
                    if requested_configuration == "ubsan"
                    else {}
                ),
                "shard_count": -1,
                "size": "small",
            }
        )
    if role == "forced_runner":
        expected_attributes["defines"] = [
            "APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING"
        ]
    _validate_runtime_environment(role, attributes, requested_configuration)
    _require(
        attributes == expected_attributes,
        f"{role} complete C++ rule attribute schema drifted: {attributes}",
    )


def _validate_runtime_environment(
    role: str, attributes: Mapping[str, Any], configuration: str
) -> None:
    expected_environment = (
        {"UBSAN_OPTIONS": "halt_on_error=1"}
        if role == "direct_test" and configuration == "ubsan"
        else {}
    )
    _require(
        attributes.get("env") == expected_environment,
        f"{role} exact sanitizer runtime environment drifted",
    )
    _require(
        attributes.get("env_inherit") == [],
        f"{role} inherited runtime environment drifted",
    )


def _validate_ignorelist(path: pathlib.Path, audited_sources: Iterable[str]) -> None:
    try:
        content = path.read_bytes()
    except OSError as error:
        raise AuditError(f"cannot read UBSan ignorelist {path}: {error}") from error
    _require(
        path.as_posix().endswith("external/llvm+/sanitizers/ubsan_ignore.txt"),
        "UBSan ignorelist path drifted",
    )
    _require(content == _IGNORELIST_BYTES, "UBSan ignorelist exact content/order drifted")
    _require(
        hashlib.sha256(content).hexdigest() == _IGNORELIST_SHA256, "UBSan ignorelist digest drifted"
    )
    _validate_ignorelist_semantics(content, audited_sources)


def _validate_ignorelist_semantics(content: bytes, audited_sources: Iterable[str]) -> None:
    entries = [
        line for line in content.decode("utf-8").splitlines() if line and not line.startswith("#")
    ]
    _require(all(entry.startswith("src:") for entry in entries), "UBSan ignorelist type drifted")
    patterns = [entry.removeprefix("src:") for entry in entries]
    _require(len(patterns) == 2, "UBSan ignorelist multiplicity drifted")
    for source in audited_sources:
        # No v1 marker is present, so LLVM SpecialCaseList v2 interprets each
        # expression with GlobPattern semantics.  In particular, '+' is a
        # literal and not a regular-expression quantifier.
        matching_patterns = [
            pattern for pattern in patterns if fnmatch.fnmatchcase(source, pattern)
        ]
        _require(
            not matching_patterns,
            f"UBSan ignorelist semantically suppresses audited source {source}: {matching_patterns}",
        )


def _validate_identity_equality_ownership(
    target_namespaces: Sequence[Mapping[str, Any]],
) -> None:
    records = [
        child
        for namespace in target_namespaces
        for child in namespace.get("inner", ())
        if child.get("kind") == "CXXRecordDecl"
        and child.get("name") == _IDENTITY
        and child.get("completeDefinition") is True
    ]
    _require(len(records) == 1, "producer must expose one complete identity RecordDecl")
    friends = [child for child in records[0].get("inner", ()) if child.get("kind") == "FriendDecl"]
    hidden_equalities = [
        child
        for friend in friends
        for child in friend.get("inner", ())
        if child.get("kind") == "FunctionDecl" and child.get("name") == "operator=="
    ]
    namespace_equalities = [
        child
        for namespace in target_namespaces
        for child in namespace.get("inner", ())
        if child.get("kind") == "FunctionDecl" and child.get("name") == "operator=="
    ]
    if namespace_equalities:
        _require(
            len(hidden_equalities) == 1
            and len(namespace_equalities) == 1
            and namespace_equalities[0].get("previousDecl") == hidden_equalities[0].get("id"),
            "ADR067-EQUALITY-REDECL-CHAIN: identity equality redeclaration chain drifted",
        )
        raise AuditError(
            "ADR067-EQUALITY-OUT-OF-LINE: identity equality escaped its hidden-friend "
            "definition owner"
        )
    _require(
        len(hidden_equalities) != 1 or hidden_equalities[0].get("previousDecl") is None,
        "ADR067-EQUALITY-OUT-OF-LINE: identity hidden-friend equality gained a prior "
        "declaration owner",
    )


def _validate_identity(root: Mapping[str, Any]) -> None:
    _require(root.get("kind") == "CXXRecordDecl", "identity is not a CXXRecordDecl")
    _require(root.get("name") == _IDENTITY, "identity declaration name drifted")
    _require(root.get("completeDefinition") is True, "identity definition is incomplete")
    _require(root.get("tagUsed") == "struct", "identity is not a public struct aggregate")
    definition = root.get("definitionData", {})
    _require(definition.get("isAggregate") is True, "identity is no longer an aggregate")
    _require(not root.get("bases"), "identity gained a base class")
    fields = [
        (child.get("name"), child.get("type", {}).get("qualType"))
        for child in root.get("inner", ())
        if child.get("kind") == "FieldDecl"
    ]
    _require(fields == list(_FIELD_TYPES), "identity 61-field name/type/order inventory drifted")
    field_nodes = [child for child in root.get("inner", ()) if child.get("kind") == "FieldDecl"]
    _require(
        all(
            not field.get("isBitfield")
            and not field.get("isMutable")
            and not any(
                child.get("kind") in {"NoUniqueAddressAttr", "AlignedAttr", "PackedAttr"}
                for child in field.get("inner", ())
            )
            for field in field_nodes
        ),
        "identity gained a bit-field, mutable field, or layout-changing attribute",
    )
    string_fields = {name for name, type_name in _FIELD_TYPES if type_name == "std::string_view"}
    initialized_fields = {
        field.get("name") for field in field_nodes if field.get("hasInClassInitializer") is True
    }
    _require(
        initialized_fields == set(_FIELDS) - string_fields,
        "identity default-initializer presence drifted",
    )
    _require(
        not any(child.get("kind") == "VarDecl" for child in root.get("inner", ())),
        "identity gained static state",
    )

    friends = [child for child in root.get("inner", ()) if child.get("kind") == "FriendDecl"]
    _require(len(friends) == 1, "identity must expose exactly one hidden friend")
    operators = [
        child
        for child in friends[0].get("inner", ())
        if child.get("kind") == "FunctionDecl" and child.get("name") == "operator=="
    ]
    _require(len(operators) == 1, "identity hidden-friend equality multiplicity drifted")
    equality = operators[0]
    _require(equality.get("explicitlyDefaulted") == "default", "identity equality is not defaulted")
    _require(equality.get("constexpr") is True, "identity equality lost constexpr semantics")
    _require(
        str(equality.get("type", {}).get("qualType", "")).endswith(") noexcept"),
        "identity equality lost noexcept semantics",
    )
    parameters = [
        child for child in equality.get("inner", ()) if child.get("kind") == "ParmVarDecl"
    ]
    expected_parameter = f"const {_IDENTITY} &"
    _require(
        [parameter.get("type", {}).get("qualType") for parameter in parameters]
        == [expected_parameter, expected_parameter],
        "identity equality signature drifted",
    )
    explicit_callables = [
        child
        for child in root.get("inner", ())
        if child.get("kind")
        in {"CXXConstructorDecl", "CXXDestructorDecl", "CXXMethodDecl", "FunctionDecl"}
        and not child.get("isImplicit")
    ]
    _require(not explicit_callables, "identity gained a method or lifecycle declaration")


def _validate_builder(function: Mapping[str, Any], source: str) -> None:
    body = _body(function)
    statements = body.get("inner", ())
    _require(
        len(statements) == 1 and statements[0].get("kind") == "ReturnStmt",
        "identity builder must remain one pure return",
    )
    initializers = [node for node in _objects(statements[0]) if node.get("kind") == "InitListExpr"]
    _require(len(initializers) == 1, "identity builder must construct one aggregate")
    _require(
        len(initializers[0].get("inner", ())) == 61,
        "identity builder aggregate does not initialize exactly 61 fields",
    )
    designators = re.findall(r"\.([a-z][a-z0-9_]*)\s*=", source)
    _require(
        designators[:61] == list(_FIELDS),
        "identity builder explicit designator order/completeness drifted",
    )
    callables = [
        call
        for call in _calls(function)
        if any(
            node.get("kind") in {"FunctionDecl", "CXXMethodDecl"}
            for node in _objects(call)
            if isinstance(node, dict)
        )
    ]
    # Trivial string_view construction is represented as CXXConstructExpr, not
    # as a capability-bearing CallExpr.
    _require(not callables, "identity builder gained a callable capability edge")


def _single_nested_call(argument: Mapping[str, Any], name: str) -> Mapping[str, Any]:
    matching = [call for call in _calls(argument) if name in _call_callee_names(call)]
    _require(len(matching) == 1, f"controller argument must be the direct {name} result")
    return matching[0]


def _validate_direct_function_callee(call: Mapping[str, Any], name: str) -> None:
    children = call.get("inner", ())
    _require(children, f"{name} call has no callee expression")
    callee = children[0]
    _require(
        callee.get("kind") == "ImplicitCastExpr"
        and callee.get("castKind") == "FunctionToPointerDecay",
        f"{name} call is indirect or wrapped",
    )
    callee_children = callee.get("inner", ())
    _require(
        len(callee_children) == 1 and callee_children[0].get("kind") == "DeclRefExpr",
        f"{name} call lost its direct declaration reference",
    )
    referenced = callee_children[0].get("referencedDecl")
    _require(
        isinstance(referenced, dict)
        and referenced.get("kind") == "FunctionDecl"
        and referenced.get("name") == name,
        f"{name} call resolved to an unexpected declaration",
    )


def _validate_composite(function: Mapping[str, Any]) -> None:
    body = _body(function)
    statements = body.get("inner", ())
    calls = _calls(function)
    controller_calls = [call for call in calls if _CONTROLLER in _call_callee_names(call)]
    _require(
        len(controller_calls) == 1,
        "ADR067-COMPOSITE-CONTROLLER-MULTIPLICITY: composite controller call multiplicity drifted",
    )
    controller_call = controller_calls[0]
    controller_statement_indexes = [
        index
        for index, statement in enumerate(statements)
        if any(candidate is controller_call for candidate in _calls(statement))
    ]
    _require(
        len(controller_statement_indexes) == 1,
        "ADR067-COMPOSITE-CONTROLLER-OWNER: controller call statement ownership drifted",
    )
    controller_statement_index = controller_statement_indexes[0]
    if (
        controller_statement_index != len(statements) - 1
        or statements[controller_statement_index].get("kind") != "ReturnStmt"
    ):
        later_nonreturn = any(
            statement.get("kind") != "ReturnStmt"
            for statement in statements[controller_statement_index + 1 :]
        )
        if later_nonreturn:
            raise AuditError(
                "ADR067-COMPOSITE-CONTROLLER-CONTINUATION: composite executes code after "
                "the shared controller call"
            )
        raise AuditError(
            "ADR067-COMPOSITE-CONTROLLER-NONTERMINAL: controller result is not its direct "
            "terminal return"
        )
    returned = statements[controller_statement_index].get("inner", ())
    _require(len(returned) == 1, "composite final return expression drifted")
    _require(
        not any(node.get("kind") == "ConditionalOperator" for node in _objects(returned[0])),
        "ADR067-COMPOSITE-CONTROLLER-CONDITIONAL: controller call is conditionally selected",
    )
    _require(
        _unwrap_compiler_expression(returned[0]) is controller_call,
        "composite controller call is not its direct final return",
    )
    _validate_direct_function_callee(controller_call, _CONTROLLER)
    _require(
        [statement.get("kind") for statement in statements] == ["IfStmt", "ReturnStmt"],
        "composite must contain its rejection branch followed by one final return",
    )
    arguments = controller_call.get("inner", ())[1:]
    _require(len(arguments) == 3, "composite controller argument count drifted")
    first = _single_nested_call(arguments[0], _PREFLIGHT_BUILDER)
    second = _single_nested_call(arguments[1], _CELL_BUILDER)
    _require(
        _referenced_names(first).count("kSameRun") == 1
        and _referenced_names(first).count("kController") == 1,
        "preflight identity argument lost literal kSameRun/kController",
    )
    _require(
        _referenced_names(second).count("kSameRun") == 1,
        "canonical-cell argument lost literal kSameRun",
    )
    third = arguments[2]
    third_reference = third.get("referencedDecl")
    _require(
        third.get("kind") == "DeclRefExpr"
        and isinstance(third_reference, dict)
        and third_reference.get("kind") == "ParmVarDecl"
        and third_reference.get("name") == "source"
        and third_reference.get("type", {}).get("qualType")
        == "const Phase4H4096SessionV5ControllerSourceAssociation &",
        "ADR067-COMPOSITE-SOURCE-OWNER: composite no longer directly forwards its original "
        "source parameter",
    )
    names = _referenced_names(function)
    _require("kOrdinary" not in names, "same-run composite references kOrdinary")
    _require(names.count(_CONTROLLER) == 1, "composite controller reference multiplicity drifted")
    equality_calls = [call for call in calls if "operator==" in _call_callee_names(call)]
    _require(
        len(equality_calls) == 1, "identity rejection no longer uses the canonical equality once"
    )
    _require(
        _BUILDER in _referenced_names(statements[0]),
        "identity rejection no longer compares against the canonical builder",
    )


def _validate_producer_text(source: str, header: str) -> None:
    source_tokens = _strip_comments_and_literals(source)
    header_tokens = _strip_comments_and_literals(header)
    _require(
        not re.search(
            r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif|pragma)", source_tokens, re.MULTILINE
        ),
        "ADR067-PRODUCER-SOURCE: producer implementation gained conditional preprocessing or a pragma",
    )
    directives = re.findall(r"^\s*#\s*([A-Za-z_]+)", header_tokens, re.MULTILINE)
    _require(
        directives.count("ifndef") == 1
        and directives.count("define") == 1
        and directives.count("endif") == 1
        and not any(item in {"if", "ifdef", "elif", "else", "pragma"} for item in directives),
        "producer header contains a conditional beyond its one include guard",
    )
    namespace_token = "namespace apgar::benchmark::internal {"
    _require(
        source_tokens.count(namespace_token) == 1 and header_tokens.count(namespace_token) == 1,
        "producer namespace wrapper drifted",
    )
    source_prefix, source_namespace = source_tokens.split(namespace_token)
    source_lines = [line.strip() for line in source_prefix.splitlines() if line.strip()]
    _require(
        source_lines == ["#include"], "producer implementation gained state outside its namespace"
    )
    source_close = source_namespace.rfind("}")
    _require(
        source_close >= 0 and not source_namespace[source_close + 1 :].strip(),
        "producer implementation gained trailing global state",
    )
    header_prefix, header_namespace = header_tokens.split(namespace_token)
    header_prefix_lines = [line.strip() for line in header_prefix.splitlines() if line.strip()]
    _require(
        len(header_prefix_lines) == 6
        and header_prefix_lines[0].startswith("#ifndef ")
        and header_prefix_lines[1].startswith("#define ")
        and all(line.startswith("#include") for line in header_prefix_lines[2:]),
        "producer header gained a declaration outside its namespace",
    )
    header_close = header_namespace.rfind("}")
    _require(
        header_close >= 0
        and [
            line.strip()
            for line in header_namespace[header_close + 1 :].splitlines()
            if line.strip()
        ]
        == ["#endif"],
        "producer header gained trailing global state",
    )
    forbidden = (
        r"\b(no_sanitize|no_sanitize_address|disable_sanitizer_instrumentation)\b",
        r"\b(__has_feature|__SANITIZE_ADDRESS__|__UBSAN__)\b",
        r"\b(asm|__asm__|system|fork|exec|dlopen|dlsym|syscall)\b",
        r"\b(new|delete)\b",
        r"\b(alias|ifunc)\b",
        r"\b(__ubsan_default_options|__ubsan_on_report)\b",
        r"\b__ubsan_handle_[A-Za-z0-9_]*\b",
        r"\b(init_array|fini_array|constructor|destructor)\b",
        r"\bsection\s*\(",
    )
    for pattern in forbidden:
        _require(
            not re.search(pattern, source_tokens),
            f"ADR067-PRODUCER-SOURCE: producer source matched forbidden edge {pattern}",
        )


def _validate_producer_semantic_surface(
    target_namespaces: Sequence[Mapping[str, Any]],
) -> None:
    forbidden_attributes = {
        "AliasAttr",
        "ConstructorAttr",
        "DestructorAttr",
        "DisableSanitizerInstrumentationAttr",
        "IFuncAttr",
        "NoSanitizeAttr",
        "SectionAttr",
    }
    for namespace in target_namespaces:
        for node in _objects(namespace):
            kind = str(node.get("kind", ""))
            if kind in forbidden_attributes:
                raise AuditError(
                    "ADR067-PRODUCER-SEMANTIC-SURFACE: producer gained forbidden structured "
                    f"attribute {kind}"
                )
            name = str(node.get("name", ""))
            if name in {"__ubsan_default_options", "__ubsan_on_report"} or name.startswith(
                "__ubsan_handle_"
            ):
                raise AuditError(
                    f"ADR067-PRODUCER-SEMANTIC-SURFACE: producer gained a UBSan runtime hook {name}"
                )
            referenced = node.get("referencedDecl")
            if isinstance(referenced, dict) and str(referenced.get("name", "")) in {
                "system",
                "fork",
                "execve",
                "dlopen",
                "dlsym",
                "syscall",
            }:
                raise AuditError(
                    "ADR067-PRODUCER-SEMANTIC-SURFACE: producer gained an external-state "
                    f"reference {referenced.get('name')}"
                )


def _audit_producer(inventory: Mapping[str, Any]) -> str:
    namespace_roots = _dump_ast(inventory, "apgar::benchmark::internal")
    target_namespaces = [
        root
        for root in namespace_roots
        if _declaration_file(root) in {_PRODUCER_HEADER, _PRODUCER_SOURCE}
    ]
    _require(len(target_namespaces) == 2, "producer target-owned namespace inventory drifted")
    header_namespace = next(
        root for root in target_namespaces if _declaration_file(root) == _PRODUCER_HEADER
    )
    source_namespace = next(
        root for root in target_namespaces if _declaration_file(root) == _PRODUCER_SOURCE
    )
    _validate_identity_equality_ownership(target_namespaces)
    _validate_producer_namespace_inventory(header_namespace, source_namespace)
    _validate_producer_semantic_surface(target_namespaces)
    _validate_producer_tu_identity(target_namespaces)
    roots = [*header_namespace.get("inner", ()), *source_namespace.get("inner", ())]
    records = [
        root
        for root in roots
        if root.get("kind") == "CXXRecordDecl"
        and root.get("name") == _IDENTITY
        and root.get("completeDefinition") is True
    ]
    _require(len(records) == 1, "producer must expose one complete identity RecordDecl")
    _validate_identity(records[0])
    _, builder = _function_definitions(roots, _BUILDER)
    _, composite = _function_definitions(roots, _COMPOSITE)
    source = _read_source(inventory["commands"][0]["source"])
    header = _read_source(_PRODUCER_HEADER)
    _validate_producer_text(source, header)
    _validate_builder(builder, source)
    _validate_composite(composite)
    return _fingerprint([records[0], builder, composite])


def _validate_producer_tu_identity(
    target_namespaces: Sequence[Mapping[str, Any]],
) -> None:
    whole_tu_fingerprint = _fingerprint(target_namespaces)
    _require(
        whole_tu_fingerprint == _PRODUCER_TU_FINGERPRINT,
        "ADR067-PRODUCER-TU-IDENTITY: producer complete target-owned TU AST drifted: "
        f"{whole_tu_fingerprint}",
    )


def _validate_producer_namespace_inventory(
    header_namespace: Mapping[str, Any], source_namespace: Mapping[str, Any]
) -> None:
    header_inventory = [
        (child.get("kind"), child.get("name")) for child in header_namespace.get("inner", ())
    ]
    source_inventory = [
        (child.get("kind"), child.get("name")) for child in source_namespace.get("inner", ())
    ]
    _require(
        header_inventory
        == [
            ("VarDecl", "kPhase4H4096SessionV5SameRunPerNetReportProducerInvariant"),
            ("CXXRecordDecl", _IDENTITY),
            ("FunctionDecl", _BUILDER),
            ("FunctionDecl", _COMPOSITE),
        ],
        "ADR067-PRODUCER-DECL-INVENTORY: producer header declaration inventory "
        f"drifted: {header_inventory}",
    )
    _require(
        source_inventory == [("FunctionDecl", _BUILDER), ("FunctionDecl", _COMPOSITE)],
        "ADR067-PRODUCER-DECL-INVENTORY: producer implementation declaration inventory "
        f"drifted: {source_inventory}",
    )


def _validate_test_source(source: str) -> None:
    tokens = _strip_comments_and_literals(source)
    forbidden = (
        r"\b(?:EXPECT|ASSERT)_(?:DEATH|EXIT)\s*\(",
        r"\bGTEST_SKIP\s*\(",
        r"\bDISABLED_",
        r"\b(?:int|signed|auto|long|short)\s+main\s*\(",
        r"\b(?:system|popen|fork|exec\w*|getenv|setenv|unsetenv|fopen|open|socket|connect|"
        r"send|recv|dlopen|dlsym|syscall)\s*\(",
        r"\b(?:new|delete)\b",
        r"\b(?:asm|__asm__)\b",
        r"\b(?:__has_feature|__SANITIZE_ADDRESS__|__UBSAN__)\b",
        r"\b(?:__ubsan_default_options|__ubsan_on_report)\b",
        r"\b(?:throw|try|catch)\b",
    )
    for pattern in forbidden:
        _require(
            not re.search(pattern, tokens),
            f"ADR067-DIRECT-SOURCE: direct test matched forbidden surface {pattern}",
        )
    directives = re.findall(r"^\s*#\s*([A-Za-z_]+)", tokens, re.MULTILINE)
    _require(
        set(directives) <= {"include"},
        "ADR067-DIRECT-PREPROCESSOR: direct test gained macro replacement or conditionals",
    )
    macro_calls = set(re.findall(r"\b([A-Z][A-Z0-9_]*)\s*\(", tokens))
    allowed_macros = {
        "TEST",
        "EXPECT_EQ",
        "EXPECT_TRUE",
        "EXPECT_FALSE",
        "ASSERT_TRUE",
        "SCOPED_TRACE",
    }
    _require(
        macro_calls <= allowed_macros,
        f"direct test GoogleTest macro surface drifted: {macro_calls}",
    )
    registrations = re.findall(r"\bTEST\s*\(\s*([A-Za-z_]\w*)\s*,\s*([A-Za-z_]\w*)\s*\)", source)
    _require(
        registrations == [(_TEST_SUITE, name) for name in _TEST_CASES],
        "ADR067-DIRECT-REGISTRATION: direct-test registration set/order drifted",
    )
    outer_namespace = "namespace apgar::benchmark {"
    anonymous_namespace = "namespace {"
    _require(
        tokens.count(outer_namespace) == 1 and tokens.count(anonymous_namespace) == 1,
        "direct-test namespace wrapper drifted",
    )
    prefix, namespace_body = tokens.split(outer_namespace)
    prefix_lines = [line.strip() for line in prefix.splitlines() if line.strip()]
    _require(
        len(prefix_lines) == 7 and all(line.startswith("#include") for line in prefix_lines),
        "direct test gained global state before its namespace",
    )
    outer_open = tokens.index("{", tokens.index(outer_namespace))
    outer_close = _matching_brace(tokens, outer_open, "direct-test apgar::benchmark namespace")
    _require(
        not tokens[outer_close + 1 :].strip(),
        "direct test gained trailing global state",
    )
    outer_body = tokens[outer_open + 1 : outer_close]
    anonymous_start = outer_body.index(anonymous_namespace)
    anonymous_open = outer_body.index("{", anonymous_start)
    anonymous_close = _matching_brace(
        outer_body,
        anonymous_open,
        "direct-test anonymous namespace",
    )
    _require(
        not outer_body[:anonymous_start].strip() and not outer_body[anonymous_close + 1 :].strip(),
        "direct test outer namespace must contain only its anonymous namespace",
    )


_DIRECT_CAPABILITY_NAME = re.compile(
    r"(?:Acquisition|Allocate|Allocator|Execute|Execution|RouteQuery|Prepare|Worker|Serialize|"
    r"Artifact|system|popen|fork|exec|fopen|open|filesystem|getenv|setenv|unsetenv|"
    r"dlopen|dlsym|syscall|DeathTest|Subprocess)",
    re.IGNORECASE,
)


def _validate_direct_test_semantic_surface(
    roots: Sequence[Mapping[str, Any]],
) -> None:
    for root in roots:
        if root.get("kind") in {"FunctionDecl", "CXXMethodDecl"} and root.get("name") in {
            _BUILDER,
            _COMPOSITE,
        }:
            raise AuditError(
                "ADR067-DIRECT-PRODUCER-OWNER: direct test redeclared an allowlisted producer function"
            )
        for node in _objects(root):
            kind = node.get("kind")
            if kind in {
                "CXXNewExpr",
                "CXXDeleteExpr",
                "GCCAsmStmt",
                "MSAsmStmt",
            } and _declaration_file(node) in {"", _DIRECT_TEST_SOURCE}:
                raise AuditError(
                    f"ADR067-DIRECT-CAPABILITY: direct test gained semantic node {kind}"
                )
            if kind in {
                "CXXConstructExpr",
                "CXXTemporaryObjectExpr",
                "CXXBindTemporaryExpr",
            } and _declaration_file(node) in {"", _DIRECT_TEST_SOURCE}:
                type_name = str(node.get("type", {}).get("qualType", ""))
                if _DIRECT_CAPABILITY_NAME.search(type_name):
                    raise AuditError(
                        "ADR067-DIRECT-CAPABILITY: direct test gained an acquisition-capable "
                        f"lifecycle edge {type_name}"
                    )
            referenced = node.get("referencedDecl")
            if not isinstance(referenced, dict):
                continue
            if referenced.get("kind") not in {
                "FunctionDecl",
                "CXXMethodDecl",
                "CXXConstructorDecl",
                "CXXDestructorDecl",
                "VarDecl",
            }:
                continue
            reference_name = str(referenced.get("name", ""))
            identity = reference_name
            if reference_name.startswith("operator"):
                identity += " " + str(referenced.get("type", {}).get("qualType", ""))
            if _DIRECT_CAPABILITY_NAME.search(identity):
                raise AuditError(
                    "ADR067-DIRECT-CAPABILITY: direct test gained a forbidden callable/reference "
                    f"edge {identity.strip()}"
                )


def _validate_direct_test_control_flow(
    bodies: Sequence[Mapping[str, Any]],
) -> None:
    for body in bodies:
        statements = _body(body).get("inner", ())
        _require(
            not any(statement.get("kind") == "ReturnStmt" for statement in statements),
            "ADR067-DIRECT-CFG-EARLY-RETURN: direct TestBody gained a top-level early return",
        )
        _require(
            not any(statement.get("kind") == "IfStmt" for statement in statements),
            "ADR067-DIRECT-CFG-DEAD-GUARD: direct TestBody gained a top-level conditional guard",
        )


def _validate_google_test_wrapper() -> None:
    source = _read_source(_TEST_SUPPORT_HEADER)
    tokens = _strip_comments_and_literals(source)
    directives = [
        directive.strip() for directive in re.findall(r"^\s*#\s*([^\n]+)", tokens, re.MULTILINE)
    ]
    _require(
        directives
        == [
            "ifndef APGAR_TESTS_SUPPORT_GOOGLE_TEST_H_",
            "define APGAR_TESTS_SUPPORT_GOOGLE_TEST_H_",
            "if defined(__clang__)",
            "pragma clang diagnostic push",
            "pragma clang diagnostic ignored",
            "endif",
            "include <gtest/gtest.h>",
            "if defined(__clang__)",
            "pragma clang diagnostic pop",
            "endif",
            "endif",
        ],
        "GoogleTest compatibility wrapper directive/control-flow surface drifted",
    )
    _require(
        not re.search(
            r"\b(?:namespace|class|struct|union|enum|using|typedef|extern|static)\b", tokens
        ),
        "GoogleTest compatibility wrapper gained a declaration or static state",
    )


def _audit_direct_test(inventory: Mapping[str, Any]) -> str:
    source = _read_source(inventory["commands"][0]["source"])
    _validate_test_source(source)
    _validate_google_test_wrapper()
    namespace_roots = _dump_ast(inventory, "apgar::benchmark")
    outer_namespaces = [
        root
        for root in namespace_roots
        if root.get("kind") == "NamespaceDecl" and _declaration_file(root) == _DIRECT_TEST_SOURCE
    ]
    _require(
        len(outer_namespaces) == 1,
        "direct test must have exactly one complete target-owned outer namespace",
    )
    outer_inventory = [
        (child.get("kind"), child.get("name")) for child in outer_namespaces[0].get("inner", ())
    ]
    _require(
        outer_inventory == [("NamespaceDecl", None), ("UsingDirectiveDecl", None)],
        f"direct-test outer namespace declaration inventory drifted: {outer_inventory}",
    )
    outer_fingerprint = _validate_direct_test_tu_identity(
        outer_namespaces,
        str(inventory.get("configuration")),
    )
    producer_namespaces = [
        root
        for root in namespace_roots
        if root.get("kind") == "NamespaceDecl" and _declaration_file(root) == _PRODUCER_HEADER
    ]
    _require(
        len(producer_namespaces) == 1,
        "direct test lost the exact producer-header declaration owner",
    )
    producer_declaration_ids = {
        node.get("id")
        for node in _objects(producer_namespaces[0])
        if node.get("kind") == "FunctionDecl" and node.get("name") in {_BUILDER, _COMPOSITE}
    }
    expected_reference_types = {
        _BUILDER: ("Phase4H4096SessionV5SameRunPerNetReportProducerIdentity () noexcept"),
        _COMPOSITE: (
            "std::optional<Phase4PairedTrialError> "
            "(const Phase4H4096SessionV5SameRunPerNetReportProducerIdentity &, "
            "const Phase4H4096SessionV5ControllerSourceAssociation &)"
        ),
    }
    producer_references = [
        node.get("referencedDecl", {})
        for node in _objects(outer_namespaces[0])
        if node.get("kind") == "DeclRefExpr"
        and node.get("referencedDecl", {}).get("name") in expected_reference_types
    ]
    _require(
        Counter(reference.get("name") for reference in producer_references)
        == Counter({_BUILDER: 4, _COMPOSITE: 4}),
        "direct-test producer-call multiplicity drifted",
    )
    _require(
        all(
            reference.get("id") in producer_declaration_ids
            and reference.get("kind") == "FunctionDecl"
            and reference.get("type", {}).get("qualType")
            == expected_reference_types[reference.get("name")]
            for reference in producer_references
        ),
        "direct-test producer call lost its exact namespace owner or signature",
    )
    _require(
        not any(
            node.get("kind") == "DeclRefExpr"
            and node.get("referencedDecl", {}).get("name") == _CONTROLLER
            for node in _objects(outer_namespaces[0])
        ),
        "direct test bypasses the composite and calls the controller directly",
    )
    roots = _dump_ast(inventory, "(anonymous namespace)")
    _validate_direct_test_semantic_surface(roots)
    root_inventory = [(root.get("kind"), root.get("name")) for root in roots]
    expected_root_inventory: list[tuple[str, str | None]] = [
        ("TypeAliasDecl", "Identity"),
        ("TypeAliasDecl", "Source"),
        ("VarDecl", "kCleanCommit"),
        ("FunctionDecl", "CleanSource"),
        ("FunctionDecl", "UnpublishableSource"),
        ("FunctionDecl", "ExpectInvariant"),
        ("FunctionDecl", "ExpectProducerIdentityError"),
        ("CXXRecordDecl", f"{_TEST_SUITE}_{_TEST_CASES[0]}_Test"),
        ("VarDecl", "test_info_"),
        ("CXXMethodDecl", "TestBody"),
        ("CXXRecordDecl", "IdentityMutation"),
        ("VarDecl", "kIdentityMutations"),
        ("CXXRecordDecl", f"{_TEST_SUITE}_{_TEST_CASES[1]}_Test"),
        ("VarDecl", "test_info_"),
        ("CXXMethodDecl", "TestBody"),
        ("CXXRecordDecl", "ProfileMutation"),
        ("VarDecl", "kForeignProfiles"),
        ("CXXRecordDecl", f"{_TEST_SUITE}_{_TEST_CASES[2]}_Test"),
        ("VarDecl", "test_info_"),
        ("CXXMethodDecl", "TestBody"),
        ("CXXRecordDecl", f"{_TEST_SUITE}_{_TEST_CASES[3]}_Test"),
        ("VarDecl", "test_info_"),
        ("CXXMethodDecl", "TestBody"),
    ]
    _require(
        root_inventory == expected_root_inventory,
        f"direct-test complete target-owned declaration inventory drifted: {root_inventory}",
    )
    registration_names = [
        str(root.get("name"))
        for root in roots
        if root.get("kind") == "CXXRecordDecl" and str(root.get("name", "")).endswith("_Test")
    ]
    _validate_direct_registration_names(registration_names)
    bodies = [
        root
        for root in roots
        if root.get("kind") == "CXXMethodDecl" and root.get("name") == "TestBody"
    ]
    _require(len(bodies) == len(_TEST_CASES), "direct-test TestBody multiplicity drifted")
    _validate_direct_test_control_flow(bodies)
    call_names: list[str] = []
    for body in bodies:
        call_names.extend(_referenced_names(body))
    forbidden_calls = [
        name
        for name in call_names
        if re.search(
            r"(?:Allocate|Allocator|Execute|Execution|RouteQuery|Prepare|Worker|Serialize|"
            r"Artifact|system|fork|exec|open|getenv|dlopen|syscall|DeathTest|Subprocess)",
            name,
        )
        and name not in {_BUILDER, _COMPOSITE}
    ]
    _require(not forbidden_calls, f"direct test gained a capability edge: {forbidden_calls}")

    mutation_roots = [
        root
        for root in roots
        if root.get("kind") == "VarDecl" and root.get("name") == "kIdentityMutations"
    ]
    _require(len(mutation_roots) == 1, "identity mutation array declaration drifted")
    mutation_members = [
        (str(node.get("name")), _source_offset(node))
        for root in mutation_roots
        for node in _objects(root)
        if node.get("kind") == "MemberExpr" and node.get("name") in _FIELDS
    ]
    mutation_fields: list[str] = []
    seen_mutations: set[tuple[str, int | None]] = set()
    for member in mutation_members:
        if member in seen_mutations:
            continue
        seen_mutations.add(member)
        mutation_fields.append(member[0])
    mutation_counts = Counter(mutation_fields)
    expected_mutation_counts = Counter(_FIELDS)
    expected_mutation_counts["telemetry_required"] = 2
    expected_mutation_counts["report_decision_eligible"] = 2
    _require(
        mutation_counts == expected_mutation_counts
        and list(dict.fromkeys(mutation_fields)) == list(_FIELDS),
        f"61 independent identity mutation paths drifted: {mutation_fields}",
    )
    second_body = bodies[1]
    _require(
        any(node.get("kind") == "CXXForRangeStmt" for node in _objects(second_body)),
        "61-field mutation registration lost its unavoidable complete range loop",
    )
    _require(
        "ExpectProducerIdentityError" in _referenced_names(second_body),
        "61-field mutation loop lost its rejection assertion",
    )
    canonical_fields = [
        str(node.get("name"))
        for node in _objects(bodies[0])
        if node.get("kind") == "MemberExpr" and node.get("name") in _FIELDS
    ]
    _require(canonical_fields == list(_FIELDS), "canonical builder's 61 assertion paths drifted")
    return outer_fingerprint


def _validate_direct_registration_names(registration_names: Sequence[str]) -> None:
    expected_registration_names = [f"{_TEST_SUITE}_{name}_Test" for name in _TEST_CASES]
    _require(
        list(registration_names) == expected_registration_names,
        "ADR067-DIRECT-REGISTRATION-AST: Clang test registration identity drifted",
    )


def _validate_direct_test_tu_identity(
    outer_namespaces: Sequence[Mapping[str, Any]], configuration: str
) -> str:
    expected_outer_fingerprint = _DIRECT_TEST_TU_FINGERPRINTS.get(configuration)
    _require(
        expected_outer_fingerprint is not None,
        "direct test lacks a frozen configuration-specific whole-TU identity",
    )
    outer_fingerprint = _fingerprint(outer_namespaces)
    _require(
        outer_fingerprint == expected_outer_fingerprint,
        "direct-test complete outer namespace AST/CFG/call-edge identity drifted: "
        f"{outer_fingerprint}",
    )
    return outer_fingerprint


def _run_direct_test(inventory: Mapping[str, Any], configuration: str) -> None:
    output_paths = inventory.get("target_output_paths")
    _require(
        isinstance(output_paths, list) and len(output_paths) == 1,
        "direct test must expose exactly one executable output path",
    )
    executable = pathlib.Path(output_paths[0])
    _require(
        executable.name
        == "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
        "direct-test executable basename drifted",
    )
    environment = {"UBSAN_OPTIONS": "halt_on_error=1"} if configuration == "ubsan" else {}
    completed = subprocess.run(
        [str(executable)],
        capture_output=True,
        check=False,
        text=True,
        encoding="utf-8",
        errors="strict",
        env=environment,
        timeout=60,
    )
    _require(completed.returncode == 0, f"unfiltered direct test failed: {completed.stderr[:2000]}")
    combined = completed.stdout + completed.stderr
    sanitizer_markers = (
        "AddressSanitizer",
        "UndefinedBehaviorSanitizer",
        "runtime error:",
        "SUMMARY: Sanitizer",
    )
    _require(
        not any(marker in combined for marker in sanitizer_markers),
        "direct test emitted a sanitizer diagnostic",
    )
    _require(
        "DISABLED_" not in combined and "SKIPPED" not in combined, "direct test skipped evidence"
    )
    expected_names = [f"{_TEST_SUITE}.{name}" for name in _TEST_CASES]
    observed_run = re.findall(r"^\[ RUN      \] (\S+)\s*$", combined, re.MULTILINE)
    observed_ok = re.findall(r"^\[       OK \] (\S+) \(\d+ ms\)\s*$", combined, re.MULTILINE)
    _require(observed_run == expected_names, "unfiltered direct-test run set/order drifted")
    _require(observed_ok == expected_names, "unfiltered direct-test pass set/order drifted")
    _require(
        "[==========] 4 tests from 1 test suite ran." in combined
        and "[  PASSED  ] 4 tests." in combined,
        "direct-test complete-set summary drifted",
    )


def _runner_ast(inventory: Mapping[str, Any], *, forced: bool) -> tuple[str, dict[str, bool]]:
    anonymous_roots = _dump_ast(inventory, "(anonymous namespace)")
    expected_inventory: list[tuple[str, str | None]] = [
        ("UsingDecl", "apgar::benchmark::Phase4PairedTrialError"),
        ("UsingShadowDecl", None),
        (
            "UsingDecl",
            "apgar::benchmark::internal::Phase4H4096SessionV5ControllerSourceAssociation",
        ),
        ("UsingShadowDecl", None),
        (
            "UsingDecl",
            "apgar::benchmark::internal::Phase4H4096SessionV5SameRunPerNetReportProducerIdentity",
        ),
        ("UsingShadowDecl", None),
        ("VarDecl", "kArgumentInvariant"),
        ("VarDecl", "kUnexpectedSuccessInvariant"),
        ("VarDecl", "kEmbeddedCommit"),
        ("VarDecl", "kSourceStamped"),
        ("VarDecl", "kSourceTreeDirty"),
        ("FunctionDecl", "IsLowercaseCommit"),
        ("FunctionDecl", "ParseRuntimeCommit"),
        ("FunctionDecl", "RunPreflight"),
    ]
    observed_inventory = [(root.get("kind"), root.get("name")) for root in anonymous_roots]
    _require(
        observed_inventory == expected_inventory,
        f"runner complete anonymous-namespace inventory drifted: {observed_inventory}",
    )
    expected_fingerprint = (
        _RUNNER_FORCED_TU_FINGERPRINT if forced else _RUNNER_NORMAL_TU_FINGERPRINT
    )
    anonymous_fingerprint = _fingerprint(anonymous_roots)
    _require(
        anonymous_fingerprint == expected_fingerprint,
        f"runner complete AST/call-edge identity drifted: {anonymous_fingerprint}",
    )
    main_roots = [
        root
        for root in _dump_ast(inventory, "main")
        if root.get("kind") == "FunctionDecl"
        and root.get("name") == "main"
        and _declaration_file(root) == _RUNNER_SOURCE
    ]
    _require(len(main_roots) == 1, "runner must define exactly one target-owned main")
    _validate_runner_semantic_surface([*anonymous_roots, *main_roots])
    main_fingerprint = _fingerprint(main_roots)
    _require(
        main_fingerprint == _RUNNER_MAIN_FINGERPRINT,
        f"runner main AST/control-flow identity drifted: {main_fingerprint}",
    )
    functions = [
        root
        for root in anonymous_roots
        if root.get("kind") == "FunctionDecl"
        and root.get("name") in {"IsLowercaseCommit", "ParseRuntimeCommit", "RunPreflight"}
    ]
    _require(len(functions) == 3, "runner helper function inventory drifted")
    functions.append(main_roots[0])

    run_preflight = functions[2]
    run_names = _referenced_names(run_preflight)
    _require(run_names.count(_BUILDER) == 1, "runner identity builder edge drifted")
    _require(run_names.count(_COMPOSITE) == 1, "runner producer-preflight edge drifted")
    main = functions[3]
    main_names = _referenced_names(main)
    _require(main_names.count("ParseRuntimeCommit") == 1, "runner parsing edge drifted")
    _require(main_names.count("RunPreflight") == 1, "runner preflight edge drifted")
    return_literals = [
        node.get("value")
        for node in _objects(main)
        if node.get("kind") == "IntegerLiteral"
        and any(parent.get("kind") == "ReturnStmt" for parent in _objects(main))
    ]
    _require(return_literals.count("2") >= 2, "runner no longer returns status 2 on every path")

    constants: dict[str, bool] = {}
    for name in ("kSourceStamped", "kSourceTreeDirty"):
        declarations = [
            root
            for root in anonymous_roots
            if root.get("kind") == "VarDecl" and root.get("name") == name
        ]
        _require(len(declarations) == 1, f"runner {name} multiplicity drifted")
        values = [
            node.get("value")
            for node in _objects(declarations[0])
            if node.get("kind") == "CXXBoolLiteralExpr"
        ]
        _require(len(values) == 1 and isinstance(values[0], bool), f"runner {name} is not literal")
        constants[name] = values[0]
    return _fingerprint(functions), constants


def _validate_runner_semantic_surface(
    roots: Sequence[Mapping[str, Any]],
) -> None:
    nodes = [node for root in roots for node in _objects(root)]
    startup_attributes = [
        node for node in nodes if node.get("kind") in {"ConstructorAttr", "DestructorAttr"}
    ]
    external_references = [
        node.get("referencedDecl")
        for node in nodes
        if isinstance(node.get("referencedDecl"), dict)
        and str(node["referencedDecl"].get("name", ""))
        in {
            "system",
            "popen",
            "fork",
            "execve",
            "fopen",
            "open",
            "getenv",
            "setenv",
            "unsetenv",
            "dlopen",
            "dlsym",
            "syscall",
        }
    ]
    if startup_attributes and external_references:
        raise AuditError(
            "ADR067-RUNNER-STARTUP-CAPABILITY: runner hides an external-state edge in "
            "startup/teardown"
        )
    for root in roots:
        for node in _objects(root):
            if node.get("kind") in {
                "CXXNewExpr",
                "CXXDeleteExpr",
                "GCCAsmStmt",
                "MSAsmStmt",
            }:
                raise AuditError(
                    "ADR067-RUNNER-SEMANTIC-SURFACE: runner gained an allocation/assembly edge"
                )
            if node.get("kind") in {"ConstructorAttr", "DestructorAttr"}:
                raise AuditError(
                    "ADR067-RUNNER-SEMANTIC-SURFACE: runner gained a startup/teardown attribute"
                )
            referenced = node.get("referencedDecl")
            if isinstance(referenced, dict) and str(referenced.get("name", "")) in {
                "system",
                "popen",
                "fork",
                "execve",
                "fopen",
                "open",
                "getenv",
                "setenv",
                "unsetenv",
                "dlopen",
                "dlsym",
                "syscall",
            }:
                raise AuditError(
                    "ADR067-RUNNER-SEMANTIC-SURFACE: runner gained an external-state "
                    f"reference {referenced.get('name')}"
                )


def _validate_runner_source(source: str) -> None:
    tokens = _strip_comments_and_literals(source)
    forbidden = (
        r"\b(?:system|popen|fork|exec\w*|getenv|setenv|unsetenv|fopen|open|socket|connect|"
        r"send|recv|dlopen|dlsym|syscall)\s*\(",
        r"\b(?:new|delete)\b",
        r"\b(?:asm|__asm__)\b",
        r"\b(?:__ubsan_default_options|__ubsan_on_report)\b",
        r"\b(?:constructor|destructor|init_array|fini_array)\b",
    )
    for pattern in forbidden:
        _require(
            not re.search(pattern, tokens),
            f"ADR067-RUNNER-SOURCE: runner matched forbidden external-state edge {pattern}",
        )
    directives = re.findall(r"^\s*#\s*([^\n]+)", tokens, re.MULTILINE)
    conditionals = [
        directive.strip()
        for directive in directives
        if directive.lstrip().startswith(("if", "else", "endif"))
    ]
    _require(
        conditionals
        == [
            "if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING)",
            "else",
            "endif",
        ],
        "ADR067-RUNNER-SOURCE: runner forced-source conditional surface drifted",
    )
    function_names = re.findall(
        r"(?:^|\n)(?:\[\[nodiscard\]\]\s*)?(?:[\w:<>,*&]+\s+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{",
        tokens,
    )
    _require(
        function_names == ["IsLowercaseCommit", "ParseRuntimeCommit", "RunPreflight", "main"],
        f"runner target-owned function inventory drifted: {function_names}",
    )
    namespace_token = "namespace {"
    _require(tokens.count(namespace_token) == 1, "runner anonymous namespace wrapper drifted")
    prefix, remainder = tokens.split(namespace_token)
    prefix_lines = [line.strip() for line in prefix.splitlines() if line.strip()]
    _require(
        len(prefix_lines) == 4 and all(line.startswith("#include") for line in prefix_lines),
        "runner gained global state before its namespace",
    )
    namespace_open = tokens.index("{", tokens.index(namespace_token))
    namespace_close = _matching_brace(tokens, namespace_open, "runner anonymous namespace")
    trailing = tokens[namespace_close + 1 :].strip()
    _require(re.match(r"int\s+main\s*\(", trailing) is not None, "runner gained state before main")
    main_open = trailing.find("{")
    main_close = _matching_brace(trailing, main_open, "runner main")
    _require(not trailing[main_close + 1 :].strip(), "runner gained trailing global state")


def _audit_runners(runner: Mapping[str, Any], forced_runner: Mapping[str, Any]) -> tuple[str, str]:
    source = _read_source(runner["commands"][0]["source"])
    _validate_runner_source(source)
    normal_fingerprint, normal_constants = _runner_ast(runner, forced=False)
    forced_fingerprint, forced_constants = _runner_ast(forced_runner, forced=True)
    _require(
        normal_fingerprint == forced_fingerprint,
        "runner semantic ASTs differ beyond their frozen forced-source constants",
    )
    _require(
        normal_constants == {"kSourceStamped": True, "kSourceTreeDirty": False},
        "ordinary runner source constants drifted",
    )
    _require(
        forced_constants == {"kSourceStamped": False, "kSourceTreeDirty": True},
        "forced runner source constants drifted",
    )
    return normal_fingerprint, forced_fingerprint


def _validate_negative_fixtures(
    fixtures: Sequence[pathlib.Path],
) -> tuple[pathlib.Path, list[str]]:
    names = sorted(path.name for path in fixtures)
    _require(
        names == ["negative_cases.json", "ubsan_signed_overflow.cc"],
        f"semantic negative fixture inventory drifted: {names}",
    )
    cases_path = next(path for path in fixtures if path.name == "negative_cases.json")
    overflow_path = next(path for path in fixtures if path.name == "ubsan_signed_overflow.cc")
    try:
        cases_bytes = cases_path.read_bytes()
    except OSError as error:
        raise AuditError(f"cannot read negative case inventory: {error}") from error
    _require(
        hashlib.sha256(cases_bytes).hexdigest() == _NEGATIVE_CASES_SHA256,
        "semantic negative case inventory digest drifted",
    )
    cases = _read_json(cases_path)
    _require(cases.get("schema_version") == 1, "negative case schema drifted")
    case_names = cases.get("cases")
    _require(
        isinstance(case_names, list)
        and len(case_names) == 109
        and len(set(case_names)) == 109
        and all(isinstance(name, str) and name for name in case_names),
        "negative case multiplicity/identity drifted",
    )
    required_families = {
        "carrier",
        "controller",
        "source",
        "direct",
        "producer",
        "sanitizer",
        "ignorelist",
        "ubsan",
        "gtest",
        "runner",
        "target",
        "equality",
        "stamp",
        "audit",
    }
    observed_families = {name.split("-", 1)[0] for name in case_names}
    _require(required_families <= observed_families, "negative case family coverage drifted")
    overflow = _read_source(str(overflow_path))
    _require("2147483647" in overflow and _SENTINEL in overflow, "live UBSan probe source drifted")
    return overflow_path, list(case_names)


def _run_live_ubsan_probe(executable: pathlib.Path) -> None:
    """Require the audit-action probe to diagnose and abort before its sentinel."""

    _require(executable.is_file(), "UBSan audit action did not provide its private probe binary")
    completed = subprocess.run(
        [str(executable)],
        capture_output=True,
        check=False,
        text=True,
        encoding="utf-8",
        errors="strict",
        env={"UBSAN_OPTIONS": "halt_on_error=1"},
        timeout=30,
    )
    _validate_ubsan_probe_result(completed.returncode, completed.stdout, completed.stderr)


def _validate_ubsan_probe_result(returncode: int, stdout: str, stderr: str) -> None:
    combined = stdout + stderr
    _require(returncode != 0, "live UBSan probe unexpectedly exited zero")
    _require(_SENTINEL not in combined, "live UBSan probe reached its post-overflow sentinel")
    _require(
        "runtime error: signed integer overflow" in stderr
        or "UndefinedBehaviorSanitizer" in stderr,
        f"live UBSan probe emitted no signed-overflow diagnostic: {stderr[:2000]}",
    )


def _validate_provider_evidence(
    inventories: Sequence[pathlib.Path],
    proofs: Sequence[pathlib.Path],
    configuration: str,
    kind: str,
) -> None:
    _require(len(inventories) == 1, "semantic audit requires one startup provider manifest")
    _require(len(proofs) == 1, "semantic audit requires one startup provider proof")
    inventory_path = inventories[0]
    proof_path = proofs[0]
    _require(
        inventory_path.name.endswith(".startup-provenance-manifest.json"),
        "startup provider manifest basename drifted",
    )
    _require(
        proof_path.name.endswith(".startup-provenance.ok"),
        "startup provider proof basename drifted",
    )
    manifest = _read_json(inventory_path)
    _require(
        set(manifest)
        == {
            "configuration",
            "direct_test_elf",
            "kind",
            "llvm_readobj_label",
            "llvm_readobj_resolved_owner",
            "negative_fixture_provider",
            "negative_fixtures",
            "records",
            "schema_version",
            "targets",
        },
        "startup provider manifest schema surface drifted",
    )
    _require(manifest.get("schema_version") == 1, "startup provider schema drifted")
    _require(
        manifest.get("configuration") == configuration, "startup provider configuration drifted"
    )
    _require(manifest.get("kind") == kind, "startup provider kind drifted")
    _require(
        manifest.get("llvm_readobj_label") == "@@llvm+//tools:llvm-readobj"
        and manifest.get("llvm_readobj_resolved_owner")
        == "@@llvm++llvm_toolchain_minimal+llvm-toolchain-minimal-22.1.8-linux-amd64//:bin/llvm-readobj",
        "startup provider tool authority drifted",
    )
    direct_test_elf = manifest.get("direct_test_elf")
    direct_test_basename = "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
    _require(
        isinstance(direct_test_elf, dict)
        and set(direct_test_elf) == {"owner", "path", "short_path"}
        and direct_test_elf.get("owner") == f"@@//:{direct_test_basename}"
        and direct_test_elf.get("short_path") == direct_test_basename
        and isinstance(direct_test_elf.get("path"), str)
        and str(direct_test_elf.get("path")).endswith(f"/bin/{direct_test_basename}"),
        "startup provider direct-test ELF identity drifted",
    )
    _require(
        pathlib.Path(str(direct_test_elf["path"])).is_file(),
        "startup provider direct-test ELF is missing",
    )
    targets = manifest.get("targets")
    expected_target_labels = [
        "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
        "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support",
        "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
        "@@//:phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
        "@@//:phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
    ]
    _require(
        isinstance(targets, list)
        and [target.get("label") for target in targets] == expected_target_labels,
        "startup provider target inventory/order drifted",
    )
    for target in targets:
        _require(
            isinstance(target, dict)
            and set(target)
            == {"configuration_options", "label", "label_attributes", "value_attributes"},
            "startup provider target schema drifted",
        )
        _require(
            target.get("configuration_options") == _EXPECTED_CONFIGURATION_OPTIONS,
            "startup provider resolved C++ configuration drifted",
        )
        _require(
            isinstance(target.get("label_attributes"), dict)
            and isinstance(target.get("value_attributes"), dict),
            "startup provider target attributes are missing",
        )
    records = manifest.get("records")
    _require(isinstance(records, list) and records, "startup provider records are missing")
    normalized_records: list[tuple[str, str, str]] = []
    for record in records:
        _require(
            isinstance(record, dict) and set(record) == {"owner", "path", "short_path"},
            "startup provider record schema drifted",
        )
        owner = record.get("owner")
        path = record.get("path")
        short_path = record.get("short_path")
        _require(
            isinstance(owner, str)
            and (owner.startswith("//:") or owner.startswith("@"))
            and isinstance(path, str)
            and isinstance(short_path, str)
            and path
            and short_path,
            "startup provider record identity drifted",
        )
        _require(pathlib.Path(path).is_file(), f"startup provider artifact is missing: {path}")
        normalized_records.append((owner, path, short_path))
    _require(
        len(normalized_records) == len(set(normalized_records)),
        "startup provider manifest contains a duplicate record",
    )
    owners_by_path: dict[str, set[str]] = {}
    for owner, path, _ in normalized_records:
        owners_by_path.setdefault(path, set()).add(owner)
    _require(
        all(len(owners) == 1 for owners in owners_by_path.values()),
        "startup provider manifest has ambiguous artifact ownership",
    )
    negative_fixtures = manifest.get("negative_fixtures")
    negative_fixture_provider = manifest.get("negative_fixture_provider")
    if kind == "negative":
        _require(
            isinstance(negative_fixtures, list) and len(negative_fixtures) == 2,
            "negative startup provider fixture multiplicity drifted",
        )
        expected_contexts = [
            (
                False,
                "allowlisted-owner-initializer",
                "@@//:phase4_h4096_session_v5_execution_preflight_test_support",
            ),
            (
                True,
                "foreign-alwayslink-initializer",
                "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_"
                "audit_negative_contract_foreign_alwayslink_initializer",
            ),
        ]
        fixture_paths: list[tuple[str, str]] = []
        for fixture, expected_context in zip(negative_fixtures, expected_contexts, strict=True):
            _require(
                isinstance(fixture, dict)
                and set(fixture) == {"alwayslink", "diagnostic_id", "owner", "path", "short_path"}
                and (
                    fixture.get("alwayslink"),
                    fixture.get("diagnostic_id"),
                    fixture.get("owner"),
                )
                == expected_context
                and isinstance(fixture.get("path"), str)
                and bool(fixture.get("path"))
                and str(fixture.get("short_path", "")).endswith(
                    "_foreign_alwayslink_initializer.pic.o"
                ),
                "negative startup provider fixture identity drifted",
            )
            _require(
                pathlib.Path(str(fixture["path"])).is_file(),
                "negative startup provider fixture artifact is missing",
            )
            fixture_paths.append((str(fixture["path"]), str(fixture["short_path"])))
        _require(
            len(set(fixture_paths)) == 1,
            "negative startup provider contexts do not authenticate one live object",
        )
        fixture_target = expected_contexts[1][2]
        fixture_target_name = fixture_target.removeprefix("@@//:")
        object_short_path = fixture_paths[0][1]
        archive_short_path = f"lib{fixture_target_name}.lo"
        _require(
            isinstance(negative_fixture_provider, dict)
            and set(negative_fixture_provider)
            == {"alwayslink", "artifacts", "linker_inputs", "target_label"}
            and negative_fixture_provider.get("alwayslink") is True
            and negative_fixture_provider.get("target_label") == fixture_target,
            "negative startup provider CcInfo identity drifted",
        )
        provider_artifacts = negative_fixture_provider.get("artifacts")
        _require(
            isinstance(provider_artifacts, list) and len(provider_artifacts) == 2,
            "negative startup provider artifact inventory drifted",
        )
        expected_artifact_identities = [
            (fixture_target, object_short_path),
            (fixture_target, archive_short_path),
        ]
        observed_artifact_identities: list[tuple[str, str]] = []
        for artifact in provider_artifacts:
            _require(
                isinstance(artifact, dict)
                and set(artifact) == {"owner", "path", "short_path"}
                and isinstance(artifact.get("owner"), str)
                and isinstance(artifact.get("path"), str)
                and isinstance(artifact.get("short_path"), str),
                "negative startup provider artifact identity drifted",
            )
            _require(
                pathlib.Path(str(artifact["path"])).is_file(),
                "negative startup provider artifact is missing",
            )
            observed_artifact_identities.append(
                (str(artifact["owner"]), str(artifact["short_path"]))
            )
        _require(
            observed_artifact_identities == expected_artifact_identities
            and str(provider_artifacts[0]["path"]) == fixture_paths[0][0],
            "negative startup provider artifacts are not the authenticated fixture closure",
        )
        expected_linker_input = (
            f"owner={fixture_target};libraries=alwayslink=1,static=,"
            f"pic_static={archive_short_path},dynamic=,interface=,resolved_dynamic=,"
            f"resolved_interface=,objects=,pic_objects={object_short_path};flags=;"
            "additional=;linkstamps="
        )
        _require(
            negative_fixture_provider.get("linker_inputs") == [expected_linker_input],
            "negative startup provider linker-input serialization drifted",
        )
    else:
        _require(
            negative_fixtures is None and negative_fixture_provider is None,
            "positive startup provider manifest gained a fixture",
        )
    try:
        proof = proof_path.read_bytes()
        manifest_bytes = inventory_path.read_bytes()
    except OSError as error:
        raise AuditError(f"cannot read startup provider proof {proof_path}: {error}") from error
    diagnostic_lines = (
        "diagnostic=allowlisted-owner-initializer\n"
        "diagnostic=foreign-alwayslink-initializer\n"
        "diagnostic=gtest-initializer-missing\n"
        "diagnostic=gtest-initializer-duplicated\n"
        "diagnostic=gtest-initializer-substituted\n"
        "diagnostic=gtest-initializer-additional\n"
        if kind == "negative"
        else ""
    )
    _require(
        proof
        == (
            f"{_STARTUP_PROVENANCE_STAMP}\n"
            f"configuration={configuration}\n"
            f"kind={kind}\n"
            f"{diagnostic_lines}"
            f"manifest_sha256={hashlib.sha256(manifest_bytes).hexdigest()}\n"
        ).encode(),
        "startup provider proof exact bytes drifted",
    )


def _expect_negative_rejection(name: str, probe: Any, expected_diagnostic_identity: str) -> None:
    try:
        probe()
    except AuditError as error:
        observed = str(error)
        _require(
            observed.startswith(expected_diagnostic_identity),
            "negative case reached the wrong production classifier: "
            f"case={name} expected={expected_diagnostic_identity!r} observed={observed!r}",
        )
        return
    raise AuditError(f"negative case did not trigger its production classifier: {name}")


def _negative_diagnostic_identities(configuration: str) -> dict[str, str]:
    identities: dict[str, str] = {
        "carrier-first-kOrdinary": (
            "preflight identity argument lost literal kSameRun/kController"
        ),
        "carrier-second-kOrdinary": "canonical-cell argument lost literal kSameRun",
        "controller-wrapped": "composite controller call is not its direct final return",
        "controller-indirect": f"{_CONTROLLER} call is indirect or wrapped",
        "controller-duplicated": "ADR067-COMPOSITE-CONTROLLER-MULTIPLICITY:",
        "controller-conditional": "ADR067-COMPOSITE-CONTROLLER-CONDITIONAL:",
        "controller-nonterminal": "ADR067-COMPOSITE-CONTROLLER-NONTERMINAL:",
        "controller-continuation": "ADR067-COMPOSITE-CONTROLLER-CONTINUATION:",
        "source-copied": "ADR067-COMPOSITE-SOURCE-OWNER:",
        "source-substituted": "ADR067-COMPOSITE-SOURCE-OWNER:",
        "identity-extra-field": "identity 61-field name/type/order inventory drifted",
        "equality-missing": "identity must expose exactly one hidden friend",
        "equality-out-of-line": "ADR067-EQUALITY-OUT-OF-LINE:",
        "equality-non-defaulted": "identity equality is not defaulted",
        "equality-duplicated": "identity must expose exactly one hidden friend",
        "equality-custom-partial": "identity equality is not defaulted",
        "equality-additional-operator": "identity gained a method or lifecycle declaration",
        "producer-function-shadow": "ADR067-DIRECT-PRODUCER-OWNER:",
        "test-early-return": "ADR067-DIRECT-CFG-EARLY-RETURN:",
        "test-dead-branch": "ADR067-DIRECT-CFG-DEAD-GUARD:",
        "producer-ast-mismatch": "ADR067-PRODUCER-TU-IDENTITY:",
        "producer-extra-function": "ADR067-PRODUCER-DECL-INVENTORY:",
        "producer-asan-conditional": (
            "ADR067-PRODUCER-SEMANTIC-SURFACE:"
            if configuration == "asan"
            else "ADR067-PRODUCER-SOURCE:"
        ),
        "producer-ubsan-conditional": (
            "ADR067-PRODUCER-SEMANTIC-SURFACE:"
            if configuration == "ubsan"
            else "ADR067-PRODUCER-SOURCE:"
        ),
        "direct-test-sanitizer-conditional": (
            "ADR067-DIRECT-CAPABILITY:" if configuration == "asan" else "ADR067-DIRECT-SOURCE:"
        ),
        "direct-test-main": "ADR067-DIRECT-MAIN-AST:",
        "disabled-test-registration": "ADR067-DIRECT-REGISTRATION-AST:",
        "producer-alias": "ADR067-PRODUCER-SEMANTIC-SURFACE:",
        "producer-indirect-function": "ADR067-PRODUCER-SEMANTIC-SURFACE:",
        "producer-init-fini-section": "ADR067-PRODUCER-SEMANTIC-SURFACE:",
        "producer-system-initializer": "ADR067-PRODUCER-SEMANTIC-SURFACE:",
        "runner-process-edge": "ADR067-RUNNER-SEMANTIC-SURFACE:",
        "runner-file-edge": "ADR067-RUNNER-SEMANTIC-SURFACE:",
        "runner-startup-edge": "ADR067-RUNNER-STARTUP-CAPABILITY:",
        "runner-forced-branch-edge": "ADR067-RUNNER-SEMANTIC-SURFACE:",
        "ubsan-no-recover-missing": "direct_test UBSan fail-live recovery flags drifted:",
        "ubsan-recover-enabled": "direct_test UBSan fail-live recovery flags drifted:",
        "ubsan-options-missing": "direct_test exact sanitizer runtime environment drifted",
        "ubsan-options-weakened": "direct_test exact sanitizer runtime environment drifted",
        "sanitizer-disabling-flag": "direct_test has sanitizer-disabling flags:",
        "ignorelist-additional-flag": "direct_test UBSan ignorelist flag drifted:",
        "ignorelist-substituted-file": "direct_test UBSan ignorelist flag drifted:",
        "configuration-audit-missing": "ADR067-CONFIGURATION-AUDIT-COVERAGE:",
        "unexpected-feature": "direct_test feature configuration drifted",
        "target-source-added": "direct_test sources inventory drifted",
        "target-source-substituted": "direct_test sources inventory drifted",
        "gtest-filter-environment": "direct_test exact sanitizer runtime environment drifted",
        "gtest-main-missing": "direct_test direct dependency inventory drifted",
        "gtest-main-duplicated": "direct_test direct dependency inventory drifted",
        "gtest-main-substituted": "direct_test direct dependency inventory drifted",
        "ignorelist-source-match": "UBSan ignorelist semantically suppresses audited source",
        "ignorelist-extra-suppression": "UBSan ignorelist multiplicity drifted",
        "ignorelist-digest-drift": "UBSan ignorelist digest drifted",
        "ubsan-live-probe-zero-exit": "live UBSan probe unexpectedly exited zero",
        "ubsan-live-probe-sentinel": "live UBSan probe reached its post-overflow sentinel",
        "ubsan-zero-status-diagnostic": "live UBSan probe unexpectedly exited zero",
        "allowlisted-owner-initializer": "ADR067-PROVIDER-DIAGNOSTIC:",
        "foreign-alwayslink-initializer": "ADR067-PROVIDER-DIAGNOSTIC:",
        "gtest-initializer-missing": "ADR067-PROVIDER-DIAGNOSTIC:",
        "gtest-initializer-duplicated": "ADR067-PROVIDER-DIAGNOSTIC:",
        "gtest-initializer-substituted": "ADR067-PROVIDER-DIAGNOSTIC:",
        "gtest-initializer-additional": "ADR067-PROVIDER-DIAGNOSTIC:",
        "stamp-owner-wrong": "ADR067-ANALYSIS-DIAGNOSTIC:",
        "audit-output-unrequested": "ADR067-ANALYSIS-DIAGNOSTIC:",
        "audit-output-missing": "ADR067-ANALYSIS-DIAGNOSTIC:",
        "stamp-stale-input": "ADR067-ANALYSIS-DIAGNOSTIC:",
        "stamp-basename-wrong": "semantic-audit stamp basename drifted",
        "stamp-content-wrong": "semantic-audit stamp exact content/configuration drifted",
        "stamp-configuration-wrong": "semantic-audit stamp exact content/configuration drifted",
    }
    for name in {
        "direct-test-direct-acquisition",
        "direct-test-local-wrapper-acquisition",
        "direct-test-indirect-acquisition",
        "direct-test-external-apgar-method",
        "direct-test-external-apgar-constructor",
        "direct-test-external-apgar-operator",
        "allocator-global-initializer",
        "implicit-acquisition-destructor",
        "implicit-allocation-edge",
        "gtest-subprocess-internal",
        "direct-test-process-edge",
        "direct-test-file-edge",
        "direct-test-environment-edge",
        "direct-test-dynamic-loading-edge",
        "direct-test-raw-syscall-edge",
        "direct-test-inline-assembly-edge",
        "gtest-expect-death",
        "gtest-assert-death",
        "gtest-expect-exit",
        "gtest-assert-exit",
        "gtest-death-local-alias",
    }:
        identities[name] = "ADR067-DIRECT-CAPABILITY:"
    for name in {
        "no-sanitize-attribute",
        "no-sanitize-address-attribute",
        "disable-sanitizer-instrumentation-attribute",
        "sanitizer-optout-pragma",
        "ubsan-default-options-hook",
        "ubsan-report-hook",
        "ubsan-runtime-interposition",
    }:
        identities[name] = "ADR067-PRODUCER-SEMANTIC-SURFACE:"
    for name in {
        "linkstamp-added",
        "additional-linker-input-added",
        "unexpected-link-option",
        "gtest-filter-argument",
        "manual-tag",
        "flaky-enabled",
        "local-enabled",
        "sharding-enabled",
    }:
        identities[name] = "direct_test complete C++ rule attribute schema drifted:"
    for name in {
        "ignorelist-missing",
        "ignorelist-substituted",
        "ignorelist-additional",
        "ignorelist-reordered",
    }:
        identities[name] = "UBSan ignorelist exact content/order drifted"
    return identities


def _producer_negative_nodes(
    inventory: Mapping[str, Any],
) -> tuple[Mapping[str, Any], Mapping[str, Any], list[Mapping[str, Any]]]:
    namespace_roots = _dump_ast(inventory, "apgar::benchmark::internal")
    target_namespaces = [
        root
        for root in namespace_roots
        if _declaration_file(root) in {_PRODUCER_HEADER, _PRODUCER_SOURCE}
    ]
    roots = [child for namespace in target_namespaces for child in namespace.get("inner", ())]
    records = [
        root
        for root in roots
        if root.get("kind") == "CXXRecordDecl"
        and root.get("name") == _IDENTITY
        and root.get("completeDefinition") is True
    ]
    _require(len(records) == 1, "negative probes lost the identity AST baseline")
    _, composite = _function_definitions(roots, _COMPOSITE)
    return records[0], composite, target_namespaces


def _direct_test_negative_roots(
    inventory: Mapping[str, Any],
) -> list[Mapping[str, Any]]:
    roots = _dump_ast(inventory, "(anonymous namespace)")
    _require(roots, "negative probes lost the direct-test semantic baseline")
    return roots


def _semantic_reference(
    name: str,
    *,
    declaration_kind: str = "FunctionDecl",
    type_name: str = "void ()",
) -> Mapping[str, Any]:
    return {
        "kind": "DeclRefExpr",
        "type": {"qualType": type_name},
        "referencedDecl": {
            "kind": declaration_kind,
            "name": name,
            "type": {"qualType": type_name},
        },
    }


def _semantic_call(
    name: str,
    *,
    declaration_kind: str = "FunctionDecl",
    type_name: str = "void ()",
    call_kind: str = "CallExpr",
) -> Mapping[str, Any]:
    return {
        "kind": call_kind,
        "inner": [
            {
                "kind": "ImplicitCastExpr",
                "castKind": "FunctionToPointerDecay",
                "inner": [
                    _semantic_reference(
                        name,
                        declaration_kind=declaration_kind,
                        type_name=type_name,
                    )
                ],
            }
        ],
    }


def _run_direct_semantic_negative_case(
    name: str,
    roots: Sequence[Mapping[str, Any]],
) -> None:
    mutant = copy.deepcopy(list(roots))
    acquisition_call = _semantic_call(
        "ExecutePhase4GlobalAllocation",
        type_name="void (Phase4GlobalAllocator &)",
    )
    if name == "direct-test-direct-acquisition":
        mutation: Mapping[str, Any] = acquisition_call
    elif name == "direct-test-local-wrapper-acquisition":
        mutation = {
            "kind": "FunctionDecl",
            "name": "LocalAcquisitionWrapper",
            "type": {"qualType": "void ()"},
            "inner": [{"kind": "CompoundStmt", "inner": [acquisition_call]}],
        }
    elif name == "direct-test-indirect-acquisition":
        mutation = {
            "kind": "VarDecl",
            "name": "acquisition_pointer",
            "type": {"qualType": "void (*)()"},
            "inner": [
                {
                    "kind": "UnaryOperator",
                    "opcode": "&",
                    "inner": [
                        _semantic_reference(
                            "ExecutePhase4GlobalAllocation",
                            type_name="void ()",
                        )
                    ],
                },
                _semantic_call(
                    "acquisition_pointer",
                    declaration_kind="VarDecl",
                    type_name="void (*)()",
                ),
            ],
        }
    elif name == "direct-test-external-apgar-method":
        mutation = _semantic_call(
            "ExecuteBoardAllocation",
            declaration_kind="CXXMethodDecl",
            type_name="void (apgar::router::Phase4GlobalAllocator::*)()",
            call_kind="CXXMemberCallExpr",
        )
    elif name == "direct-test-external-apgar-constructor":
        mutation = {
            "kind": "CXXConstructExpr",
            "type": {"qualType": "apgar::router::Phase4GlobalAllocator"},
        }
    elif name == "direct-test-external-apgar-operator":
        mutation = _semantic_call(
            "operator()",
            type_name="void (apgar::router::Phase4GlobalAllocator &)",
            call_kind="CXXOperatorCallExpr",
        )
    elif name == "producer-function-shadow":
        mutation = {
            "kind": "FunctionDecl",
            "name": _BUILDER,
            "type": {"qualType": "void ()"},
            "inner": [{"kind": "CompoundStmt", "inner": []}],
        }
    elif name == "allocator-global-initializer":
        mutation = {
            "kind": "VarDecl",
            "name": "kAllocatorStartup",
            "type": {"qualType": "int"},
            "inner": [acquisition_call],
        }
    elif name == "implicit-acquisition-destructor":
        mutation = {
            "kind": "CXXBindTemporaryExpr",
            "type": {"qualType": "apgar::router::Phase4AcquisitionDestructor"},
            "inner": [
                {
                    "kind": "CXXTemporaryObjectExpr",
                    "type": {"qualType": "apgar::router::Phase4AcquisitionDestructor"},
                }
            ],
        }
    elif name == "implicit-allocation-edge":
        mutation = {
            "kind": "CXXNewExpr",
            "type": {"qualType": "apgar::router::Phase4GlobalAllocator *"},
        }
    elif name == "gtest-subprocess-internal":
        mutation = _semantic_call(
            "ExecDeathTestChildMain",
            type_name="int (testing::internal::InternalRunDeathTestFlag *)",
        )
    elif name in {
        "direct-test-process-edge",
        "direct-test-file-edge",
        "direct-test-environment-edge",
        "direct-test-dynamic-loading-edge",
        "direct-test-raw-syscall-edge",
    }:
        reference_names = {
            "direct-test-process-edge": "system",
            "direct-test-file-edge": "fopen",
            "direct-test-environment-edge": "getenv",
            "direct-test-dynamic-loading-edge": "dlopen",
            "direct-test-raw-syscall-edge": "syscall",
        }
        mutation = _semantic_call(reference_names[name], type_name="int (...) ")
    elif name == "direct-test-inline-assembly-edge":
        mutation = {"kind": "GCCAsmStmt", "asmString": "nop"}
    else:
        raise AuditError(f"unknown direct semantic negative case: {name}")
    mutant.append(mutation)
    _validate_direct_test_semantic_surface(mutant)


def _run_direct_control_flow_negative_case(
    name: str,
    roots: Sequence[Mapping[str, Any]],
) -> None:
    mutant = copy.deepcopy(list(roots))
    bodies = [
        root
        for root in mutant
        if root.get("kind") == "CXXMethodDecl" and root.get("name") == "TestBody"
    ]
    _require(len(bodies) == len(_TEST_CASES), "negative direct-CFG baseline drifted")
    statements = _body(bodies[0])["inner"]
    if name == "test-early-return":
        statements.insert(0, {"kind": "ReturnStmt"})
    elif name == "test-dead-branch":
        statements.insert(
            0,
            {
                "kind": "IfStmt",
                "inner": [
                    {"kind": "CXXBoolLiteralExpr", "value": False},
                    {"kind": "CompoundStmt", "inner": []},
                ],
            },
        )
    else:
        raise AuditError(f"unknown direct CFG negative case: {name}")
    _validate_direct_test_control_flow(bodies)


def _insert_before(source: str, anchor: str, payload: str, subject: str) -> str:
    _require(source.count(anchor) == 1, f"{subject} source-mutation anchor drifted")
    return source.replace(anchor, f"{payload}{anchor}", 1)


def _insert_after(source: str, anchor: str, payload: str, subject: str) -> str:
    _require(source.count(anchor) == 1, f"{subject} source-mutation anchor drifted")
    return source.replace(anchor, f"{anchor}{payload}", 1)


def _run_source_negative_case(
    name: str,
    inventories: Mapping[str, Mapping[str, Any]],
    producer_source: str,
    producer_header: str,
    direct_source: str,
    runner_source: str,
) -> None:
    producer_payloads = {
        "producer-asan-conditional": (
            'extern "C" int system(const char*);\n'
            "#if __has_feature(address_sanitizer)\n"
            'int kNegativeAsanAcquisition = system("true");\n#endif\n'
        ),
        "producer-ubsan-conditional": (
            'extern "C" int system(const char*);\n'
            "#if __has_feature(undefined_behavior_sanitizer)\n"
            'int kNegativeUbsanAcquisition = system("true");\n#endif\n'
        ),
        "no-sanitize-attribute": (
            '[[clang::no_sanitize("undefined")]] void NegativeNoSanitize() {}\n'
        ),
        "no-sanitize-address-attribute": (
            "__attribute__((no_sanitize_address)) void NegativeNoAddressSanitize() {}\n"
        ),
        "disable-sanitizer-instrumentation-attribute": (
            "[[clang::disable_sanitizer_instrumentation]] void NegativeNoInstrument() {}\n"
        ),
        "sanitizer-optout-pragma": (
            '#pragma clang attribute push (__attribute__((no_sanitize("undefined"))), '
            "apply_to=function)\n"
            "void NegativePragmaSuppressedFunction() {}\n"
            "#pragma clang attribute pop\n"
        ),
        "ubsan-default-options-hook": (
            'extern "C" const char* __ubsan_default_options() { return "halt_on_error=0"; }\n'
        ),
        "ubsan-report-hook": 'extern "C" void __ubsan_on_report() {}\n',
        "ubsan-runtime-interposition": ('extern "C" void __ubsan_handle_add_overflow() {}\n'),
        "producer-alias": (
            'extern "C" void NegativeAliasTarget() {}\n'
            'extern "C" void NegativeAlias() '
            '__attribute__((alias("NegativeAliasTarget")));\n'
        ),
        "producer-indirect-function": (
            'extern "C" void NegativeImplementation() {}\n'
            'extern "C" void* NegativeResolver() { '
            "return reinterpret_cast<void*>(&NegativeImplementation); }\n"
            'void NegativeIfunc() __attribute__((ifunc("NegativeResolver")));\n'
        ),
        "producer-init-fini-section": (
            'void (*kNegativeInitEntry)() __attribute__((section(".init_array"))) = nullptr;\n'
        ),
        "producer-system-initializer": (
            'extern "C" int system(const char*);\n'
            'int kNegativeSystemInitializer = system("true");\n'
        ),
    }
    direct_payloads = {
        "gtest-expect-death": (
            '[[maybe_unused]] void NegativeDeath() { EXPECT_DEATH((void)0, ".*"); }\n'
        ),
        "gtest-assert-death": (
            '[[maybe_unused]] void NegativeDeath() { ASSERT_DEATH((void)0, ".*"); }\n'
        ),
        "gtest-expect-exit": (
            "[[maybe_unused]] void NegativeDeath() { "
            'EXPECT_EXIT((void)0, ::testing::ExitedWithCode(0), ".*"); }\n'
        ),
        "gtest-assert-exit": (
            "[[maybe_unused]] void NegativeDeath() { "
            'ASSERT_EXIT((void)0, ::testing::ExitedWithCode(0), ".*"); }\n'
        ),
        "gtest-death-local-alias": (
            "#define ADR067_NEGATIVE_DEATH(statement, matcher) EXPECT_DEATH(statement, matcher)\n"
            "[[maybe_unused]] void NegativeDeath() { "
            'ADR067_NEGATIVE_DEATH((void)0, ".*"); }\n'
        ),
        "direct-test-sanitizer-conditional": (
            "#if __has_feature(address_sanitizer)\n"
            "void ExecutePhase4GlobalAllocation() {}\n"
            "[[maybe_unused]] void NegativeSanitizerAcquisition() { "
            "ExecutePhase4GlobalAllocation(); }\n"
            "#endif\n"
        ),
    }
    runner_payloads = {
        "runner-process-edge": (
            'extern "C" int system(const char*);\n'
            '[[maybe_unused]] int NegativeProcess() { return system("true"); }\n'
        ),
        "runner-file-edge": (
            '[[maybe_unused]] auto* NegativeFile() { return fopen("foreign", "r"); }\n'
        ),
        "runner-startup-edge": (
            'extern "C" int system(const char*);\n'
            '__attribute__((constructor)) void NegativeStartup() { system("true"); }\n'
        ),
        "runner-forced-branch-edge": (
            "#if defined(APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING)\n"
            'extern "C" int open(const char*, int, ...);\n'
            '[[maybe_unused]] int NegativeForcedEdge() { return open("foreign", 0); }\n#endif\n'
        ),
    }
    namespace_anchor = "namespace apgar::benchmark::internal {\n"
    if name in producer_payloads:
        mutant = _insert_after(
            producer_source,
            namespace_anchor,
            producer_payloads[name],
            name,
        )
        roots, mutant_path = _dump_mutant_ast(
            inventories["test_support"], mutant, "apgar::benchmark::internal"
        )
        target_namespaces = [
            root
            for root in roots
            if root.get("kind") == "NamespaceDecl"
            and (
                _declaration_file(root) == _PRODUCER_HEADER
                or pathlib.Path(_declaration_file(root)) == mutant_path
            )
        ]
        _require(
            len(target_namespaces) == 2,
            "ADR067-NEGATIVE-MUTANT-COMPILE: producer mutant target namespace inventory drifted",
        )
        _validate_producer_semantic_surface(target_namespaces)
        _validate_producer_text(mutant, producer_header)
        return
    if name in direct_payloads:
        mutant = _insert_after(
            direct_source,
            "namespace {\n",
            direct_payloads[name],
            name,
        )
        roots, _ = _dump_mutant_ast(inventories["direct_test"], mutant, "(anonymous namespace)")
        _validate_direct_test_semantic_surface(roots)
        _validate_test_source(mutant)
        return
    if name == "direct-test-main":
        mutant = f"{direct_source}\nint main() {{ return 0; }}\n"
        roots, mutant_path = _dump_mutant_ast(inventories["direct_test"], mutant, "main")
        target_mains = [
            root
            for root in roots
            if root.get("kind") == "FunctionDecl"
            and root.get("name") == "main"
            and pathlib.Path(_declaration_file(root)) == mutant_path
        ]
        _require(
            not target_mains,
            "ADR067-DIRECT-MAIN-AST: direct test gained a target-owned main",
        )
        return
    if name == "disabled-test-registration":
        _require(_TEST_CASES[0] in direct_source, "disabled-test baseline drifted")
        mutant = direct_source.replace(_TEST_CASES[0], f"DISABLED_{_TEST_CASES[0]}", 1)
        roots, _ = _dump_mutant_ast(inventories["direct_test"], mutant, "(anonymous namespace)")
        registration_names = [
            str(root.get("name"))
            for root in roots
            if root.get("kind") == "CXXRecordDecl" and str(root.get("name", "")).endswith("_Test")
        ]
        _validate_direct_registration_names(registration_names)
        return
    if name in runner_payloads:
        mutant = _insert_after(
            runner_source,
            "namespace {\n",
            runner_payloads[name],
            name,
        )
        role = "forced_runner" if name == "runner-forced-branch-edge" else "runner"
        anonymous_roots, _ = _dump_mutant_ast(inventories[role], mutant, "(anonymous namespace)")
        main_roots, _ = _dump_mutant_ast(inventories[role], mutant, "main")
        _validate_runner_semantic_surface([*anonymous_roots, *main_roots])
        _validate_runner_source(mutant)
        return
    raise AuditError(f"unknown source negative case: {name}")


def _run_producer_tu_negative_case(
    name: str,
    target_namespaces: Sequence[Mapping[str, Any]],
) -> None:
    mutant = copy.deepcopy(list(target_namespaces))
    header_namespace = next(root for root in mutant if _declaration_file(root) == _PRODUCER_HEADER)
    source_namespace = next(root for root in mutant if _declaration_file(root) == _PRODUCER_SOURCE)
    if name == "producer-extra-function":
        source_namespace["inner"].append(
            {
                "kind": "FunctionDecl",
                "name": "NegativeExtraProducerFunction",
                "type": {"qualType": "void ()"},
                "inner": [{"kind": "CompoundStmt", "inner": []}],
            }
        )
        _validate_producer_namespace_inventory(header_namespace, source_namespace)
        return
    if name == "producer-ast-mismatch":
        composite = next(
            child for child in source_namespace.get("inner", ()) if child.get("name") == _COMPOSITE
        )
        composite.setdefault("type", {})["qualType"] = "void ()"
        _validate_producer_tu_identity(mutant)
        return
    raise AuditError(f"unknown producer TU negative case: {name}")


def _mutate_first_reference(value: Mapping[str, Any], old: str, new: str) -> None:
    for node in _objects(value):
        referenced = node.get("referencedDecl")
        if (
            node.get("kind") == "DeclRefExpr"
            and isinstance(referenced, dict)
            and referenced.get("name") == old
        ):
            referenced["name"] = new
            return
    raise AuditError(f"negative probe baseline has no {old} reference")


def _run_ast_negative_case(
    name: str,
    identity: Mapping[str, Any],
    composite: Mapping[str, Any],
) -> None:
    if name in {"carrier-first-kOrdinary", "carrier-second-kOrdinary"}:
        mutant = copy.deepcopy(composite)
        controller = next(
            call for call in _calls(mutant) if _CONTROLLER in _call_callee_names(call)
        )
        argument_index = 1 if name == "carrier-first-kOrdinary" else 2
        _mutate_first_reference(controller["inner"][argument_index], "kSameRun", "kOrdinary")
        _validate_composite(mutant)
        return
    if name in {"source-copied", "source-substituted"}:
        mutant = copy.deepcopy(composite)
        controller = next(
            call for call in _calls(mutant) if _CONTROLLER in _call_callee_names(call)
        )
        if name == "source-copied":
            original = controller["inner"][3]
            controller["inner"][3] = {
                "kind": "CXXConstructExpr",
                "type": {"qualType": "Phase4H4096SessionV5ControllerSourceAssociation"},
                "inner": [original],
            }
        else:
            _mutate_first_reference(controller["inner"][3], "source", "substituted_source")
        _validate_composite(mutant)
        return
    mutant = copy.deepcopy(identity)
    children = mutant["inner"]
    friends = [child for child in children if child.get("kind") == "FriendDecl"]
    _require(len(friends) == 1, "negative equality probe lost its baseline friend")
    operator = next(
        child
        for child in friends[0].get("inner", ())
        if child.get("kind") == "FunctionDecl" and child.get("name") == "operator=="
    )
    if name == "identity-extra-field":
        children.append({"kind": "FieldDecl", "name": "foreign", "type": {"qualType": "int"}})
    elif name == "equality-missing":
        mutant["inner"] = [child for child in children if child.get("kind") != "FriendDecl"]
    elif name == "equality-non-defaulted":
        operator["explicitlyDefaulted"] = "deleted"
    elif name == "equality-duplicated":
        children.append(copy.deepcopy(friends[0]))
    elif name == "equality-custom-partial":
        operator.pop("explicitlyDefaulted", None)
        operator["inner"].append({"kind": "CompoundStmt", "inner": []})
    elif name == "equality-additional-operator":
        children.append({"kind": "CXXMethodDecl", "name": "operator!=", "isImplicit": False})
    else:
        raise AuditError(f"unknown structured AST negative case: {name}")
    _validate_identity(mutant)


def _run_composite_source_negative_case(
    name: str,
    inventory: Mapping[str, Any],
    producer_source: str,
) -> None:
    start_marker = f"  return {_CONTROLLER}("
    start = producer_source.find(start_marker)
    _require(start >= 0, "composite negative source baseline lost its controller return")
    end_marker = "\n      source);"
    end = producer_source.find(end_marker, start)
    _require(end >= 0, "composite negative source baseline lost its source argument")
    end += len(end_marker)
    canonical_return = producer_source[start:end]
    canonical_call = canonical_return.strip().removeprefix("return ").removesuffix(";")
    if name == "controller-wrapped":
        replacement = f"  return ({canonical_call});"
    elif name == "controller-indirect":
        indirect_call = canonical_call.replace(_CONTROLLER, f"(&{_CONTROLLER})", 1)
        replacement = f"  return {indirect_call};"
    elif name == "controller-duplicated":
        replacement = f"  (void){canonical_call};\n  return {canonical_call};"
    elif name == "controller-conditional":
        replacement = (
            f"  return source.source_stamped ? {canonical_call} : "
            "std::optional<Phase4PairedTrialError>{};"
        )
    elif name == "controller-nonterminal":
        replacement = (
            f"  const auto controller_result = {canonical_call};\n  return controller_result;"
        )
    elif name == "controller-continuation":
        replacement = (
            f"  const auto controller_result = {canonical_call};\n"
            "  const bool continued_after_controller = true;\n"
            "  (void)continued_after_controller;\n"
            "  return controller_result;"
        )
    else:
        raise AuditError(f"unknown concrete composite negative case: {name}")
    mutant_source = producer_source[:start] + replacement + producer_source[end:]
    roots, mutant_path = _dump_mutant_ast(
        inventory,
        mutant_source,
        "apgar::benchmark::internal",
    )
    source_namespaces = [
        root
        for root in roots
        if root.get("kind") == "NamespaceDecl"
        and pathlib.Path(_declaration_file(root)) == mutant_path
    ]
    _require(
        len(source_namespaces) == 1,
        "ADR067-NEGATIVE-MUTANT-COMPILE: composite mutant source namespace drifted",
    )
    composites = [
        child
        for child in source_namespaces[0].get("inner", ())
        if child.get("kind") == "FunctionDecl" and child.get("name") == _COMPOSITE
    ]
    _require(
        len(composites) == 1,
        "ADR067-NEGATIVE-MUTANT-COMPILE: composite mutant definition drifted",
    )
    _validate_composite(composites[0])


def _run_equality_out_of_line_negative_case(
    inventory: Mapping[str, Any],
    producer_source: str,
    producer_header: str,
) -> None:
    canonical_equality = (
        "  friend bool operator==(const "
        f"{_IDENTITY}&,\n"
        "                         const "
        f"{_IDENTITY}&) = default;"
    )
    _require(
        producer_header.count(canonical_equality) == 1,
        "ADR067-NEGATIVE-MUTANT-COMPILE: equality source baseline drifted",
    )
    mutant_header = producer_header.replace(
        canonical_equality,
        canonical_equality.removesuffix(" = default;") + ";",
        1,
    )
    definition = (
        f"\nbool operator==(const {_IDENTITY}&,\n                const {_IDENTITY}&) = default;\n"
    )
    mutant_source = _insert_after(
        producer_source,
        "namespace apgar::benchmark::internal {\n",
        definition,
        "equality-out-of-line",
    )
    roots, mutant_header_path, mutant_source_path = _dump_mutant_producer_ast(
        inventory,
        mutant_source,
        mutant_header,
        "apgar::benchmark::internal",
    )
    target_namespaces = [
        root
        for root in roots
        if root.get("kind") == "NamespaceDecl"
        and pathlib.Path(_declaration_file(root)) in {mutant_header_path, mutant_source_path}
    ]
    _require(
        len(target_namespaces) == 2,
        "ADR067-NEGATIVE-MUTANT-COMPILE: out-of-line equality namespace inventory drifted",
    )
    _validate_identity_equality_ownership(target_namespaces)


def _run_inventory_negative_case(
    name: str,
    inventories: Mapping[str, Mapping[str, Any]],
    configuration: str,
) -> None:
    role = "direct_test"
    mutant = copy.deepcopy(inventories[role])
    attributes = mutant["attributes"]
    arguments = mutant["commands"][0]["arguments"]
    if name == "ubsan-no-recover-missing":
        flags = [
            flag
            for flag in arguments
            if not str(flag).startswith("-fsanitize=")
            and "sanitize-recover" not in str(flag)
            and "sanitize-ignorelist" not in str(flag)
        ]
        flags.extend(
            [
                "-fsanitize=undefined",
                "-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt",
            ]
        )
        _validate_sanitizer_compile_flags(role, flags, "ubsan")
        return
    elif name == "ubsan-recover-enabled":
        flags = [
            flag
            for flag in arguments
            if not str(flag).startswith("-fsanitize=")
            and "sanitize-recover" not in str(flag)
            and "sanitize-ignorelist" not in str(flag)
        ]
        flags.extend(
            [
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
                "-fsanitize-recover=undefined",
                "-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt",
            ]
        )
        _validate_sanitizer_compile_flags(role, flags, "ubsan")
        return
    elif name in {
        "sanitizer-disabling-flag",
        "ignorelist-additional-flag",
        "ignorelist-substituted-file",
    }:
        flags = [
            flag
            for flag in arguments
            if not str(flag).startswith("-fsanitize=")
            and not str(flag).startswith("-fno-sanitize=")
            and "sanitize-recover" not in str(flag)
            and "sanitize-ignorelist" not in str(flag)
            and "sanitize-blacklist" not in str(flag)
        ]
        ignorelist_flag = (
            "-fsanitize-ignorelist=foreign/sanitizer-ignorelist.txt"
            if name == "ignorelist-substituted-file"
            else "-fsanitize-ignorelist=external/llvm+/sanitizers/ubsan_ignore.txt"
        )
        flags.extend(
            [
                "-fsanitize=undefined",
                "-fno-sanitize-recover=all",
                ignorelist_flag,
            ]
        )
        if name == "sanitizer-disabling-flag":
            flags.append("-fno-sanitize=undefined")
        elif name == "ignorelist-additional-flag":
            flags.append("-fsanitize-blacklist=foreign/sanitizer-ignorelist.txt")
        _validate_sanitizer_compile_flags(role, flags, "ubsan")
        return
    elif name == "ubsan-options-missing":
        attributes["env"] = {}
        _validate_runtime_environment(role, attributes, "ubsan")
        return
    elif name == "ubsan-options-weakened":
        attributes["env"] = {"UBSAN_OPTIONS": "halt_on_error=0"}
        _validate_runtime_environment(role, attributes, "ubsan")
        return
    elif name == "linkstamp-added":
        attributes["linkstamp"] = ["foreign_linkstamp.cc"]
    elif name == "additional-linker-input-added":
        attributes["additional_linker_inputs"] = ["foreign.o"]
    elif name == "unexpected-link-option":
        attributes["linkopts"].append("-Wl,--build-id=none")
    elif name == "unexpected-feature":
        mutant["requested_features"] = ["adr067_unexpected_feature"]
    elif name == "target-source-added":
        mutant["sources"].append("foreign.cc")
    elif name == "target-source-substituted":
        mutant["sources"][0] = "foreign.cc"
    elif name == "gtest-filter-argument":
        attributes["args"] = ["--gtest_filter=*Canonical*"]
    elif name == "gtest-filter-environment":
        attributes["env"] = {"GTEST_FILTER": "*Canonical*"}
    elif name == "manual-tag":
        attributes["tags"] = ["manual"]
    elif name == "flaky-enabled":
        attributes["flaky"] = True
    elif name == "local-enabled":
        attributes["local"] = True
    elif name == "sharding-enabled":
        attributes["shard_count"] = 2
    elif name == "gtest-main-missing":
        mutant["direct_dependency_labels"] = [
            label
            for label in mutant["direct_dependency_labels"]
            if label != "@@googletest+//:gtest_main"
        ]
    elif name == "gtest-main-duplicated":
        mutant["direct_dependency_labels"].append("@@googletest+//:gtest_main")
    elif name == "gtest-main-substituted":
        mutant["direct_dependency_labels"] = [
            "@@googletest+//:gtest" if label == "@@googletest+//:gtest_main" else label
            for label in mutant["direct_dependency_labels"]
        ]
    else:
        raise AuditError(f"unknown inventory negative case: {name}")
    _validate_target_inventory(role, mutant, configuration)


def _run_role_negative_case(
    name: str,
    role_rows: Sequence[Mapping[str, Any]],
) -> None:
    if name != "configuration-audit-missing":
        raise AuditError(f"unknown semantic-role negative case: {name}")
    mutant = [copy.deepcopy(row) for row in role_rows if row.get("role") != "test_support"]
    _validate_semantic_role_rows(mutant)


def _run_ignorelist_negative_case(
    name: str,
    ignorelist: pathlib.Path,
    audited_sources: Iterable[str],
) -> None:
    content = ignorelist.read_bytes()
    if name == "ignorelist-source-match":
        content = content.replace(
            b"src:external/llvm+/3rd_party/libc/glibc/csu/elf-init-2.31.c",
            f"src:{_PRODUCER_SOURCE}".encode(),
        )
    elif name == "ignorelist-missing":
        content = b""
    elif name == "ignorelist-substituted":
        content = content.replace(b"elf-init-2.31.c", b"foreign-init.cc")
    elif name == "ignorelist-additional":
        content += b"src:foreign.cc\n"
    elif name == "ignorelist-reordered":
        lines = content.splitlines(keepends=True)
        content = b"".join([lines[0], lines[2], lines[1]])
    elif name == "ignorelist-digest-drift":
        content += b"# digest drift\n"
    elif name == "ignorelist-extra-suppression":
        content += b"src:**\n"
    else:
        raise AuditError(f"unknown ignorelist negative case: {name}")
    if name in {"ignorelist-source-match", "ignorelist-extra-suppression"}:
        _validate_ignorelist_semantics(content, audited_sources)
        return
    if name == "ignorelist-digest-drift":
        _require(
            hashlib.sha256(content).hexdigest() == _IGNORELIST_SHA256,
            "UBSan ignorelist digest drifted",
        )
        return
    with tempfile.TemporaryDirectory(prefix="apgar-adr067-ignorelist-") as directory:
        path = pathlib.Path(directory) / "external/llvm+/sanitizers/ubsan_ignore.txt"
        path.parent.mkdir(parents=True)
        path.write_bytes(content)
        _validate_ignorelist(path, audited_sources)


def _run_provider_negative_case(
    name: str,
    inventory_path: pathlib.Path,
    proof_path: pathlib.Path,
    configuration: str,
) -> None:
    if name not in {
        "allowlisted-owner-initializer",
        "foreign-alwayslink-initializer",
        "gtest-initializer-missing",
        "gtest-initializer-duplicated",
        "gtest-initializer-substituted",
        "gtest-initializer-additional",
    }:
        raise AuditError(f"unknown provider negative case: {name}")
    _validate_provider_evidence(
        [inventory_path],
        [proof_path],
        configuration,
        "negative",
    )
    diagnostics = [
        line.removeprefix("diagnostic=")
        for line in proof_path.read_text(encoding="utf-8").splitlines()
        if line.startswith("diagnostic=")
    ]
    _require(
        name not in diagnostics,
        f"ADR067-PROVIDER-DIAGNOSTIC: authenticated startup rejection {name}",
    )


def _run_stamp_negative_case(name: str, configuration: str) -> None:
    if name == "stamp-content-wrong":
        _validate_stamp_bytes(b"foreign\n", "negative", configuration)
        return
    if name == "stamp-configuration-wrong":
        _validate_stamp_bytes(
            f"{_NEGATIVE_STAMP}\nconfiguration=foreign\n".encode(),
            "negative",
            configuration,
        )
        return
    with tempfile.TemporaryDirectory(prefix="apgar-adr067-stamp-") as directory:
        root = pathlib.Path(directory)
        basename = (
            "wrong.ok"
            if name == "stamp-basename-wrong"
            else "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok"
        )
        path = root / basename
        _write_stamp(path, "negative", configuration)


def _run_negative_cases(
    case_names: Sequence[str],
    role_rows: Sequence[Mapping[str, Any]],
    inventories: Mapping[str, Mapping[str, Any]],
    configuration: str,
    ignorelist: pathlib.Path,
    audited_sources: Iterable[str],
    provider_inventory: pathlib.Path,
    provider_proof: pathlib.Path,
    analysis_diagnostics: Sequence[str],
) -> None:
    identity, composite, producer_namespaces = _producer_negative_nodes(inventories["production"])
    direct_roots = _direct_test_negative_roots(inventories["direct_test"])
    ast_cases = {
        "carrier-first-kOrdinary",
        "carrier-second-kOrdinary",
        "source-copied",
        "source-substituted",
        "identity-extra-field",
        "equality-missing",
        "equality-non-defaulted",
        "equality-duplicated",
        "equality-custom-partial",
        "equality-additional-operator",
    }
    composite_source_cases = {
        "controller-wrapped",
        "controller-indirect",
        "controller-duplicated",
        "controller-conditional",
        "controller-nonterminal",
        "controller-continuation",
    }
    equality_source_cases = {"equality-out-of-line"}
    role_cases = {"configuration-audit-missing"}
    inventory_cases = {
        "ubsan-no-recover-missing",
        "ubsan-recover-enabled",
        "ubsan-options-missing",
        "ubsan-options-weakened",
        "sanitizer-disabling-flag",
        "ignorelist-additional-flag",
        "ignorelist-substituted-file",
        "linkstamp-added",
        "additional-linker-input-added",
        "unexpected-link-option",
        "unexpected-feature",
        "target-source-added",
        "target-source-substituted",
        "gtest-filter-argument",
        "gtest-filter-environment",
        "manual-tag",
        "flaky-enabled",
        "local-enabled",
        "sharding-enabled",
        "gtest-main-missing",
        "gtest-main-duplicated",
        "gtest-main-substituted",
    }
    ignorelist_cases = {
        "ignorelist-source-match",
        "ignorelist-missing",
        "ignorelist-substituted",
        "ignorelist-additional",
        "ignorelist-reordered",
        "ignorelist-digest-drift",
        "ignorelist-extra-suppression",
    }
    ubsan_result_cases = {
        "ubsan-live-probe-zero-exit",
        "ubsan-live-probe-sentinel",
        "ubsan-zero-status-diagnostic",
    }
    provider_cases = {
        "allowlisted-owner-initializer",
        "foreign-alwayslink-initializer",
        "gtest-initializer-missing",
        "gtest-initializer-duplicated",
        "gtest-initializer-substituted",
        "gtest-initializer-additional",
    }
    stamp_cases = {
        "stamp-basename-wrong",
        "stamp-content-wrong",
        "stamp-configuration-wrong",
    }
    analysis_cases = {
        "stamp-owner-wrong",
        "audit-output-unrequested",
        "audit-output-missing",
        "stamp-stale-input",
    }
    direct_semantic_cases = {
        "direct-test-direct-acquisition",
        "direct-test-local-wrapper-acquisition",
        "direct-test-indirect-acquisition",
        "direct-test-external-apgar-method",
        "direct-test-external-apgar-constructor",
        "direct-test-external-apgar-operator",
        "producer-function-shadow",
        "allocator-global-initializer",
        "implicit-acquisition-destructor",
        "implicit-allocation-edge",
        "gtest-subprocess-internal",
        "direct-test-process-edge",
        "direct-test-file-edge",
        "direct-test-environment-edge",
        "direct-test-dynamic-loading-edge",
        "direct-test-raw-syscall-edge",
        "direct-test-inline-assembly-edge",
    }
    producer_tu_cases = {"producer-ast-mismatch", "producer-extra-function"}
    direct_control_flow_cases = {"test-early-return", "test-dead-branch"}
    source_cases = {
        "producer-asan-conditional": _PRODUCER_SOURCE,
        "producer-ubsan-conditional": _PRODUCER_SOURCE,
        "no-sanitize-attribute": _PRODUCER_SOURCE,
        "no-sanitize-address-attribute": _PRODUCER_SOURCE,
        "disable-sanitizer-instrumentation-attribute": _PRODUCER_SOURCE,
        "sanitizer-optout-pragma": _PRODUCER_SOURCE,
        "ubsan-default-options-hook": _PRODUCER_SOURCE,
        "ubsan-report-hook": _PRODUCER_SOURCE,
        "ubsan-runtime-interposition": _PRODUCER_SOURCE,
        "gtest-expect-death": _DIRECT_TEST_SOURCE,
        "gtest-assert-death": _DIRECT_TEST_SOURCE,
        "gtest-expect-exit": _DIRECT_TEST_SOURCE,
        "gtest-assert-exit": _DIRECT_TEST_SOURCE,
        "gtest-death-local-alias": _DIRECT_TEST_SOURCE,
        "direct-test-sanitizer-conditional": _DIRECT_TEST_SOURCE,
        "direct-test-main": _DIRECT_TEST_SOURCE,
        "producer-alias": _PRODUCER_SOURCE,
        "producer-indirect-function": _PRODUCER_SOURCE,
        "producer-init-fini-section": _PRODUCER_SOURCE,
        "producer-system-initializer": _PRODUCER_SOURCE,
        "runner-process-edge": _RUNNER_SOURCE,
        "runner-file-edge": _RUNNER_SOURCE,
        "runner-startup-edge": _RUNNER_SOURCE,
        "runner-forced-branch-edge": _RUNNER_SOURCE,
        "disabled-test-registration": _DIRECT_TEST_SOURCE,
    }
    producer_source = _read_source(_PRODUCER_SOURCE)
    producer_header = _read_source(_PRODUCER_HEADER)
    direct_source = _read_source(_DIRECT_TEST_SOURCE)
    runner_source = _read_source(_RUNNER_SOURCE)
    probes: dict[str, Any] = {}
    for name in ast_cases:
        probes[name] = lambda name=name: _run_ast_negative_case(name, identity, composite)
    for name in composite_source_cases:
        probes[name] = lambda name=name: _run_composite_source_negative_case(
            name, inventories["production"], producer_source
        )
    for name in equality_source_cases:
        probes[name] = lambda: _run_equality_out_of_line_negative_case(
            inventories["production"], producer_source, producer_header
        )
    for name in role_cases:
        probes[name] = lambda name=name: _run_role_negative_case(name, role_rows)
    for name in inventory_cases:
        probes[name] = lambda name=name: _run_inventory_negative_case(
            name, inventories, configuration
        )
    for name in ignorelist_cases:
        probes[name] = lambda name=name: _run_ignorelist_negative_case(
            name, ignorelist, audited_sources
        )
    for name in ubsan_result_cases:
        if name == "ubsan-live-probe-sentinel":
            probes[name] = lambda: _validate_ubsan_probe_result(
                1, _SENTINEL, "runtime error: signed integer overflow"
            )
        elif name == "ubsan-zero-status-diagnostic":
            probes[name] = lambda: _validate_ubsan_probe_result(
                0, "", "runtime error: signed integer overflow"
            )
        else:
            probes[name] = lambda: _validate_ubsan_probe_result(0, "", "")
    for name in provider_cases:
        probes[name] = lambda name=name: _run_provider_negative_case(
            name, provider_inventory, provider_proof, configuration
        )
    for name in stamp_cases:
        probes[name] = lambda name=name: _run_stamp_negative_case(name, configuration)
    for name in analysis_cases:
        probes[name] = lambda name=name: _require(
            name not in analysis_diagnostics,
            f"ADR067-ANALYSIS-DIAGNOSTIC: authenticated analysis rejection {name}",
        )
    for name in direct_semantic_cases:
        probes[name] = lambda name=name: _run_direct_semantic_negative_case(name, direct_roots)
    for name in direct_control_flow_cases:
        probes[name] = lambda name=name: _run_direct_control_flow_negative_case(name, direct_roots)
    for name in producer_tu_cases:
        probes[name] = lambda name=name: _run_producer_tu_negative_case(name, producer_namespaces)
    for name in source_cases:
        probes[name] = lambda name=name: _run_source_negative_case(
            name,
            inventories,
            producer_source,
            producer_header,
            direct_source,
            runner_source,
        )

    _require(
        set(probes) == set(case_names) and len(probes) == len(case_names),
        "negative case dispatch does not exactly cover the authenticated roster: "
        f"missing={sorted(set(case_names) - set(probes))} "
        f"extra={sorted(set(probes) - set(case_names))}",
    )
    diagnostic_identities = _negative_diagnostic_identities(configuration)
    _require(
        set(diagnostic_identities) == set(case_names),
        "negative diagnostic identity matrix does not exactly cover the authenticated roster: "
        f"missing={sorted(set(case_names) - set(diagnostic_identities))} "
        f"extra={sorted(set(diagnostic_identities) - set(case_names))}",
    )
    for name in case_names:
        _expect_negative_rejection(
            name,
            probes[name],
            diagnostic_identities[name],
        )


def _write_stamp(path: pathlib.Path, mode: str, configuration: str) -> None:
    invariant = _POSITIVE_STAMP if mode == "positive" else _NEGATIVE_STAMP
    content = f"{invariant}\nconfiguration={configuration}\n"
    expected_basename = (
        "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.ok"
        if mode == "positive"
        else "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok"
    )
    _require(path.name == expected_basename, "semantic-audit stamp basename drifted")
    _validate_stamp_bytes(content.encode("utf-8"), mode, configuration)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(content)
    except OSError as error:
        raise AuditError(
            f"cannot exclusively create semantic-audit stamp {path}: {error}"
        ) from error


def _validate_stamp_bytes(content: bytes, mode: str, configuration: str) -> None:
    invariant = _POSITIVE_STAMP if mode == "positive" else _NEGATIVE_STAMP
    _require(
        content == f"{invariant}\nconfiguration={configuration}\n".encode(),
        "semantic-audit stamp exact content/configuration drifted",
    )


def _validate_semantic_role_rows(value: Any) -> list[Mapping[str, Any]]:
    _require(
        isinstance(value, list)
        and [row.get("role") for row in value if isinstance(row, dict)]
        == ["production", "test_support", "direct_test", "runner", "forced_runner"],
        "ADR067-CONFIGURATION-AUDIT-COVERAGE: semantic manifest role inventory/order drifted",
    )
    _require(
        all(
            set(row) == {"inventory", "label", "role"}
            and isinstance(row.get("inventory"), str)
            and bool(row.get("inventory"))
            and isinstance(row.get("label"), str)
            and bool(row.get("label"))
            for row in value
        ),
        "ADR067-CONFIGURATION-AUDIT-COVERAGE: semantic manifest role evidence schema drifted",
    )
    return value


def _parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("positive", "negative"), required=True)
    parser.add_argument("--configuration", choices=("normal", "asan", "ubsan"), required=True)
    parser.add_argument("--manifest", type=pathlib.Path, required=True)
    parser.add_argument("--ignorelist", type=pathlib.Path, required=True)
    parser.add_argument("--stamp", type=pathlib.Path, required=True)
    parser.add_argument("--fixture", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--ubsan-live-probe", type=pathlib.Path)
    parser.add_argument("--provider-inventory", type=pathlib.Path, action="append", default=[])
    parser.add_argument("--provider-proof", type=pathlib.Path, action="append", default=[])
    return parser.parse_args(argv)


def _main(argv: Sequence[str]) -> int:
    args = _parse_args(argv)
    manifest = _read_json(args.manifest)
    _require(
        set(manifest)
        == {"analysis_diagnostics", "configuration", "kind", "roles", "schema_version"},
        "semantic manifest schema surface drifted",
    )
    _require(manifest.get("schema_version") == 1, "semantic manifest schema drifted")
    _require(manifest.get("configuration") == args.configuration, "manifest configuration drifted")
    _require(manifest.get("kind") == args.mode, "manifest audit kind drifted")
    expected_analysis_diagnostics = (
        [
            "stamp-owner-wrong",
            "audit-output-missing",
            "audit-output-unrequested",
            "stamp-stale-input",
        ]
        if args.mode == "negative"
        else []
    )
    _require(
        manifest.get("analysis_diagnostics") == expected_analysis_diagnostics,
        "semantic manifest analysis-diagnostic evidence drifted",
    )
    role_rows = _validate_semantic_role_rows(manifest.get("roles"))
    inventories: dict[str, Mapping[str, Any]] = {}
    for row in role_rows:
        role = row["role"]
        inventory = _read_json(pathlib.Path(row["inventory"]))
        _require(inventory.get("label") == row.get("label"), f"{role} manifest label drifted")
        _validate_target_inventory(role, inventory, args.configuration)
        inventories[role] = inventory

    audited_sources = {
        source for inventory in inventories.values() for source in inventory.get("sources", ())
    }
    audited_sources.add(
        "tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit/ubsan_signed_overflow.cc"
    )
    _validate_ignorelist(args.ignorelist, audited_sources)

    production_fingerprint = _audit_producer(inventories["production"])
    support_fingerprint = _audit_producer(inventories["test_support"])
    _require(
        production_fingerprint == support_fingerprint,
        "production/test-support identity, equality, builder, or composite AST differs: "
        f"production={production_fingerprint} test_support={support_fingerprint}",
    )
    _audit_direct_test(inventories["direct_test"])
    _run_direct_test(inventories["direct_test"], args.configuration)
    _audit_runners(inventories["runner"], inventories["forced_runner"])

    _validate_provider_evidence(
        args.provider_inventory,
        args.provider_proof,
        args.configuration,
        args.mode,
    )

    _, negative_case_names = _validate_negative_fixtures(args.fixture)
    if args.mode == "negative":
        _require(
            len(args.provider_inventory) == 1 and len(args.provider_proof) == 1,
            "negative semantic audit requires its authenticated provider evidence",
        )
        _run_negative_cases(
            negative_case_names,
            role_rows,
            inventories,
            args.configuration,
            args.ignorelist,
            audited_sources,
            args.provider_inventory[0],
            args.provider_proof[0],
            expected_analysis_diagnostics,
        )
    if args.mode == "negative" and args.configuration == "ubsan":
        _require(args.ubsan_live_probe is not None, "UBSan negative audit omitted its live probe")
        _run_live_ubsan_probe(args.ubsan_live_probe)
    else:
        _require(args.ubsan_live_probe is None, "unrequested configuration received a live probe")
    _write_stamp(args.stamp, args.mode, args.configuration)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(_main(sys.argv[1:]))
    except AuditError as error:
        print(f"semantic audit failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
