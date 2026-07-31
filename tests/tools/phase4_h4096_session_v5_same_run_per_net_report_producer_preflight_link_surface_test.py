from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import subprocess
import sys
import unittest
from collections import Counter
from typing import Any

_STARTUP_SECTION_TYPES = frozenset(("SHT_PREINIT_ARRAY", "SHT_INIT_ARRAY", "SHT_FINI_ARRAY"))
_SANITIZER_HOOK_FRAGMENTS = (
    "__asan_default_options",
    "__asan_on_error",
    "__asan_set_error_report_callback",
    "__sanitizer_report_error_summary",
    "__sanitizer_set_report_fd",
    "__sanitizer_set_report_path",
    "__ubsan_default_options",
    "__ubsan_get_current_report_data",
    "__ubsan_on_report",
)
_EXPECTED_GTEST_STARTUP_OBJECTS = {
    "gmock.cc": (
        "@@googletest+//:gtest",
        "../googletest+/_objs/gtest/gmock.pic.o",
    ),
    "gtest.cc": (
        "@@googletest+//:gtest",
        "../googletest+/_objs/gtest/gtest.pic.o",
    ),
    "gtest-death-test.cc": (
        "@@googletest+//:gtest",
        "../googletest+/_objs/gtest/gtest-death-test.pic.o",
    ),
}
_DIRECT_TEST_LINK_OWNER = (
    "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
)
_DIRECT_TEST_OBJECT_SHORT_PATH = (
    "_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test/"
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.pic.o"
)
_DIRECT_TEST_REGISTRATION_INITIALIZER = (
    "_GLOBAL__sub_I_phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.cc"
)
_DIRECT_TEST_ELF_SHORT_PATH = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
)
_EXPECTED_AUDITED_TARGETS = (
    "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
    "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support",
    "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
    "@@//:phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
    "@@//:phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
)
_EXPECTED_LABEL_ATTRIBUTE_KEYS = frozenset(
    (
        "additional_compiler_inputs",
        "additional_linker_inputs",
        "data",
        "deps",
        "dynamic_deps",
        "hdrs",
        "implementation_deps",
        "link_extra_lib",
        "linkstamp",
        "malloc",
        "srcs",
        "target_compatible_with",
        "textual_hdrs",
    )
)
_EXPECTED_VALUE_ATTRIBUTE_KEYS = frozenset(
    (
        "alwayslink",
        "copts",
        "defines",
        "features",
        "include_prefix",
        "includes",
        "linkopts",
        "linkshared",
        "linkstatic",
        "local_defines",
        "nocopts",
        "stamp",
        "strip_include_prefix",
    )
)
_LLVM_READOBJ_RESOLVED_OWNER = (
    "@@llvm++llvm_toolchain_minimal+llvm-toolchain-minimal-22.1.8-linux-amd64//:bin/llvm-readobj"
)
_LLVM_READOBJ_CANONICAL_LABEL = "@@llvm+//tools:llvm-readobj"
_DIAGNOSTIC_ALLOWLISTED_OWNER_INITIALIZER = "allowlisted-owner-initializer"
_DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER = "foreign-alwayslink-initializer"
_DIAGNOSTIC_GTEST_INITIALIZER_MISSING = "gtest-initializer-missing"
_DIAGNOSTIC_GTEST_INITIALIZER_DUPLICATED = "gtest-initializer-duplicated"
_DIAGNOSTIC_GTEST_INITIALIZER_SUBSTITUTED = "gtest-initializer-substituted"
_DIAGNOSTIC_GTEST_INITIALIZER_ADDITIONAL = "gtest-initializer-additional"
_ALLOWLISTED_INITIALIZER_OWNER = "@@//:phase4_h4096_session_v5_execution_preflight_test_support"
_FOREIGN_ALWAYSLINK_INITIALIZER_OWNER = (
    "@@//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_"
    "audit_negative_contract_foreign_alwayslink_initializer"
)
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


def _llvm_readobj_json(
    llvm_readobj: pathlib.Path, artifact: pathlib.Path, *options: str
) -> list[dict[str, Any]]:
    completed = subprocess.run(
        [
            str(llvm_readobj),
            "--elf-output-style=JSON",
            *options,
            str(artifact),
        ],
        check=True,
        text=True,
        capture_output=True,
    )
    parsed = json.loads(completed.stdout)
    if not isinstance(parsed, list) or not parsed:
        raise ValueError(f"llvm-readobj returned no ELF records for {artifact}")
    return parsed


def _unit_startup_sections(unit: dict[str, Any]) -> tuple[tuple[str, str, int], ...]:
    result: list[tuple[str, str, int]] = []
    for wrapped in unit.get("Sections", []):
        section = wrapped["Section"]
        section_type = section["Type"]["Name"]
        size = section["Size"]
        if section_type in _STARTUP_SECTION_TYPES and size != 0:
            result.append((section["Name"]["Name"], section_type, size))
    return tuple(result)


def _retained_object_violation(
    owner: str,
    short_path: str,
    configuration: str,
    units: list[dict[str, Any]],
) -> str | None:
    startup = [
        (unit["FileSummary"]["File"], section)
        for unit in units
        for section in _unit_startup_sections(unit)
    ]
    hooks = [
        (unit["FileSummary"]["File"], symbol["Symbol"]["Name"]["Name"])
        for unit in units
        for symbol in unit.get("Symbols", [])
        if symbol["Symbol"]["Section"]["Name"] != "Undefined"
        and any(
            fragment in symbol["Symbol"]["Name"]["Name"] for fragment in _SANITIZER_HOOK_FRAGMENTS
        )
    ]
    if hooks:
        return f"retained object {owner} {short_path} contributed sanitizer hook: {hooks}"
    if not startup:
        return None

    symbols = [
        symbol["Symbol"]["Name"]["Name"]
        for unit in units
        for symbol in unit.get("Symbols", [])
        if symbol["Symbol"]["Section"]["Name"] != "Undefined"
    ]

    # AddressSanitizer emits a compiler-owned module constructor/destructor for
    # any input object that describes instrumented globals. Such archive
    # members are only available to the link; --gc-sections may discard their
    # startup fragments. Admit exactly that compiler-generated pair here, and
    # let the final-ELF audit below freeze every startup slot that survived.
    asan_only_sections = (
        (".init_array.1", "SHT_INIT_ARRAY", 8),
        (".fini_array.1", "SHT_FINI_ARRAY", 8),
    )
    actual_sections = tuple(section for _, section in startup)
    if (
        configuration == "asan"
        and actual_sections == asan_only_sections
        and symbols.count("asan.module_ctor") == 1
        and symbols.count("asan.module_dtor") == 1
    ):
        return None

    exact_gtest_objects = {value for value in _EXPECTED_GTEST_STARTUP_OBJECTS.values()}
    is_pinned_gtest = owner == "@@googletest+//:gtest"
    is_direct_test = (
        owner == _DIRECT_TEST_LINK_OWNER and short_path == _DIRECT_TEST_OBJECT_SHORT_PATH
    )
    is_exact_gtest = (owner, short_path) in exact_gtest_objects
    if is_pinned_gtest and not is_exact_gtest:
        # The checksum-pinned archive has additional registration sections that
        # the mandatory --gc-sections final link discards. The final-ELF audit
        # below freezes the exact three retained GoogleTest entries.
        return None
    if not is_direct_test and not is_exact_gtest:
        if owner == _ALLOWLISTED_INITIALIZER_OWNER:
            return f"allowlisted APGAR owner {owner} contributed startup: {startup}"
        return f"unallowlisted retained object {owner} {short_path} contributed startup: {startup}"

    expected_sections = (
        (
            (".init_array.1", "SHT_INIT_ARRAY", 8),
            (".init_array", "SHT_INIT_ARRAY", 8),
            (".fini_array.1", "SHT_FINI_ARRAY", 8),
        )
        if configuration == "asan"
        else ((".init_array", "SHT_INIT_ARRAY", 8),)
    )
    if actual_sections != expected_sections:
        return (
            f"exact retained startup object {owner} {short_path} section drift: "
            f"{actual_sections} != {expected_sections}"
        )

    expected_initializer = (
        _DIRECT_TEST_REGISTRATION_INITIALIZER
        if is_direct_test
        else {
            "../googletest+/_objs/gtest/gmock.pic.o": "_GLOBAL__sub_I_gmock.cc",
            "../googletest+/_objs/gtest/gtest.pic.o": "_GLOBAL__sub_I_gtest.cc",
            "../googletest+/_objs/gtest/gtest-death-test.pic.o": (
                "_GLOBAL__sub_I_gtest_death_test.cc"
            ),
        }[short_path]
    )
    if symbols.count(expected_initializer) != 1:
        return (
            f"exact retained startup object {owner} {short_path} registration drift: "
            f"{expected_initializer} multiplicity={symbols.count(expected_initializer)}"
        )
    return None


def _parse_startup_owner_audit_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--llvm-readobj", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    parser.add_argument("--stamp", required=True, type=pathlib.Path)
    return parser.parse_args(argv)


def _run_startup_owner_audit(argv: list[str]) -> int:
    args = _parse_startup_owner_audit_args(argv)
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    if manifest.get("schema_version") != 1:
        raise ValueError("startup owner manifest schema substitution")
    if manifest.get("kind") not in ("positive", "negative"):
        raise ValueError("startup owner manifest kind substitution")
    if manifest.get("configuration") not in ("normal", "asan", "ubsan"):
        raise ValueError("startup owner manifest configuration substitution")
    if manifest.get("llvm_readobj_label") != _LLVM_READOBJ_CANONICAL_LABEL:
        raise ValueError("startup owner manifest llvm-readobj label substitution")
    if manifest.get("llvm_readobj_resolved_owner") != _LLVM_READOBJ_RESOLVED_OWNER:
        raise ValueError("startup owner manifest llvm-readobj owner substitution")
    if args.llvm_readobj.name != "llvm-readobj":
        raise ValueError("startup owner audit did not receive pinned llvm-readobj")

    direct_test_elf = manifest.get("direct_test_elf")
    if (
        not isinstance(direct_test_elf, dict)
        or set(direct_test_elf) != {"owner", "path", "short_path"}
        or direct_test_elf.get("owner") != _DIRECT_TEST_LINK_OWNER
        or direct_test_elf.get("short_path") != _DIRECT_TEST_ELF_SHORT_PATH
        or not isinstance(direct_test_elf.get("path"), str)
        or not direct_test_elf["path"]
    ):
        raise ValueError("startup owner manifest direct-test ELF substitution")

    targets = manifest.get("targets")
    if (
        not isinstance(targets, list)
        or tuple(target.get("label") for target in targets) != _EXPECTED_AUDITED_TARGETS
    ):
        raise ValueError("startup owner manifest target order/identity substitution")
    for target in targets:
        if target.get("configuration_options") != _EXPECTED_CONFIGURATION_OPTIONS:
            raise ValueError(f"global compile/link option drift for {target['label']}")
        if frozenset(target.get("label_attributes", {})) != _EXPECTED_LABEL_ATTRIBUTE_KEYS:
            raise ValueError(f"link label-attribute schema drift for {target['label']}")
        if frozenset(target.get("value_attributes", {})) != _EXPECTED_VALUE_ATTRIBUTE_KEYS:
            raise ValueError(f"link value-attribute schema drift for {target['label']}")

    records = manifest.get("records")
    if not isinstance(records, list) or not records:
        raise ValueError("startup owner manifest has no linker artifacts")
    owners_by_path: dict[str, set[str]] = {}
    for record in records:
        owners_by_path.setdefault(record["path"], set()).add(record["owner"])
    ambiguous = {
        path: sorted(owners) for path, owners in owners_by_path.items() if len(owners) != 1
    }
    if ambiguous:
        raise ValueError(f"startup owner manifest has ambiguous artifacts: {ambiguous}")

    object_records = [record for record in records if record["path"].endswith(".o")]
    if not object_records:
        raise ValueError("startup owner manifest has no retained linker objects")
    units_by_path: dict[str, list[dict[str, Any]]] = {}
    for record in object_records:
        path = record["path"]
        if path not in units_by_path:
            units_by_path[path] = _llvm_readobj_json(
                args.llvm_readobj,
                pathlib.Path(path),
                "--sections",
                "--symbols",
            )
        violation = _retained_object_violation(
            record["owner"],
            record["short_path"],
            manifest["configuration"],
            units_by_path[path],
        )
        if violation is not None:
            raise ValueError(violation)

    exact_gtest_records = {
        (record["owner"], record["short_path"]): record
        for record in object_records
        if record["owner"] == "@@googletest+//:gtest"
    }
    for source, (owner, short_path) in _EXPECTED_GTEST_STARTUP_OBJECTS.items():
        record = exact_gtest_records.get((owner, short_path))
        if record is None:
            raise ValueError(f"missing exact GoogleTest startup object for {source}")
        sections = [
            section
            for unit in units_by_path[record["path"]]
            for section in _unit_startup_sections(unit)
        ]
        if not any(section[1] == "SHT_INIT_ARRAY" for section in sections):
            raise ValueError(f"GoogleTest startup object lost initializer: {short_path}")

    negative_fixture_provider = manifest.get("negative_fixture_provider")
    negative_fixtures = manifest.get("negative_fixtures")
    observed_diagnostics: list[str] = []
    if manifest["kind"] == "negative":
        if (
            not isinstance(negative_fixture_provider, dict)
            or set(negative_fixture_provider)
            != {"alwayslink", "artifacts", "linker_inputs", "target_label"}
            or negative_fixture_provider.get("alwayslink") is not True
            or negative_fixture_provider.get("target_label")
            != _FOREIGN_ALWAYSLINK_INITIALIZER_OWNER
            or not isinstance(negative_fixture_provider.get("linker_inputs"), list)
            or len(negative_fixture_provider["linker_inputs"]) != 1
            or not isinstance(negative_fixture_provider.get("artifacts"), list)
            or len(negative_fixture_provider["artifacts"]) != 2
        ):
            raise ValueError("negative startup audit lost its real CcInfo fixture")
        linker_input_record = negative_fixture_provider["linker_inputs"][0]
        if (
            not isinstance(linker_input_record, str)
            or not linker_input_record.startswith(f"owner={_FOREIGN_ALWAYSLINK_INITIALIZER_OWNER};")
            or "libraries=alwayslink=1," not in linker_input_record
            or "_foreign_alwayslink_initializer.pic.o" not in linker_input_record
            or "_foreign_alwayslink_initializer.lo" not in linker_input_record
        ):
            raise ValueError("negative startup audit CcInfo LinkerInput drifted")
        provider_artifacts: set[tuple[str, str]] = set()
        for artifact in negative_fixture_provider["artifacts"]:
            if (
                not isinstance(artifact, dict)
                or set(artifact) != {"owner", "path", "short_path"}
                or artifact.get("owner") != _FOREIGN_ALWAYSLINK_INITIALIZER_OWNER
                or not isinstance(artifact.get("path"), str)
                or not artifact["path"]
                or not pathlib.Path(artifact["path"]).is_file()
                or not isinstance(artifact.get("short_path"), str)
                or not artifact["short_path"]
            ):
                raise ValueError("negative startup audit CcInfo artifact drifted")
            provider_artifacts.add((artifact["path"], artifact["short_path"]))
        if (
            len(provider_artifacts) != 2
            or any(short_path not in linker_input_record for _, short_path in provider_artifacts)
            or sum(short_path.endswith(".lo") for _, short_path in provider_artifacts) != 1
            or sum(
                short_path.endswith("_foreign_alwayslink_initializer.pic.o")
                for _, short_path in provider_artifacts
            )
            != 1
        ):
            raise ValueError("negative startup audit CcInfo artifacts are not exact")
        expected_fixtures = (
            (
                False,
                _DIAGNOSTIC_ALLOWLISTED_OWNER_INITIALIZER,
                _ALLOWLISTED_INITIALIZER_OWNER,
                f"allowlisted APGAR owner {_ALLOWLISTED_INITIALIZER_OWNER} ",
            ),
            (
                True,
                _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER,
                _FOREIGN_ALWAYSLINK_INITIALIZER_OWNER,
                f"unallowlisted retained object {_FOREIGN_ALWAYSLINK_INITIALIZER_OWNER} ",
            ),
        )
        if not isinstance(negative_fixtures, list) or len(negative_fixtures) != len(
            expected_fixtures
        ):
            raise ValueError("negative startup audit lacks its live initializer fixtures")
        fixture_paths: set[tuple[str, str]] = set()
        for fixture, (alwayslink, diagnostic_id, owner, violation_prefix) in zip(
            negative_fixtures,
            expected_fixtures,
            strict=True,
        ):
            if (
                not isinstance(fixture, dict)
                or set(fixture) != {"alwayslink", "diagnostic_id", "owner", "path", "short_path"}
                or fixture.get("alwayslink") is not alwayslink
                or fixture.get("diagnostic_id") != diagnostic_id
                or fixture.get("owner") != owner
                or not isinstance(fixture.get("path"), str)
                or not fixture["path"]
                or not isinstance(fixture.get("short_path"), str)
                or not fixture["short_path"].endswith("_foreign_alwayslink_initializer.pic.o")
            ):
                raise ValueError(
                    f"negative startup audit fixture identity drifted: {diagnostic_id}"
                )
            fixture_paths.add((fixture["path"], fixture["short_path"]))
            fixture_units = _llvm_readobj_json(
                args.llvm_readobj,
                pathlib.Path(fixture["path"]),
                "--sections",
                "--symbols",
            )
            violation = _retained_object_violation(
                fixture["owner"],
                fixture["short_path"],
                manifest["configuration"],
                fixture_units,
            )
            if violation is None or not violation.startswith(violation_prefix):
                raise ValueError(f"[{diagnostic_id}] live initializer classifier did not fire")
            observed_diagnostics.append(diagnostic_id)
        if len(fixture_paths) != 1:
            raise ValueError("negative startup audit fixtures do not share one live object")
        if not fixture_paths.issubset(provider_artifacts):
            raise ValueError("negative startup fixture is not the real CcInfo object")
    elif negative_fixtures is not None or negative_fixture_provider is not None:
        raise ValueError("positive startup audit requested negative fixture evidence")

    direct_test_startup, _, _ = _elf_startup_inventory(
        pathlib.Path(direct_test_elf["path"]),
        args.llvm_readobj,
    )
    expected_direct_test_startup = _expected_direct_test_startup(manifest["configuration"])
    startup_violation = _direct_test_startup_inventory_violation(
        direct_test_startup,
        expected_direct_test_startup,
    )
    if startup_violation is not None:
        raise ValueError(f"direct-test final-ELF startup inventory drifted: {startup_violation}")
    if manifest["kind"] == "negative":
        observed_diagnostics.extend(
            _exercise_gtest_initializer_classifiers(
                direct_test_startup,
                expected_direct_test_startup,
            )
        )

    diagnostics = "".join(f"diagnostic={diagnostic_id}\n" for diagnostic_id in observed_diagnostics)
    args.stamp.write_bytes(
        (
            "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-STARTUP-PROVENANCE-001\n"
            f"configuration={manifest['configuration']}\n"
            f"kind={manifest['kind']}\n"
            f"{diagnostics}"
            f"manifest_sha256={hashlib.sha256(manifest_bytes).hexdigest()}\n"
        ).encode()
    )
    return 0


def _runfile(rootpath: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"] / rootpath


_STARTUP_OWNER_AUDIT_MODE = len(sys.argv) > 1 and sys.argv[1] == "--startup-owner-audit"
if _STARTUP_OWNER_AUDIT_MODE:
    _CONFIGURATION = ""
    _LLVM_NM = ""
    _LLVM_READOBJ = ""
    _POSITIVE_STAMP = ""
    _NEGATIVE_STAMP = ""
    _DIRECT_TEST = ""
    _RUNNERS: tuple[str, ...] = ()
else:
    if len(sys.argv) != 9:
        raise SystemExit(
            "expected configuration, llvm-nm, llvm-readobj, two semantic stamps, "
            "the direct test, and two process runners"
        )
    _CONFIGURATION = sys.argv[1]
    _LLVM_NM = sys.argv[2]
    _LLVM_READOBJ = sys.argv[3]
    _POSITIVE_STAMP = sys.argv[4]
    _NEGATIVE_STAMP = sys.argv[5]
    _DIRECT_TEST = sys.argv[6]
    _RUNNERS = tuple(sys.argv[7:])

_EXPECTED_DIRECT_TEST_NAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
)
_EXPECTED_RUNNER_NAMES = frozenset(
    (
        "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
        "phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
    )
)
_POSITIVE_STAMP_NAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.ok"
)
_NEGATIVE_STAMP_NAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok"
)
_POSITIVE_INVARIANT = "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-001"
_NEGATIVE_INVARIANT = "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-NEGATIVE-001"

_REQUIRED_RUNNER_SYMBOLS = (
    "BuildPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducerIdentity(",
    "PreflightPhase4ConfirmatoryH4096SessionV5SameRunPerNetReportProducer(",
    "BuildPhase4ConfirmatoryH4096SessionV5PreflightIdentity(",
    "BuildPhase4ConfirmatoryH4096SessionV5CanonicalCell(",
    "PreflightPhase4ConfirmatoryH4096SessionV5Controller(",
    "BuildPhase4CanonicalTrialSpecForCorpusV2H4096SessionV5(",
    "ComputePhase4CanonicalAlgorithmBudgetChecksumV1(",
    "ComputePhase4PairedBudgetChecksumForAuthorityV1(",
    "PreflightPhase4CorpusV2SessionExecutionAuthority(",
)

_FORBIDDEN_RUNNER_SYMBOLS = (
    "BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity(",
    "PreflightPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducer(",
    "AllocateMultiWorld",
    "AllocateOneWorld",
    "ArtifactInstaller",
    "AssemblePhase4",
    "BuildPhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact",
    "BuildPhase4ConfirmatoryH4096SameRunPerNetReportArtifact",
    "BuildPhase4ConfirmatorySameRunDecisionTelemetry",
    "BuildPhase4Imported",
    "BuildPhase4PerNetReportArtifact",
    "BuildPhase4RepresentativeCase",
    "BuildPhase4SameRunDecisionTelemetry",
    "BuildPhase4SameRunRaw",
    "CandidateStore::",
    "ChildLaunch",
    "CpuAStar",
    "CpuCandidateAllocationSession::",
    "CpuCandidatePoolPreparer::",
    "CreatePersistentCpuCandidatePoolPreparer",
    "DecodePhase4",
    "Diagnostic",
    "DurableOutput",
    "EncodePhase4",
    "ExecuteCpuCandidateAllocation",
    "ExecutePhase4",
    "ExecuteSequentialNegotiatedBaseline",
    "ExecuteTargetedRegeneration",
    "FinalizePhase4",
    "InstallPhase4",
    "KiCad",
    "Kicad",
    "LaunchPhase4",
    "OpenPhase4",
    "ParsePhase4",
    "PersistentCpuCandidatePoolPreparer::",
    "PrepareCpuCandidate",
    "PrepareInitialCpuCandidatePools",
    "PreflightPhase4ConfirmatoryH4096SessionV5Worker(",
    "ReadFile",
    "ReadPhase4",
    "ResolveRunfile",
    "RunPhase4",
    "SerializePhase4",
    "ValidatePhase4ConfirmatoryH4096OrdinaryPerNetReportArtifact",
    "ValidatePhase4ConfirmatoryH4096SameRunPerNetReportArtifact",
    "ValidatePhase4ConfirmatorySameRunDecisionTelemetry",
    "ValidatePhase4PerNetReportArtifact",
    "ValidatePhase4SameRunRaw",
    "WorkerLauncher",
    "WriteFile",
    "WritePhase4",
    "kicad_fixture",
    "tool_runfiles",
)

_PINNED_LLVM_GLOBAL_INITIALIZERS = frozenset(
    (
        "_GLOBAL__sub_I_AsmWriter.cpp",
        "_GLOBAL__sub_I_AutoUpgrade.cpp",
        "_GLOBAL__sub_I_BitcodeReader.cpp",
        "_GLOBAL__sub_I_BuiltinGCs.cpp",
        "_GLOBAL__sub_I_Constants.cpp",
        "_GLOBAL__sub_I_ContinuationRecordBuilder.cpp",
        "_GLOBAL__sub_I_DebugInfoMetadata.cpp",
        "_GLOBAL__sub_I_DebugProgramInstruction.cpp",
        "_GLOBAL__sub_I_DiagnosticHandler.cpp",
        "_GLOBAL__sub_I_Dominators.cpp",
        "_GLOBAL__sub_I_Function.cpp",
        "_GLOBAL__sub_I_IRSymtab.cpp",
        "_GLOBAL__sub_I_Instruction.cpp",
        "_GLOBAL__sub_I_Instructions.cpp",
        "_GLOBAL__sub_I_LLParser.cpp",
        "_GLOBAL__sub_I_LegacyPassManager.cpp",
        "_GLOBAL__sub_I_MCAsmInfo.cpp",
        "_GLOBAL__sub_I_MCAsmParser.cpp",
        "_GLOBAL__sub_I_MCSymbol.cpp",
        "_GLOBAL__sub_I_MachOUniversalWriter.cpp",
        "_GLOBAL__sub_I_MetadataLoader.cpp",
        "_GLOBAL__sub_I_ModuleSummaryIndex.cpp",
        "_GLOBAL__sub_I_OffloadBundle.cpp",
        "_GLOBAL__sub_I_OptBisect.cpp",
        "_GLOBAL__sub_I_PassTimingInfo.cpp",
        "_GLOBAL__sub_I_PrintPasses.cpp",
        "_GLOBAL__sub_I_ProfDataUtils.cpp",
        "_GLOBAL__sub_I_RemarkStreamer.cpp",
        "_GLOBAL__sub_I_SafepointIRVerifier.cpp",
        "_GLOBAL__sub_I_TypeHashing.cpp",
        "_GLOBAL__sub_I_TypeStreamMerger.cpp",
        "_GLOBAL__sub_I_Value.cpp",
        "_GLOBAL__sub_I_Verifier.cpp",
    )
)
_GTEST_GLOBAL_INITIALIZERS = frozenset(
    (
        "_GLOBAL__sub_I_gmock.cc",
        "_GLOBAL__sub_I_gtest.cc",
        "_GLOBAL__sub_I_gtest_death_test.cc",
    )
)
_DIRECT_TEST_GLOBAL_INITIALIZER = (
    "_GLOBAL__sub_I_phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.cc"
)
_PINNED_RUNTIME_STARTUP_SYMBOLS = frozenset(
    (
        "__do_fini",
        "__do_init",
        "__fini",
        "__init",
        "__libc_csu_fini",
        "__libc_csu_init",
    )
)
_PINNED_LLVM_INIT_ORDER = (
    "OffloadBundle.cpp",
    "MachOUniversalWriter.cpp",
    "IRSymtab.cpp",
    "LLParser.cpp",
    "MetadataLoader.cpp",
    "BitcodeReader.cpp",
    "Verifier.cpp",
    "Value.cpp",
    "SafepointIRVerifier.cpp",
    "ProfDataUtils.cpp",
    "PrintPasses.cpp",
    "PassTimingInfo.cpp",
    "OptBisect.cpp",
    "ModuleSummaryIndex.cpp",
    "LegacyPassManager.cpp",
    "Instructions.cpp",
    "Instruction.cpp",
    "Function.cpp",
    "Dominators.cpp",
    "DiagnosticHandler.cpp",
    "DebugProgramInstruction.cpp",
    "DebugInfoMetadata.cpp",
    "Constants.cpp",
    "BuiltinGCs.cpp",
    "AutoUpgrade.cpp",
    "AsmWriter.cpp",
    "RemarkStreamer.cpp",
    "MCAsmParser.cpp",
    "MCSymbol.cpp",
    "MCAsmInfo.cpp",
    "TypeStreamMerger.cpp",
    "TypeHashing.cpp",
    "ContinuationRecordBuilder.cpp",
)
_DIRECT_TEST_SOURCE = "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.cc"
_DIRECT_TEST_OBJECT_OWNER = (
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
    "[_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test/"
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.pic.o]"
)
_PINNED_CRT_OWNER = "@llvm//toolchain:crtbeginS.o[crtbegin.c]"
_PINNED_GLIBC_CSU_OWNER = "@llvm//runtimes/glibc:libc-start[csu/libc-start.c]"
_PINNED_LIBCXX_IOSTREAM_OWNER = (
    "@llvm//runtimes/libcxx:libcxx.static_[liblibcxx.static.a(iostream.pic.o)]"
)
_PINNED_SANITIZER_OWNER_PREFIX = "@llvm//runtimes:checksum-pinned-llvm-22.1.8"
_GTEST_FINAL_OWNERS = {
    "gmock.cc": "@googletest//:gtest[libgtest.a(gmock.pic.o)]",
    "gtest.cc": "@googletest//:gtest[libgtest.a(gtest.pic.o)]",
    "gtest-death-test.cc": ("@googletest//:gtest[libgtest.a(gtest-death-test.pic.o)]"),
}


def _final_startup_owner(symbol: str, source: str) -> str:
    if source == _DIRECT_TEST_SOURCE:
        return _DIRECT_TEST_OBJECT_OWNER
    if source in _GTEST_FINAL_OWNERS:
        return _GTEST_FINAL_OWNERS[source]
    if symbol in ("__do_init", "__do_fini") and source == "crtbegin.c":
        return _PINNED_CRT_OWNER
    if symbol == "_GLOBAL__I_000100" and source == "iostream.cpp":
        return _PINNED_LIBCXX_IOSTREAM_OWNER
    if source:
        return f"{_PINNED_SANITIZER_OWNER_PREFIX}[{source}]"
    return "<unowned>"


def _expected_runner_startup() -> dict[str, tuple[tuple[str, str, str], ...]]:
    return {
        ".preinit_array": (),
        ".init_array": (
            (
                "_GLOBAL__I_000100",
                "iostream.cpp",
                _PINNED_LIBCXX_IOSTREAM_OWNER,
            ),
            ("__do_init", "crtbegin.c", _PINNED_CRT_OWNER),
        ),
        ".fini_array": (("__do_fini", "crtbegin.c", _PINNED_CRT_OWNER),),
    }


def _expected_runtime_startup_provenance() -> tuple[tuple[str, str, str, str, str, str], ...]:
    return tuple(
        sorted(
            (
                (
                    "__do_fini",
                    "Local",
                    "Function",
                    ".text",
                    "crtbegin.c",
                    _PINNED_CRT_OWNER,
                ),
                (
                    "__do_init",
                    "Local",
                    "Function",
                    ".text",
                    "crtbegin.c",
                    _PINNED_CRT_OWNER,
                ),
                (
                    "__fini",
                    "Local",
                    "Object",
                    ".fini_array",
                    "crtbegin.c",
                    _PINNED_CRT_OWNER,
                ),
                (
                    "__init",
                    "Local",
                    "Object",
                    ".init_array",
                    "crtbegin.c",
                    _PINNED_CRT_OWNER,
                ),
                (
                    "__libc_csu_fini",
                    "Global",
                    "Function",
                    ".text",
                    "empty.c",
                    _PINNED_GLIBC_CSU_OWNER,
                ),
                (
                    "__libc_csu_init",
                    "Global",
                    "Function",
                    ".text",
                    "empty.c",
                    _PINNED_GLIBC_CSU_OWNER,
                ),
            )
        )
    )


def _expected_direct_test_startup(
    configuration: str,
) -> dict[str, tuple[tuple[str, str, str], ...]]:
    ordinary = (
        (
            "_GLOBAL__I_000100",
            "iostream.cpp",
            _PINNED_LIBCXX_IOSTREAM_OWNER,
        ),
        ("__do_init", "crtbegin.c", _PINNED_CRT_OWNER),
    )
    registrations = (
        (_DIRECT_TEST_GLOBAL_INITIALIZER, _DIRECT_TEST_SOURCE, _DIRECT_TEST_OBJECT_OWNER),
        (
            "_GLOBAL__sub_I_gmock.cc",
            "gmock.cc",
            _GTEST_FINAL_OWNERS["gmock.cc"],
        ),
        (
            "_GLOBAL__sub_I_gtest.cc",
            "gtest.cc",
            _GTEST_FINAL_OWNERS["gtest.cc"],
        ),
        (
            "_GLOBAL__sub_I_gtest_death_test.cc",
            "gtest-death-test.cc",
            _GTEST_FINAL_OWNERS["gtest-death-test.cc"],
        ),
    )
    if configuration == "normal":
        return {
            ".preinit_array": (),
            ".init_array": ordinary + registrations,
            ".fini_array": (("__do_fini", "crtbegin.c", _PINNED_CRT_OWNER),),
        }

    sanitizer_initializers = (
        (
            "_GLOBAL__sub_I_sanitizer_common_libcdep.cpp",
            "sanitizer_common_libcdep.cpp",
            f"{_PINNED_SANITIZER_OWNER_PREFIX}[sanitizer_common_libcdep.cpp]",
        ),
    ) + tuple(
        (
            f"_GLOBAL__sub_I_{source}",
            source,
            f"{_PINNED_SANITIZER_OWNER_PREFIX}[{source}]",
        )
        for source in _PINNED_LLVM_INIT_ORDER
    )
    if configuration == "ubsan":
        ubsan_initializer = (
            (
                "_GLOBAL__sub_I_ubsan_init_standalone.cpp",
                "ubsan_init_standalone.cpp",
                f"{_PINNED_SANITIZER_OWNER_PREFIX}[ubsan_init_standalone.cpp]",
            ),
        )
        return {
            ".preinit_array": (
                (
                    "__ubsan::PreInitAsStandalone()",
                    "ubsan_init_standalone_preinit.cpp",
                    f"{_PINNED_SANITIZER_OWNER_PREFIX}[ubsan_init_standalone_preinit.cpp]",
                ),
            ),
            ".init_array": (ordinary + ubsan_initializer + sanitizer_initializers + registrations),
            ".fini_array": (("__do_fini", "crtbegin.c", _PINNED_CRT_OWNER),),
        }

    return {
        ".preinit_array": (
            (
                "__asan_init",
                "empty.c",
                f"{_PINNED_SANITIZER_OWNER_PREFIX}[empty.c]",
            ),
        ),
        ".init_array": (
            (
                "asan.module_ctor",
                _DIRECT_TEST_SOURCE,
                _DIRECT_TEST_OBJECT_OWNER,
            ),
        )
        + ordinary
        + sanitizer_initializers
        + registrations
        + (
            (
                "UnpoisonBeforeMain()",
                "asan_globals.cpp",
                f"{_PINNED_SANITIZER_OWNER_PREFIX}[asan_globals.cpp]",
            ),
        ),
        ".fini_array": (
            (
                "asan.module_dtor",
                _DIRECT_TEST_SOURCE,
                _DIRECT_TEST_OBJECT_OWNER,
            ),
            ("__do_fini", "crtbegin.c", _PINNED_CRT_OWNER),
        ),
    }


def _direct_test_startup_inventory_violation(
    actual: dict[str, tuple[tuple[str, str, str], ...]],
    expected: dict[str, tuple[tuple[str, str, str], ...]],
) -> str | None:
    if actual == expected:
        return None
    if set(actual) != set(expected) or any(
        actual[name] != expected[name] for name in (".preinit_array", ".fini_array")
    ):
        return "startup-inventory-drift"

    actual_initializers = Counter(actual[".init_array"])
    expected_initializers = Counter(expected[".init_array"])
    missing = list((expected_initializers - actual_initializers).elements())
    additional = list((actual_initializers - expected_initializers).elements())
    gtest_initializers = {
        initializer
        for initializer in expected[".init_array"]
        if initializer[1] in _GTEST_FINAL_OWNERS
    }
    if len(missing) == 1 and missing[0] in gtest_initializers and not additional:
        return _DIAGNOSTIC_GTEST_INITIALIZER_MISSING
    if not missing and len(additional) == 1 and additional[0] in gtest_initializers:
        return _DIAGNOSTIC_GTEST_INITIALIZER_DUPLICATED
    if len(missing) == 1 and missing[0] in gtest_initializers and len(additional) == 1:
        return _DIAGNOSTIC_GTEST_INITIALIZER_SUBSTITUTED
    if not missing and len(additional) == 1:
        return _DIAGNOSTIC_GTEST_INITIALIZER_ADDITIONAL
    return "startup-inventory-drift"


def _exercise_gtest_initializer_classifiers(
    inventory: dict[str, tuple[tuple[str, str, str], ...]],
    expected: dict[str, tuple[tuple[str, str, str], ...]],
) -> tuple[str, ...]:
    if _direct_test_startup_inventory_violation(inventory, expected) is not None:
        raise ValueError("direct-test final-ELF startup baseline drifted")
    initializers = list(inventory[".init_array"])
    index = next(
        (
            index
            for index, initializer in enumerate(initializers)
            if initializer[1] in _GTEST_FINAL_OWNERS
        ),
        None,
    )
    if index is None:
        raise ValueError("direct-test final ELF has no GoogleTest initializer to mutate")
    gtest_initializer = initializers[index]
    foreign_initializer = (
        "_GLOBAL__sub_I_foreign_gtest.cc",
        "foreign_gtest.cc",
        "@@phase4_negative_probe//:foreign_gtest_initializer",
    )
    mutations = (
        (
            _DIAGNOSTIC_GTEST_INITIALIZER_MISSING,
            initializers[:index] + initializers[index + 1 :],
        ),
        (
            _DIAGNOSTIC_GTEST_INITIALIZER_DUPLICATED,
            initializers[: index + 1] + [gtest_initializer] + initializers[index + 1 :],
        ),
        (
            _DIAGNOSTIC_GTEST_INITIALIZER_SUBSTITUTED,
            initializers[:index] + [foreign_initializer] + initializers[index + 1 :],
        ),
        (
            _DIAGNOSTIC_GTEST_INITIALIZER_ADDITIONAL,
            initializers + [foreign_initializer],
        ),
    )
    observed = []
    for diagnostic_id, mutated_initializers in mutations:
        mutant = dict(inventory)
        mutant[".init_array"] = tuple(mutated_initializers)
        if _direct_test_startup_inventory_violation(mutant, expected) != diagnostic_id:
            raise ValueError(f"[{diagnostic_id}] final-ELF startup classifier did not fire")
        observed.append(diagnostic_id)
    return tuple(observed)


def _expected_sanitizer_hooks(
    configuration: str,
) -> tuple[tuple[str, str, str, str], ...]:
    if configuration == "normal":
        return ()
    runtime_owner = f"{_PINNED_SANITIZER_OWNER_PREFIX}[empty.c]"
    common = (
        ("__sanitizer_report_error_summary", "Weak", "empty.c", runtime_owner),
        ("__sanitizer_set_report_fd", "Global", "empty.c", runtime_owner),
        ("__sanitizer_set_report_path", "Global", "empty.c", runtime_owner),
        ("__ubsan_default_options", "Weak", "empty.c", runtime_owner),
        ("__ubsan_get_current_report_data", "Global", "empty.c", runtime_owner),
        ("__ubsan_on_report", "Weak", "empty.c", runtime_owner),
    )
    if configuration == "ubsan":
        return tuple(sorted(common))
    return tuple(
        sorted(
            (
                ("__asan_default_options", "Weak", "empty.c", runtime_owner),
                ("__asan_on_error", "Weak", "empty.c", runtime_owner),
                (
                    "__asan_set_error_report_callback",
                    "Global",
                    "empty.c",
                    runtime_owner,
                ),
            )
            + common
        )
    )


def _symbol_names(binary: pathlib.Path) -> list[str]:
    output = subprocess.run(
        [
            str(_runfile(_LLVM_NM)),
            "--defined-only",
            "--demangle",
            str(binary),
        ],
        check=True,
        text=True,
        capture_output=True,
    ).stdout
    names: list[str] = []
    for line in output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) == 3:
            names.append(fields[2])
    return names


def _elf_symbol_records(data: dict[str, Any]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    source = ""
    for wrapped in data["Symbols"]:
        symbol = wrapped["Symbol"]
        if symbol["Type"]["Name"] == "File":
            source = symbol["Name"]["Name"]
            continue
        records.append(
            {
                "binding": symbol["Binding"]["Name"],
                "name": symbol["Name"]["Name"],
                "section": symbol["Section"]["Name"],
                "source": source,
                "type": symbol["Type"]["Name"],
                "value": symbol["Value"],
            }
        )
    return records


def _elf_startup_inventory(
    binary: pathlib.Path,
    llvm_readobj: pathlib.Path | None = None,
) -> tuple[
    dict[str, tuple[tuple[str, str, str], ...]],
    tuple[tuple[str, str, str, str], ...],
    tuple[tuple[str, str, str, str, str, str], ...],
]:
    data = _llvm_readobj_json(
        _runfile(_LLVM_READOBJ) if llvm_readobj is None else llvm_readobj,
        binary,
        "--demangle",
        "--dynamic-table",
        "--relocations",
        "--section-data",
        "--sections",
        "--symbols",
    )
    if len(data) != 1:
        raise AssertionError(f"expected one ELF image from {binary}: {len(data)}")
    elf = data[0]
    if elf["FileSummary"]["Format"] != "elf64-x86-64":
        raise AssertionError(f"unexpected ELF format for {binary}")

    sections: dict[str, dict[str, Any]] = {}
    for wrapped in elf["Sections"]:
        section = wrapped["Section"]
        section_type = section["Type"]["Name"]
        name = section["Name"]["Name"]
        if section_type in _STARTUP_SECTION_TYPES or name in (
            ".init",
            ".fini",
            ".ctors",
            ".dtors",
        ):
            if name not in (".preinit_array", ".init_array", ".fini_array"):
                raise AssertionError(f"forbidden startup section in {binary}: {name}")
            if name in sections:
                raise AssertionError(f"duplicate startup section in {binary}: {name}")
            sections[name] = section

    relocations: dict[int, list[dict[str, Any]]] = {}
    for relocation_section in elf["Relocations"]:
        for wrapped in relocation_section["Relocs"]:
            relocation = wrapped["Relocation"]
            relocations.setdefault(relocation["Offset"], []).append(relocation)

    symbols_by_address: dict[int, list[dict[str, Any]]] = {}
    symbol_records = _elf_symbol_records(elf)
    for symbol in symbol_records:
        if symbol["name"] and symbol["type"] == "Function":
            symbols_by_address.setdefault(symbol["value"], []).append(symbol)

    inventory: dict[str, tuple[tuple[str, str, str], ...]] = {}
    section_dynamic_tags = {
        ".preinit_array": ("PREINIT_ARRAY", "PREINIT_ARRAYSZ"),
        ".init_array": ("INIT_ARRAY", "INIT_ARRAYSZ"),
        ".fini_array": ("FINI_ARRAY", "FINI_ARRAYSZ"),
    }
    expected_dynamic: dict[str, int] = {}
    for name, (address_tag, size_tag) in section_dynamic_tags.items():
        section = sections.get(name)
        if section is None:
            inventory[name] = ()
            continue
        expected_type = {
            ".preinit_array": "SHT_PREINIT_ARRAY",
            ".init_array": "SHT_INIT_ARRAY",
            ".fini_array": "SHT_FINI_ARRAY",
        }[name]
        if section["Type"]["Name"] != expected_type:
            raise AssertionError(f"wrong type for {name} in {binary}")
        if section["AddressAlignment"] != 8 or section["Size"] % 8 != 0:
            raise AssertionError(f"non-pointer-aligned {name} in {binary}")
        if section["Flags"]["Value"] != 3:
            raise AssertionError(f"wrong flags for {name} in {binary}")
        raw = bytes(section["SectionData"]["Bytes"])
        if len(raw) != section["Size"] or any(raw):
            raise AssertionError(f"non-relocated startup bytes in {name} of {binary}")
        expected_dynamic[address_tag] = section["Address"]
        expected_dynamic[size_tag] = section["Size"]

        entries: list[tuple[str, str, str]] = []
        for offset in range(0, section["Size"], 8):
            slot = section["Address"] + offset
            slot_relocations = relocations.get(slot, [])
            if len(slot_relocations) != 1:
                raise AssertionError(
                    f"startup slot {slot:#x} in {binary} has {len(slot_relocations)} relocations"
                )
            relocation = slot_relocations[0]
            if (
                relocation["Type"]["Name"] != "R_X86_64_RELATIVE"
                or relocation["Symbol"]["Name"] != "-"
                or relocation["Symbol"]["Value"] != 0
            ):
                raise AssertionError(f"non-relative startup relocation at {slot:#x} in {binary}")
            target = relocation["Addend"]
            candidates = symbols_by_address.get(target, [])
            if len(candidates) != 1:
                raise AssertionError(
                    f"startup target {target:#x} in {binary} has ambiguous owner: {candidates}"
                )
            symbol = candidates[0]
            owner = _final_startup_owner(symbol["name"], symbol["source"])
            if owner == "<unowned>":
                raise AssertionError(f"startup target {symbol['name']} in {binary} has no owner")
            entries.append((symbol["name"], symbol["source"], owner))
        inventory[name] = tuple(entries)

    dynamic_startup = [
        entry
        for entry in elf["DynamicSection"]
        if entry["Type"]
        in {
            "PREINIT_ARRAY",
            "PREINIT_ARRAYSZ",
            "INIT",
            "INIT_ARRAY",
            "INIT_ARRAYSZ",
            "FINI",
            "FINI_ARRAY",
            "FINI_ARRAYSZ",
        }
    ]
    actual_dynamic = {entry["Type"]: entry["Value"] for entry in dynamic_startup}
    if len(actual_dynamic) != len(dynamic_startup) or actual_dynamic != expected_dynamic:
        raise AssertionError(
            f"ELF init/fini hook inventory differs in {binary}: "
            f"actual={actual_dynamic} expected={expected_dynamic}"
        )

    hook_inventory = tuple(
        sorted(
            (
                symbol["name"],
                symbol["binding"],
                symbol["source"],
                _final_startup_owner(symbol["name"], symbol["source"]),
            )
            for symbol in symbol_records
            if symbol["section"] != "Undefined"
            and any(fragment in symbol["name"] for fragment in _SANITIZER_HOOK_FRAGMENTS)
        )
    )
    if any(record[3] == "<unowned>" for record in hook_inventory):
        raise AssertionError(f"sanitizer hook has no exact owner in {binary}")

    runtime_names = {
        "__do_fini",
        "__do_init",
        "__fini",
        "__init",
        "__libc_csu_fini",
        "__libc_csu_init",
    }
    runtime_provenance = tuple(
        sorted(
            (
                symbol["name"],
                symbol["binding"],
                symbol["type"],
                symbol["section"],
                symbol["source"],
                (
                    _PINNED_GLIBC_CSU_OWNER
                    if symbol["name"].startswith("__libc_csu_")
                    else _PINNED_CRT_OWNER
                ),
            )
            for symbol in symbol_records
            if symbol["name"] in runtime_names
        )
    )
    if runtime_provenance != _expected_runtime_startup_provenance():
        raise AssertionError(
            f"pinned runtime startup provenance differs in {binary}: {runtime_provenance}"
        )

    slot_addresses: dict[str, int] = {}
    for section_name in (".init_array", ".fini_array"):
        section = sections[section_name]
        for offset in range(0, section["Size"], 8):
            relocation = relocations[section["Address"] + offset][0]
            target_symbols = symbols_by_address[relocation["Addend"]]
            slot_addresses[target_symbols[0]["name"]] = section["Address"] + offset
    runtime_by_name = {
        symbol["name"]: symbol for symbol in symbol_records if symbol["name"] in runtime_names
    }
    if runtime_by_name["__init"]["value"] != slot_addresses["__do_init"]:
        raise AssertionError(f"__init does not own the __do_init slot in {binary}")
    if runtime_by_name["__fini"]["value"] != slot_addresses["__do_fini"]:
        raise AssertionError(f"__fini does not own the __do_fini slot in {binary}")
    return inventory, hook_inventory, runtime_provenance


def _matches(symbols: list[str], needle: str) -> list[str]:
    return [symbol for symbol in symbols if needle in symbol]


def _expected_direct_test_initializers() -> frozenset[str]:
    expected = _GTEST_GLOBAL_INITIALIZERS | {_DIRECT_TEST_GLOBAL_INITIALIZER}
    if _CONFIGURATION in ("asan", "ubsan"):
        expected |= _PINNED_LLVM_GLOBAL_INITIALIZERS
        expected |= {"_GLOBAL__sub_I_sanitizer_common_libcdep.cpp"}
    if _CONFIGURATION == "ubsan":
        expected |= {"_GLOBAL__sub_I_ubsan_init_standalone.cpp"}
    return frozenset(expected)


def _assert_exact_pinned_runtime_startup(test: unittest.TestCase, symbols: list[str]) -> None:
    for symbol in _PINNED_RUNTIME_STARTUP_SYMBOLS:
        test.assertEqual(symbols.count(symbol), 1, f"wrong multiplicity for {symbol}")


class Phase4H4096SessionV5SameRunProducerPreflightLinkSurfaceTest(unittest.TestCase):
    def test_semantic_stamps_are_consumed_with_exact_current_configuration(self) -> None:
        self.assertIn(_CONFIGURATION, ("normal", "asan", "ubsan"))
        expected = (
            (
                _POSITIVE_STAMP,
                _POSITIVE_STAMP_NAME,
                _POSITIVE_INVARIANT,
            ),
            (
                _NEGATIVE_STAMP,
                _NEGATIVE_STAMP_NAME,
                _NEGATIVE_INVARIANT,
            ),
        )
        for rootpath, basename, invariant in expected:
            stamp = _runfile(rootpath)
            with self.subTest(stamp=basename):
                self.assertEqual(stamp.name, basename)
                self.assertEqual(
                    stamp.read_bytes(),
                    f"{invariant}\nconfiguration={_CONFIGURATION}\n".encode(),
                )

    def test_both_process_runners_retain_exactly_the_preflight_capability(self) -> None:
        self.assertEqual(len(_RUNNERS), 2)
        self.assertEqual(
            {pathlib.PurePosixPath(rootpath).name for rootpath in _RUNNERS},
            _EXPECTED_RUNNER_NAMES,
        )
        for rootpath in _RUNNERS:
            binary = _runfile(rootpath)
            symbols = _symbol_names(binary)
            with self.subTest(binary=binary.name):
                self.assertEqual(symbols.count("main"), 1)
                for required in _REQUIRED_RUNNER_SYMBOLS:
                    matches = _matches(symbols, required)
                    self.assertEqual(
                        len(matches),
                        1,
                        f"{required!r} has wrong multiplicity in {binary.name}:\n"
                        + "\n".join(matches[:20]),
                    )
                for forbidden in _FORBIDDEN_RUNNER_SYMBOLS:
                    matches = _matches(symbols, forbidden)
                    self.assertFalse(
                        matches,
                        f"{forbidden!r} unexpectedly found in {binary.name}:\n"
                        + "\n".join(matches[:20]),
                    )

    def test_runner_startup_and_teardown_are_exact_reset_runtime_only(self) -> None:
        for rootpath in _RUNNERS:
            binary = _runfile(rootpath)
            symbols = _symbol_names(binary)
            startup, sanitizer_hooks, runtime_provenance = _elf_startup_inventory(binary)
            with self.subTest(binary=binary.name):
                self.assertEqual(startup, _expected_runner_startup())
                self.assertEqual(sanitizer_hooks, ())
                self.assertEqual(
                    runtime_provenance,
                    _expected_runtime_startup_provenance(),
                )
                self.assertEqual(
                    [symbol for symbol in symbols if symbol.startswith("_GLOBAL__sub_I_")],
                    [],
                )
                self.assertEqual(_matches(symbols, "asan.module_ctor"), [])
                self.assertEqual(_matches(symbols, "asan.module_dtor"), [])
                self.assertEqual(_matches(symbols, "ubsan.module_"), [])
                _assert_exact_pinned_runtime_startup(self, symbols)

    def test_direct_test_startup_has_exact_pinned_provenance(self) -> None:
        direct_test = _runfile(_DIRECT_TEST)
        self.assertEqual(direct_test.name, _EXPECTED_DIRECT_TEST_NAME)
        symbols = _symbol_names(direct_test)
        startup, sanitizer_hooks, runtime_provenance = _elf_startup_inventory(direct_test)
        self.assertIsNone(
            _direct_test_startup_inventory_violation(
                startup,
                _expected_direct_test_startup(_CONFIGURATION),
            )
        )
        self.assertEqual(sanitizer_hooks, _expected_sanitizer_hooks(_CONFIGURATION))
        self.assertEqual(runtime_provenance, _expected_runtime_startup_provenance())
        initializers = [symbol for symbol in symbols if symbol.startswith("_GLOBAL__sub_I_")]
        self.assertEqual(len(initializers), len(set(initializers)))
        self.assertEqual(frozenset(initializers), _expected_direct_test_initializers())
        _assert_exact_pinned_runtime_startup(self, symbols)
        if _CONFIGURATION == "asan":
            self.assertEqual(_matches(symbols, "asan.module_ctor"), ["asan.module_ctor"])
            self.assertEqual(_matches(symbols, "asan.module_dtor"), ["asan.module_dtor"])
        else:
            self.assertEqual(_matches(symbols, "asan.module_ctor"), [])
            self.assertEqual(_matches(symbols, "asan.module_dtor"), [])
        self.assertEqual(_matches(symbols, "ubsan.module_"), [])

    def test_forbidden_matcher_covers_each_closed_capability_class(self) -> None:
        representatives = (
            "BuildPhase4ConfirmatoryH4096SessionV5OrdinaryPerNetReportProducerIdentity(",
            "BuildPhase4ConfirmatoryH4096SameRunPerNetReportArtifact(",
            "BuildPhase4SameRunDecisionTelemetryV3(",
            "ValidatePhase4SameRunRawEvidenceArtifactV3(",
            "ExecutePhase4ConfirmatoryH4096SameRunTrialArmDiagnostic(",
            "BuildPhase4RepresentativeCaseV2(",
            "PrepareInitialCpuCandidatePools(",
            "PreflightPhase4ConfirmatoryH4096SessionV5Worker(",
            "AllocateOneWorld(",
            "SerializePhase4PerNetReportArtifactJsonV1(",
            "WritePhase4TrialWireMessageV2ForCorpusV2(",
            "ResolveRunfile(",
            "ChildLaunchFailure",
        )
        for symbol in representatives:
            with self.subTest(symbol=symbol):
                self.assertTrue(
                    any(forbidden in symbol for forbidden in _FORBIDDEN_RUNNER_SYMBOLS),
                    f"{symbol!r} is not covered by the capability blacklist",
                )


if __name__ == "__main__":
    if _STARTUP_OWNER_AUDIT_MODE:
        raise SystemExit(_run_startup_owner_audit(sys.argv[2:]))
    unittest.main(argv=[sys.argv[0]])
