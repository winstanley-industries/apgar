"""Audits the ADR-067 same-run report-producer preflight boundary."""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "find_cc_toolchain",
    "use_cc_toolchain",
)
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load(
    "//bazel:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.bzl",
    "PHASE4_SAME_RUN_SEMANTIC_AUDIT_ATTRS",
    "Phase4SameRunSemanticAuditStampInfo",
    "phase4_same_run_run_semantic_audit",
    "phase4_same_run_semantic_target_aspect",
)

_PRODUCTION = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight",
)
_TEST_SUPPORT = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support",
)
_DIRECT_TEST = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
)
_DIRECT_TEST_PATH = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test"
)
_RUNNER = Label(
    "//:phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
)
_RUNNER_PATH = (
    "phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner"
)
_FORCED_RUNNER = Label(
    "//:phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
)
_FORCED_RUNNER_PATH = (
    "phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner"
)
_PRODUCER_SOURCE = Label(
    "//:src/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight.cc",
)
_PRODUCER_HEADER = Label(
    "//:src/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_internal.h",
)
_DIRECT_TEST_SOURCE = Label(
    "//:tests/benchmark/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test.cc",
)
_GOOGLE_TEST_SUPPORT_HEADER = Label("//:tests/support/google_test.h")
_RUNNER_SOURCE = Label(
    "//:tools/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner.cc",
)
_EXECUTION_PRODUCTION = Label("//:phase4_h4096_session_v5_execution_preflight")
_EXECUTION_TEST_SUPPORT = Label(
    "//:phase4_h4096_session_v5_execution_preflight_test_support",
)
_GTEST_MAIN = Label("@googletest//:gtest_main")
_GTEST = Label("@googletest//:gtest")
_RULES_CC_LINK_EXTRA_LIB = Label("@rules_cc//:link_extra_lib")
_RULES_CC_EMPTY_LIB = Label("@rules_cc//:empty_lib")
_BAZEL_MALLOC = Label("@bazel_tools//tools/cpp:malloc")
_BAZEL_LINK_EXTRA_LIB = Label("@bazel_tools//tools/cpp:link_extra_lib")
_LLVM_READOBJ = Label("@llvm//tools:llvm-readobj")

# buildifier: disable=canonical-repository
_LLVM_READOBJ_RESOLVED_OWNER = (
    "@@llvm++llvm_toolchain_minimal+llvm-toolchain-minimal-22.1.8-linux-amd64//:bin/llvm-readobj"
)
_STARTUP_PROVENANCE_CHECKER = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_startup_provenance_checker",
)

_POSITIVE_AUDIT = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract",
)
_NEGATIVE_AUDIT = Label(
    "//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract",
)
_POSITIVE_BASENAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit.ok"
)
_NEGATIVE_BASENAME = (
    "phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative.ok"
)
_POSITIVE_INVARIANT = (
    "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-001"
)
_NEGATIVE_INVARIANT = (
    "P4PAIR-H4096-SESSION-V5-SAME-RUN-REPORT-PRODUCER-SEMANTIC-AUDIT-NEGATIVE-001"
)
_DIAGNOSTIC_ALLOWLISTED_OWNER_INITIALIZER = "allowlisted-owner-initializer"
_DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER = "foreign-alwayslink-initializer"
_DIAGNOSTIC_STAMP_OWNER_WRONG = "stamp-owner-wrong"
_DIAGNOSTIC_AUDIT_OUTPUT_UNREQUESTED = "audit-output-unrequested"
_DIAGNOSTIC_AUDIT_OUTPUT_MISSING = "audit-output-missing"

# buildifier: disable=canonical-repository
_ALLOWLISTED_INITIALIZER_OWNER = (
    "@@//:phase4_h4096_session_v5_execution_preflight_test_support"
)

_PRODUCTION_APGAR_LABELS = (
    _PRODUCTION,
    _EXECUTION_PRODUCTION,
    Label("//:board_ir"),
    Label("//:candidate_contract"),
    Label("//:candidate_policy"),
    Label("//:candidate_store"),
    Label("//:cpu_astar"),
    Label("//:cpu_candidate_allocation_session"),
    Label("//:cpu_candidate_pool_preparation"),
    Label("//:exact_geometry"),
    Label("//:geometry_compiler"),
    Label("//:kicad_fixture_adapter"),
    Label("//:multi_net_workload"),
    Label("//:multi_world_allocator"),
    Label("//:negotiated_prices"),
    Label("//:one_world_allocator"),
    Label("//:phase3_commit"),
    Label("//:phase4_h4096_canonical_budget_preimage"),
    Label("//:phase4_h4096_session_v5_canonical_budget_preimage"),
    Label("//:phase4_imported_multi_net_corpus"),
    Label("//:phase4_paired_trial_preimage"),
    Label("//:phase4_representative_corpus_preimage"),
    Label("//:phase4_trial_harness_preimage"),
    Label("//:planar_route"),
    Label("//:sequential_negotiated_baseline"),
    Label("//:targeted_regeneration"),
    Label("//:targeted_regeneration_execution"),
    Label("//:utf8"),
)

_TEST_SUPPORT_APGAR_LABELS = (
    _TEST_SUPPORT,
    _EXECUTION_TEST_SUPPORT,
    Label("//:board_ir"),
    Label("//:candidate_contract"),
    Label("//:candidate_policy"),
    Label("//:candidate_store"),
    Label("//:cpu_astar"),
    Label("//:cpu_candidate_allocation_session"),
    Label("//:cpu_candidate_pool_preparation"),
    Label("//:exact_geometry"),
    Label("//:geometry_compiler"),
    Label("//:kicad_fixture_adapter"),
    Label("//:multi_net_workload"),
    Label("//:multi_world_allocator"),
    Label("//:negotiated_prices"),
    Label("//:one_world_allocator"),
    Label("//:phase3_commit"),
    Label("//:phase4_imported_multi_net_corpus"),
    Label("//:phase4_paired_trial"),
    Label("//:phase4_representative_corpus"),
    Label("//:phase4_trial_harness"),
    Label("//:planar_route"),
    Label("//:sequential_negotiated_baseline"),
    Label("//:targeted_regeneration"),
    Label("//:targeted_regeneration_execution"),
    Label("//:utf8"),
)

_TRACKED_LABEL_ATTRIBUTES = (
    "srcs",
    "hdrs",
    "textual_hdrs",
    "deps",
    "implementation_deps",
    "dynamic_deps",
    "data",
    "malloc",
    "link_extra_lib",
    "additional_compiler_inputs",
    "additional_linker_inputs",
    "linkstamp",
    "target_compatible_with",
)

_TRACKED_VALUE_ATTRIBUTES = (
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

_ApgarLinkClosureInfo = provider(
    "Frozen C++ dependency and linker-input inventory for ADR-067.",
    fields = {
        "apgar_labels": "transitive APGAR C++ labels",
        "alwayslink": "resolved target alwayslink value",
        "configuration_conlyopts": "resolved global C-only compiler options",
        "configuration_copts": "resolved global C compiler options",
        "configuration_cxxopts": "resolved global C++ compiler options",
        "configuration_disabled_features": "resolved disabled build features",
        "configuration_features": "resolved requested build features",
        "configuration_linkopts": "resolved global linker options",
        "direct_attribute_labels": "resolved direct labels by tracked attribute",
        "direct_attribute_values": "resolved scalar/list values by tracked attribute",
        "features": "resolved target-local feature requests",
        "linker_artifacts": "linker-input artifact Files paired with their exact owners",
        "linker_inputs": "encoded transitive CcInfo LinkerInput records",
        "linkopts": "resolved target link options",
        "linkstatic": "resolved target linkstatic value",
    },
)

_ApgarNegativeInitializerInfo = provider(
    "Live constructor object used by the ADR-067 negative startup audit.",
    fields = {
        "object": "the compiled constructor object represented by the synthetic CcInfo",
    },
)

def _targets(value):
    if value == None:
        return []
    if type(value) == "list":
        return value
    return [value]

def _attribute_targets(ctx, name):
    if not hasattr(ctx.rule.attr, name):
        return []
    return _targets(getattr(ctx.rule.attr, name))

def _attribute_labels(ctx, name):
    return [
        value.label
        for value in _attribute_targets(ctx, name)
        if hasattr(value, "label")
    ]

def _attribute_value(ctx, name):
    if not hasattr(ctx.rule.attr, name):
        return None
    value = getattr(ctx.rule.attr, name)
    if type(value) == "list":
        return tuple(value)
    return value

def _dependency_targets(ctx):
    values = []
    for name in (
        "deps",
        "implementation_deps",
        "dynamic_deps",
        "malloc",
    ):
        values.extend(_attribute_targets(ctx, name))
    return values

def _is_apgar_label(label):
    return label.workspace_name == ""

def _files(value):
    if type(value) == "depset":
        return value.to_list()
    return value

def _optional_file_path(value):
    if value == None:
        return ""
    return value.short_path

def _library_record(library):
    return ",".join([
        "alwayslink=" + ("1" if library.alwayslink else "0"),
        "static=" + _optional_file_path(library.static_library),
        "pic_static=" + _optional_file_path(library.pic_static_library),
        "dynamic=" + _optional_file_path(library.dynamic_library),
        "interface=" + _optional_file_path(library.interface_library),
        "resolved_dynamic=" + _optional_file_path(
            library.resolved_symlink_dynamic_library,
        ),
        "resolved_interface=" + _optional_file_path(
            library.resolved_symlink_interface_library,
        ),
        "objects=" + ":".join(sorted([file.short_path for file in library.objects])),
        "pic_objects=" + ":".join(
            sorted([file.short_path for file in library.pic_objects]),
        ),
    ])

def _linkstamp_path(linkstamp):
    if hasattr(linkstamp, "file"):
        return linkstamp.file().short_path
    return str(linkstamp)

def _linker_input_record(linker_input):
    return ";".join([
        "owner=" + str(linker_input.owner),
        "libraries=" + "|".join(
            sorted([_library_record(library) for library in linker_input.libraries]),
        ),
        "flags=" + ":".join(linker_input.user_link_flags),
        "additional=" + ":".join(
            sorted([file.short_path for file in _files(linker_input.additional_inputs)]),
        ),
        "linkstamps=" + ":".join(
            sorted([_linkstamp_path(value) for value in linker_input.linkstamps]),
        ),
    ])

def _library_files(library):
    result = []
    for name in (
        "static_library",
        "pic_static_library",
        "dynamic_library",
        "interface_library",
        "resolved_symlink_dynamic_library",
        "resolved_symlink_interface_library",
    ):
        value = getattr(library, name)
        if value != None:
            result.append(value)
    result.extend(_files(library.objects))
    result.extend(_files(library.pic_objects))
    return result

def _linker_input_artifact_records(linker_input):
    files = []
    for library in linker_input.libraries:
        files.extend(_library_files(library))
    files.extend(_files(linker_input.additional_inputs))
    for linkstamp in linker_input.linkstamps:
        if hasattr(linkstamp, "file"):
            files.append(linkstamp.file())
    return [
        struct(file = file, owner = str(linker_input.owner))
        for file in files
    ]

def _deduplicated_artifact_records(records):
    by_key = {}
    for record in records:
        key = record.owner + "\t" + record.file.path
        by_key[key] = record
    return tuple([by_key[key] for key in sorted(by_key)])

def _apgar_link_closure_aspect_impl(target, ctx):
    dependencies = _dependency_targets(ctx)
    transitive_labels = []
    transitive_linker_artifacts = []
    transitive_linker_inputs = []
    for dependency in dependencies:
        if _ApgarLinkClosureInfo in dependency:
            transitive_labels.append(dependency[_ApgarLinkClosureInfo].apgar_labels)
            transitive_linker_artifacts.extend(
                dependency[_ApgarLinkClosureInfo].linker_artifacts,
            )
            transitive_linker_inputs.append(
                dependency[_ApgarLinkClosureInfo].linker_inputs,
            )

    own_labels = []
    own_linker_artifacts = []
    own_linker_inputs = []
    if CcInfo in target:
        if _is_apgar_label(target.label):
            own_labels.append(target.label)
        for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
            own_linker_inputs.append(_linker_input_record(linker_input))
            own_linker_artifacts.extend(
                _linker_input_artifact_records(linker_input),
            )

    direct_attribute_labels = {}
    for name in _TRACKED_LABEL_ATTRIBUTES:
        direct_attribute_labels[name] = tuple(_attribute_labels(ctx, name))
    direct_attribute_values = {}
    for name in _TRACKED_VALUE_ATTRIBUTES:
        direct_attribute_values[name] = _attribute_value(ctx, name)

    return [
        _ApgarLinkClosureInfo(
            apgar_labels = depset(
                direct = own_labels,
                transitive = transitive_labels,
            ),
            direct_attribute_labels = direct_attribute_labels,
            direct_attribute_values = direct_attribute_values,
            linker_inputs = depset(
                direct = own_linker_inputs,
                transitive = transitive_linker_inputs,
            ),
            alwayslink = (
                ctx.rule.attr.alwayslink if hasattr(ctx.rule.attr, "alwayslink") else False
            ),
            configuration_conlyopts = tuple(ctx.fragments.cpp.conlyopts),
            configuration_copts = tuple(ctx.fragments.cpp.copts),
            configuration_cxxopts = tuple(ctx.fragments.cpp.cxxopts),
            configuration_disabled_features = tuple(ctx.disabled_features),
            configuration_features = tuple(ctx.features),
            configuration_linkopts = tuple(ctx.fragments.cpp.linkopts),
            features = tuple(
                ctx.rule.attr.features if hasattr(ctx.rule.attr, "features") else [],
            ),
            linker_artifacts = _deduplicated_artifact_records(
                own_linker_artifacts + transitive_linker_artifacts,
            ),
            linkopts = tuple(
                ctx.rule.attr.linkopts if hasattr(ctx.rule.attr, "linkopts") else [],
            ),
            linkstatic = (
                ctx.rule.attr.linkstatic if hasattr(ctx.rule.attr, "linkstatic") else False
            ),
        ),
    ]

_apgar_link_closure_aspect = aspect(
    implementation = _apgar_link_closure_aspect_impl,
    attr_aspects = [
        "deps",
        "implementation_deps",
        "dynamic_deps",
        "malloc",
        "link_extra_lib",
    ],
    fragments = ["cpp"],
)

_ASAN_SETTING = "@llvm//config:asan"
_UBSAN_SETTING = "@llvm//config:ubsan"
_ACTION_ENV_OPTION = "//command_line_option:action_env"
_HOST_ACTION_ENV_OPTION = "//command_line_option:host_action_env"
_PER_FILE_COPT_OPTION = "//command_line_option:per_file_copt"
_STRIP_BASELINE = "sometimes"

_CPP_CONFIGURATION_BASELINE_VALUES = {
    "collect_code_coverage": "false",
    "compilation_mode": "fastbuild",
    "dynamic_mode": "default",
    "experimental_cc_implementation_deps": "true",
    "experimental_cpp_modules": "false",
    "experimental_inmemory_dotd_files": "true",
    "experimental_save_feature_state": "false",
    "experimental_unsupported_and_brittle_include_scanning": "false",
    "experimental_use_llvm_covmap": "false",
    "fission": "no",
    "force_pic": "false",
    "host_compilation_mode": "opt",
    "incompatible_remove_legacy_whole_archive": "true",
    "incompatible_strict_action_env": "true",
    "incompatible_use_specific_tool_files": "true",
    "interface_shared_objects": "true",
    "legacy_whole_archive": "true",
    "save_temps": "false",
    "share_native_deps": "true",
    "strip": _STRIP_BASELINE,
}

_CPP_TRANSITION_BASELINE = {
    "//command_line_option:build_test_dwp": False,
    "//command_line_option:cc_dotd_files": True,
    "//command_line_option:cc_include_scanning": False,
    "//command_line_option:collect_code_coverage": False,
    "//command_line_option:conlyopt": [],
    "//command_line_option:copt": [],
    "//command_line_option:cpu": "k8",
    "//command_line_option:crosstool_top": Label("@bazel_tools//tools/cpp:toolchain"),
    "//command_line_option:cs_fdo_absolute_path": None,
    "//command_line_option:cs_fdo_instrument": None,
    "//command_line_option:cs_fdo_profile": None,
    "//command_line_option:custom_malloc": None,
    "//command_line_option:cxxopt": ["-std=c++20"],
    "//command_line_option:define": [],
    "//command_line_option:extra_execution_platforms": [],
    "//command_line_option:extra_toolchains": [],
    "//command_line_option:fdo_instrument": None,
    "//command_line_option:fdo_optimize": None,
    "//command_line_option:fdo_prefetch_hints": None,
    "//command_line_option:fdo_profile": None,
    "//command_line_option:features": [],
    "//command_line_option:host_conlyopt": [],
    "//command_line_option:host_copt": [],
    "//command_line_option:host_cpu": "k8",
    "//command_line_option:host_cxxopt": [],
    "//command_line_option:host_features": [],
    "//command_line_option:host_linkopt": [],
    "//command_line_option:host_per_file_copt": [],
    "//command_line_option:host_platform": Label("@bazel_tools//tools:host_platform"),
    "//command_line_option:interface_shared_objects": True,
    "//command_line_option:legacy_whole_archive": True,
    "//command_line_option:linkopt": [],
    "//command_line_option:ltobackendopt": [],
    "//command_line_option:ltoindexopt": [],
    "//command_line_option:memprof_profile": None,
    "//command_line_option:per_file_ltobackendopt": [],
    "//command_line_option:platforms": [Label("@bazel_tools//tools:host_platform")],
    "//command_line_option:process_headers_in_dependencies": False,
    "//command_line_option:propeller_optimize": None,
    "//command_line_option:propeller_optimize_absolute_cc_profile": None,
    "//command_line_option:propeller_optimize_absolute_ld_profile": None,
    "//command_line_option:proto_profile_path": None,
    "//command_line_option:save_temps": False,
    "//command_line_option:share_native_deps": True,
    "//command_line_option:stripopt": [],
}

_CPP_CONFIGURATION_BASELINE_MANIFEST = {
    "action_env": [],
    "build_test_dwp": False,
    "cc_dotd_files": True,
    "cc_include_scanning": False,
    "collect_code_coverage": False,
    "compilation_mode": "fastbuild",
    "cpu": "k8",
    # buildifier: disable=canonical-repository
    "crosstool_top": "@@bazel_tools//tools/cpp:toolchain",
    "cs_fdo_absolute_path": None,
    "cs_fdo_instrument": None,
    "cs_fdo_profile": None,
    "custom_malloc": None,
    "define": [],
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
    # buildifier: disable=canonical-repository
    "host_platform": "@@bazel_tools//tools:host_platform",
    "incompatible_remove_legacy_whole_archive": True,
    "incompatible_strict_action_env": True,
    "incompatible_use_specific_tool_files": True,
    "interface_shared_objects": True,
    "legacy_whole_archive": True,
    "lto_backend_options": [],
    "lto_index_options": [],
    "memprof_profile": None,
    "per_file_copt": [],
    "per_file_lto_backend_options": [],
    # buildifier: disable=canonical-repository
    "platforms": ["@@bazel_tools//tools:host_platform"],
    "process_headers_in_dependencies": False,
    "propeller_optimize": None,
    "propeller_optimize_absolute_cc_profile": None,
    "propeller_optimize_absolute_ld_profile": None,
    "proto_profile_path": None,
    "save_temps": False,
    "share_native_deps": True,
    "strip": _STRIP_BASELINE,
    "stripopts": [],
}

def _reject_untracked_transition_options(settings):
    if settings[_ACTION_ENV_OPTION]:
        fail(
            "ADR-067 forbids command-line action_env: %s" %
            settings[_ACTION_ENV_OPTION],
        )
    if settings[_HOST_ACTION_ENV_OPTION]:
        fail(
            "ADR-067 forbids command-line host_action_env: %s" %
            settings[_HOST_ACTION_ENV_OPTION],
        )
    if settings[_PER_FILE_COPT_OPTION]:
        fail(
            "ADR-067 forbids command-line per_file_copt: %s" %
            settings[_PER_FILE_COPT_OPTION],
        )
    for option, expected in _CPP_TRANSITION_BASELINE.items():
        if settings[option] != expected:
            fail(
                "ADR-067 C++ configuration drift for %s: %s != %s" %
                (option, settings[option], expected),
            )

def _reset_apgar_sanitizers_impl(settings, _attr):
    _reject_untracked_transition_options(settings)
    return {
        _ASAN_SETTING: False,
        _UBSAN_SETTING: False,
    }

_TRANSITION_INPUTS = [
    _ACTION_ENV_OPTION,
    _HOST_ACTION_ENV_OPTION,
    _PER_FILE_COPT_OPTION,
] + sorted(_CPP_TRANSITION_BASELINE)
_TRANSITION_OUTPUTS = [
    _ASAN_SETTING,
    _UBSAN_SETTING,
]

_reset_apgar_sanitizers = transition(
    implementation = _reset_apgar_sanitizers_impl,
    inputs = _TRANSITION_INPUTS,
    outputs = _TRANSITION_OUTPUTS,
)

def _preserve_apgar_sanitizers_impl(settings, _attr):
    _reject_untracked_transition_options(settings)
    return {
        _ASAN_SETTING: settings[_ASAN_SETTING],
        _UBSAN_SETTING: settings[_UBSAN_SETTING],
    }

_preserve_apgar_sanitizers = transition(
    implementation = _preserve_apgar_sanitizers_impl,
    inputs = _TRANSITION_INPUTS + _TRANSITION_OUTPUTS,
    outputs = _TRANSITION_OUTPUTS,
)

def _runfiles_violation(runfiles, executable):
    if runfiles.symlinks.to_list():
        return "workspace symlink"
    if runfiles.root_symlinks.to_list():
        return "root symlink"
    if runfiles.empty_filenames.to_list():
        return "empty filename"
    files = runfiles.files.to_list()
    if executable not in files:
        return "missing executable"
    if [file for file in files if file != executable]:
        return "unexpected file"
    return None

def _expect_binary_provider(info, owner, path, subject):
    executable = info.files_to_run.executable
    files = info.files.to_list()
    if executable == None:
        fail("%s has no executable" % subject)
    if len(files) != 1 or files[0] != executable:
        fail("%s must expose exactly its executable in DefaultInfo.files" % subject)
    if executable.short_path != path:
        fail("%s observed executable path substitution" % subject)
    if executable.owner != owner:
        fail("%s observed executable owner substitution" % subject)
    for kind, runfiles in (
        ("default", info.default_runfiles),
        ("data", info.data_runfiles),
    ):
        violation = _runfiles_violation(runfiles, executable)
        if violation != None:
            fail("%s %s runfiles contain a forbidden %s" % (subject, kind, violation))
    return executable

def _label_strings(labels):
    return sorted([str(label) for label in labels])

def _expect_exact_labels(actual, expected, subject):
    actual_strings = _label_strings(actual)
    expected_strings = _label_strings(expected)
    if actual_strings == expected_strings:
        return
    fail(
        "%s differs; missing=%s unexpected=%s" % (
            subject,
            [label for label in expected_strings if label not in actual_strings],
            [label for label in actual_strings if label not in expected_strings],
        ),
    )

def _expect_direct_attributes(info, expected, subject):
    for name in _TRACKED_LABEL_ATTRIBUTES:
        _expect_exact_labels(
            info.direct_attribute_labels[name],
            expected.get(name, ()),
            "%s resolved %s" % (subject, name),
        )

def _expect_value_attributes(info, expected, subject):
    for name in _TRACKED_VALUE_ATTRIBUTES:
        actual = info.direct_attribute_values[name]
        wanted = expected.get(name)
        if actual != wanted:
            fail(
                "%s resolved %s differs: actual=%s expected=%s" % (
                    subject,
                    name,
                    actual,
                    wanted,
                ),
            )

def _expected_library_record(archive, objects, alwayslink = False):
    if archive == "":
        return ""
    return ",".join([
        "alwayslink=" + ("1" if alwayslink else "0"),
        "static=",
        "pic_static=" + archive,
        "dynamic=",
        "interface=",
        "resolved_dynamic=",
        "resolved_interface=",
        "objects=",
        "pic_objects=" + ":".join(sorted(objects)),
    ])

def _link_spec(label, archive = "", objects = (), flags = (), alwayslink = False):
    return struct(
        label = label,
        archive = archive,
        objects = objects,
        flags = flags,
        alwayslink = alwayslink,
    )

def _expected_linker_record(spec):
    library = _expected_library_record(
        spec.archive,
        spec.objects,
        alwayslink = spec.alwayslink,
    )
    return ";".join([
        "owner=" + str(spec.label),
        "libraries=" + library,
        "flags=" + ":".join(spec.flags),
        "additional=",
        "linkstamps=",
    ])

_COMMON_LINK_SPECS = (
    _link_spec(Label("//:phase3_commit")),
    _link_spec(
        Label("//:cpu_candidate_allocation_session"),
        "libcpu_candidate_allocation_session.a",
        ("_objs/cpu_candidate_allocation_session/cpu_candidate_allocation_session.pic.o",),
    ),
    _link_spec(
        Label("//:multi_world_allocator"),
        "libmulti_world_allocator.a",
        ("_objs/multi_world_allocator/multi_world.pic.o",),
    ),
    _link_spec(
        Label("//:cpu_candidate_pool_preparation"),
        "libcpu_candidate_pool_preparation.a",
        ("_objs/cpu_candidate_pool_preparation/cpu_candidate_pool_preparation.pic.o",),
        ("-pthread",),
    ),
    _link_spec(
        Label("//:phase4_imported_multi_net_corpus"),
        "libphase4_imported_multi_net_corpus.a",
        ("_objs/phase4_imported_multi_net_corpus/phase4_corpus.pic.o",),
    ),
    _link_spec(
        Label("//:kicad_fixture_adapter"),
        "libkicad_fixture_adapter.a",
        ("_objs/kicad_fixture_adapter/kicad_fixture.pic.o",),
    ),
    _link_spec(
        Label("//:sequential_negotiated_baseline"),
        "libsequential_negotiated_baseline.a",
        ("_objs/sequential_negotiated_baseline/sequential_negotiated_baseline.pic.o",),
    ),
    _link_spec(
        Label("//:targeted_regeneration_execution"),
        "libtargeted_regeneration_execution.a",
        ("_objs/targeted_regeneration_execution/targeted_regeneration_execution.pic.o",),
    ),
    _link_spec(
        Label("//:targeted_regeneration"),
        "libtargeted_regeneration.a",
        ("_objs/targeted_regeneration/targeted_regeneration.pic.o",),
    ),
    _link_spec(
        Label("//:negotiated_prices"),
        "libnegotiated_prices.a",
        ("_objs/negotiated_prices/negotiated_prices.pic.o",),
    ),
    _link_spec(
        Label("//:one_world_allocator"),
        "libone_world_allocator.a",
        ("_objs/one_world_allocator/one_world.pic.o",),
    ),
    _link_spec(
        Label("//:candidate_store"),
        "libcandidate_store.a",
        ("_objs/candidate_store/candidate_store.pic.o",),
    ),
    _link_spec(
        Label("//:candidate_contract"),
        "libcandidate_contract.a",
        ("_objs/candidate_contract/route_candidate.pic.o",),
    ),
    _link_spec(
        Label("//:cpu_astar"),
        "libcpu_astar.a",
        ("_objs/cpu_astar/cpu_astar.pic.o",),
    ),
    _link_spec(
        Label("//:multi_net_workload"),
        "libmulti_net_workload.a",
        ("_objs/multi_net_workload/multi_net_workload.pic.o",),
    ),
    _link_spec(
        Label("//:planar_route"),
        "libplanar_route.a",
        ("_objs/planar_route/planar_route.pic.o",),
    ),
    _link_spec(
        Label("//:candidate_policy"),
        "libcandidate_policy.a",
        ("_objs/candidate_policy/candidate_policy.pic.o",),
    ),
    _link_spec(
        Label("//:geometry_compiler"),
        "libgeometry_compiler.a",
        ("_objs/geometry_compiler/compiled_board.pic.o",),
    ),
    _link_spec(
        Label("//:exact_geometry"),
        "libexact_geometry.a",
        ("_objs/exact_geometry/exact.pic.o",),
    ),
    _link_spec(
        Label("//:board_ir"),
        "libboard_ir.a",
        ("_objs/board_ir/board.pic.o",),
    ),
    _link_spec(Label("//:utf8")),
)

_PRODUCTION_LINK_SPECS = (
    _link_spec(
        _PRODUCTION,
        "libphase4_h4096_session_v5_same_run_per_net_report_producer_preflight.a",
        ("_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight.pic.o",),
    ),
    _link_spec(
        _EXECUTION_PRODUCTION,
        "libphase4_h4096_session_v5_execution_preflight.a",
        ("_objs/phase4_h4096_session_v5_execution_preflight/phase4_confirmatory_h4096_session_v5_execution.pic.o",),
    ),
    _link_spec(Label("//:phase4_h4096_session_v5_canonical_budget_preimage")),
    _link_spec(Label("//:phase4_h4096_canonical_budget_preimage")),
    _link_spec(
        Label("//:phase4_trial_harness_preimage"),
        "libphase4_trial_harness_preimage.a",
        ("_objs/phase4_trial_harness_preimage/phase4_trial_harness.pic.o",),
    ),
    _link_spec(
        Label("//:phase4_paired_trial_preimage"),
        "libphase4_paired_trial_preimage.a",
        ("_objs/phase4_paired_trial_preimage/phase4_paired_trial.pic.o",),
    ),
    _link_spec(
        Label("//:phase4_representative_corpus_preimage"),
        "libphase4_representative_corpus_preimage.a",
        ("_objs/phase4_representative_corpus_preimage/phase4_representative_corpus.pic.o",),
    ),
) + _COMMON_LINK_SPECS

_TEST_SUPPORT_LINK_SPECS = (
    _link_spec(
        _TEST_SUPPORT,
        "libphase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support.a",
        ("_objs/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test_support/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight.pic.o",),
    ),
    _link_spec(
        _EXECUTION_TEST_SUPPORT,
        "libphase4_h4096_session_v5_execution_preflight_test_support.a",
        ("_objs/phase4_h4096_session_v5_execution_preflight_test_support/phase4_confirmatory_h4096_session_v5_execution.pic.o",),
    ),
    _link_spec(
        Label("//:phase4_trial_harness"),
        "libphase4_trial_harness.a",
        (
            "_objs/phase4_trial_harness/phase4_same_run_decision_telemetry.pic.o",
            "_objs/phase4_trial_harness/phase4_trial_artifact.pic.o",
            "_objs/phase4_trial_harness/phase4_trial_harness.pic.o",
            "_objs/phase4_trial_harness/phase4_trial_process.pic.o",
            "_objs/phase4_trial_harness/phase4_trial_wire_internal.pic.o",
        ),
        ("-pthread",),
    ),
    _link_spec(
        Label("//:phase4_paired_trial"),
        "libphase4_paired_trial.a",
        ("_objs/phase4_paired_trial/phase4_paired_trial.pic.o",),
    ),
    _link_spec(
        Label("//:phase4_representative_corpus"),
        "libphase4_representative_corpus.a",
        ("_objs/phase4_representative_corpus/phase4_representative_corpus.pic.o",),
    ),
) + _COMMON_LINK_SPECS

_GTEST_LINK_SPECS = (
    _link_spec(
        _GTEST_MAIN,
        "../googletest+/libgtest_main.a",
        ("../googletest+/_objs/gtest_main/gmock_main.pic.o",),
    ),
    _link_spec(
        _GTEST,
        "../googletest+/libgtest.a",
        (
            "../googletest+/_objs/gtest/gmock-cardinalities.pic.o",
            "../googletest+/_objs/gtest/gmock-internal-utils.pic.o",
            "../googletest+/_objs/gtest/gmock-matchers.pic.o",
            "../googletest+/_objs/gtest/gmock-spec-builders.pic.o",
            "../googletest+/_objs/gtest/gmock.pic.o",
            "../googletest+/_objs/gtest/gtest-assertion-result.pic.o",
            "../googletest+/_objs/gtest/gtest-death-test.pic.o",
            "../googletest+/_objs/gtest/gtest-filepath.pic.o",
            "../googletest+/_objs/gtest/gtest-matchers.pic.o",
            "../googletest+/_objs/gtest/gtest-port.pic.o",
            "../googletest+/_objs/gtest/gtest-printers.pic.o",
            "../googletest+/_objs/gtest/gtest-test-part.pic.o",
            "../googletest+/_objs/gtest/gtest-typed-test.pic.o",
            "../googletest+/_objs/gtest/gtest.pic.o",
        ),
        ("-pthread",),
    ),
)

_LINK_EXTRA_SPECS = (
    _link_spec(_RULES_CC_LINK_EXTRA_LIB),
    _link_spec(_RULES_CC_EMPTY_LIB),
)

_MALLOC_LINK_SPECS = (
    _link_spec(_BAZEL_MALLOC),
)

_BASE_COPTS = (
    "-Wall",
    "-Werror",
    "-Wextra",
    "-Wpedantic",
)

def _library_value_attributes(copts):
    return {
        "alwayslink": False,
        "copts": copts,
        "defines": (),
        "features": (),
        "include_prefix": "",
        "includes": (),
        "linkopts": (),
        "linkshared": None,
        "linkstatic": False,
        "local_defines": (),
        "nocopts": None,
        "stamp": None,
        "strip_include_prefix": "",
    }

def _binary_value_attributes(copts, defines = ()):
    return {
        "alwayslink": None,
        "copts": copts,
        "defines": defines,
        "features": (),
        "include_prefix": None,
        "includes": (),
        "linkopts": ("-Wl,--gc-sections",),
        "linkshared": False,
        "linkstatic": True,
        "local_defines": (),
        "nocopts": "",
        "stamp": 0,
        "strip_include_prefix": None,
    }

def _record_field(record, name):
    prefix = name + "="
    for field in record.split(";"):
        if field.startswith(prefix):
            return field[len(prefix):]
    return ""

def _linker_inventory_violation(actual, expected):
    actual = sorted(actual)
    expected = sorted(expected)
    if actual == expected:
        return None
    actual_owners = sorted([record.split(";", 1)[0] for record in actual])
    expected_owners = sorted([record.split(";", 1)[0] for record in expected])
    if [owner for owner in expected_owners if owner not in actual_owners]:
        return "missing owner"
    unexpected_records = [
        record
        for record in actual
        if record.split(";", 1)[0] not in expected_owners
    ]
    if any([
        "alwayslink=1" in _record_field(record, "libraries") and
        "initializer" in _record_field(record, "libraries")
        for record in unexpected_records
    ]):
        return _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER
    if unexpected_records:
        return "unexpected owner"
    joined = "\n".join(actual)
    if "alwayslink=1" in joined:
        return "always-link input"
    if any([_record_field(record, "additional") != "" for record in actual]):
        return "additional linker input"
    if any([_record_field(record, "linkstamps") != "" for record in actual]):
        return "linkstamp"
    return "linker artifact or flag drift"

def _expect_linker_inventory(info, specs, subject):
    actual = info.linker_inputs.to_list()
    expected = [_expected_linker_record(spec) for spec in specs]
    violation = _linker_inventory_violation(actual, expected)
    if violation != None:
        fail(
            "%s has forbidden %s; actual=%s expected=%s" % (
                subject,
                violation,
                sorted(actual),
                sorted(expected),
            ),
        )

def _expect_target_contracts(
        configuration,
        production,
        test_support,
        direct_test,
        runner,
        forced_runner):
    sanitizer_copts = (
        _BASE_COPTS + ("-fno-sanitize-recover=all",) if configuration == "ubsan" else _BASE_COPTS
    )
    test_support_copts = (
        _BASE_COPTS +
        ("-fdata-sections", "-ffunction-sections") +
        (("-fno-sanitize-recover=all",) if configuration == "ubsan" else ())
    )
    expected = (
        (
            production,
            _PRODUCTION_APGAR_LABELS,
            {
                "deps": (_EXECUTION_PRODUCTION,),
                "hdrs": (_PRODUCER_HEADER,),
                "srcs": (_PRODUCER_SOURCE,),
            },
            _PRODUCTION_LINK_SPECS,
            (),
            False,
            _library_value_attributes(
                _BASE_COPTS + ("-fdata-sections", "-ffunction-sections"),
            ),
            "the production same-run producer-preflight library",
        ),
        (
            test_support,
            _TEST_SUPPORT_APGAR_LABELS,
            {
                "deps": (_EXECUTION_TEST_SUPPORT,),
                "hdrs": (_PRODUCER_HEADER,),
                "srcs": (_PRODUCER_SOURCE,),
            },
            _TEST_SUPPORT_LINK_SPECS,
            (),
            False,
            _library_value_attributes(test_support_copts),
            "the same-run producer-preflight test-support library",
        ),
        (
            direct_test,
            (_DIRECT_TEST,) + _TEST_SUPPORT_APGAR_LABELS,
            {
                "deps": (_TEST_SUPPORT, _GTEST_MAIN, _RULES_CC_LINK_EXTRA_LIB),
                "malloc": (_BAZEL_MALLOC,),
                "link_extra_lib": (_BAZEL_LINK_EXTRA_LIB,),
                "srcs": (_DIRECT_TEST_SOURCE, _GOOGLE_TEST_SUPPORT_HEADER),
            },
            _TEST_SUPPORT_LINK_SPECS + _GTEST_LINK_SPECS + _LINK_EXTRA_SPECS + _MALLOC_LINK_SPECS,
            ("-Wl,--gc-sections",),
            True,
            _binary_value_attributes(sanitizer_copts),
            "the same-run producer-preflight direct test",
        ),
        (
            runner,
            (_RUNNER,) + _PRODUCTION_APGAR_LABELS,
            {
                "deps": (_PRODUCTION, _RULES_CC_LINK_EXTRA_LIB),
                "malloc": (_BAZEL_MALLOC,),
                "link_extra_lib": (_BAZEL_LINK_EXTRA_LIB,),
                "srcs": (_RUNNER_SOURCE,),
            },
            _PRODUCTION_LINK_SPECS + _LINK_EXTRA_SPECS + _MALLOC_LINK_SPECS,
            ("-Wl,--gc-sections",),
            True,
            _binary_value_attributes(_BASE_COPTS),
            "the same-run producer-preflight runner",
        ),
        (
            forced_runner,
            (_FORCED_RUNNER,) + _PRODUCTION_APGAR_LABELS,
            {
                "deps": (_PRODUCTION, _RULES_CC_LINK_EXTRA_LIB),
                "malloc": (_BAZEL_MALLOC,),
                "link_extra_lib": (_BAZEL_LINK_EXTRA_LIB,),
                "srcs": (_RUNNER_SOURCE,),
            },
            _PRODUCTION_LINK_SPECS + _LINK_EXTRA_SPECS + _MALLOC_LINK_SPECS,
            ("-Wl,--gc-sections",),
            True,
            _binary_value_attributes(
                _BASE_COPTS,
                defines = (
                    "APGAR_PHASE4_CONFIRMATORY_H4096_SESSION_V5_FORCE_UNPUBLISHABLE_SOURCE_FOR_TESTING",
                ),
            ),
            "the forced-unpublishable same-run producer-preflight runner",
        ),
    )
    for target, labels, direct_attributes, link_specs, linkopts, linkstatic, value_attributes, subject in expected:
        info = target[_ApgarLinkClosureInfo]
        _expect_exact_labels(info.apgar_labels.to_list(), labels, subject + " APGAR closure")
        _expect_direct_attributes(info, direct_attributes, subject)
        _expect_value_attributes(info, value_attributes, subject)
        if info.configuration_copts:
            fail("%s has global C compiler options: %s" % (subject, info.configuration_copts))
        if info.configuration_cxxopts != ("-std=c++20",):
            fail(
                "%s global C++ compiler options differ: %s" % (
                    subject,
                    info.configuration_cxxopts,
                ),
            )
        if info.configuration_linkopts:
            fail("%s has global linker options: %s" % (subject, info.configuration_linkopts))
        if info.configuration_features:
            fail(
                "%s has global requested features: %s" % (
                    subject,
                    info.configuration_features,
                ),
            )
        if info.configuration_disabled_features:
            fail(
                "%s has global disabled features: %s" % (
                    subject,
                    info.configuration_disabled_features,
                ),
            )
        _expect_linker_inventory(info, link_specs, subject)
        if info.alwayslink:
            fail("%s must not be always-linked" % subject)
        if info.features:
            fail("%s has unexpected target-local features: %s" % (subject, info.features))
        if info.linkopts != linkopts:
            fail("%s linkopts differ: %s" % (subject, info.linkopts))
        if info.linkstatic != linkstatic:
            fail("%s linkstatic differs: %s" % (subject, info.linkstatic))

    _expect_binary_provider(
        direct_test[DefaultInfo],
        _DIRECT_TEST,
        _DIRECT_TEST_PATH,
        "the same-run producer-preflight direct test",
    )
    _expect_binary_provider(
        runner[DefaultInfo],
        _RUNNER,
        _RUNNER_PATH,
        "the same-run producer-preflight runner",
    )
    _expect_binary_provider(
        forced_runner[DefaultInfo],
        _FORCED_RUNNER,
        _FORCED_RUNNER_PATH,
        "the forced-unpublishable same-run producer-preflight runner",
    )

def _semantic_audit(
        ctx,
        kind,
        extra_inputs = [],
        provider_inventories = [],
        provider_proofs = []):
    return phase4_same_run_run_semantic_audit(
        ctx,
        kind,
        ctx.attr._production[0],
        ctx.attr._test_support[0],
        ctx.attr._direct_test[0],
        ctx.attr._runner[0],
        ctx.attr._forced_runner[0],
        extra_inputs = extra_inputs,
        provider_inventories = provider_inventories,
        provider_proofs = provider_proofs,
    )

def _audit_targets(ctx):
    return (
        ctx.attr._production[0],
        ctx.attr._test_support[0],
        ctx.attr._direct_test[0],
        ctx.attr._runner[0],
        ctx.attr._forced_runner[0],
    )

def _startup_artifact_records(targets):
    records = []
    for target in targets:
        records.extend(target[_ApgarLinkClosureInfo].linker_artifacts)
    return _deduplicated_artifact_records(records)

def _negative_initializer_impl(ctx):
    source = ctx.actions.declare_file(
        "%s.cc" % ctx.label.name,
    )
    ctx.actions.write(
        output = source,
        content = """extern \"C\" __attribute__((constructor))
void ApgarPhase4SyntheticAllowlistedOwnerStartupForNegativeAudit() {}
""",
    )

    cc_toolchain = find_cc_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    _, compilation_outputs = cc_common.compile(
        actions = ctx.actions,
        cc_toolchain = cc_toolchain,
        cxx_flags = ctx.fragments.cpp.cxxopts,
        feature_configuration = feature_configuration,
        name = ctx.label.name,
        srcs = [source],
        user_compile_flags = ctx.fragments.cpp.copts,
    )
    objects = _files(compilation_outputs.objects) + _files(compilation_outputs.pic_objects)
    objects = depset(objects).to_list()
    if len(objects) != 1:
        fail(
            "startup provenance audit expected exactly one live initializer object",
        )
    object_file = objects[0]
    linking_context, _ = cc_common.create_linking_context_from_compilation_outputs(
        actions = ctx.actions,
        alwayslink = ctx.attr.alwayslink,
        cc_toolchain = cc_toolchain,
        compilation_outputs = compilation_outputs,
        disallow_dynamic_library = True,
        feature_configuration = feature_configuration,
        name = ctx.label.name,
    )
    return [
        CcInfo(linking_context = linking_context),
        DefaultInfo(files = depset([object_file])),
        _ApgarNegativeInitializerInfo(object = object_file),
    ]

_negative_initializer = rule(
    implementation = _negative_initializer_impl,
    attrs = {
        "alwayslink": attr.bool(default = True),
    },
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _semantic_configuration(ctx):
    asan = ctx.attr._semantic_audit_asan_setting[BuildSettingInfo].value
    ubsan = ctx.attr._semantic_audit_ubsan_setting[BuildSettingInfo].value
    if asan and ubsan:
        fail("ASan and UBSan cannot both be enabled for ADR-067 evidence")
    if asan:
        return "asan"
    if ubsan:
        return "ubsan"
    return "normal"

def _configuration_manifest(target):
    configuration = dict(_CPP_CONFIGURATION_BASELINE_MANIFEST)
    configuration.update({
        "conlyopts": target[_ApgarLinkClosureInfo].configuration_conlyopts,
        "copts": target[_ApgarLinkClosureInfo].configuration_copts,
        "cxxopts": target[_ApgarLinkClosureInfo].configuration_cxxopts,
        "disabled_features": target[_ApgarLinkClosureInfo].configuration_disabled_features,
        "features": target[_ApgarLinkClosureInfo].configuration_features,
        "linkopts": target[_ApgarLinkClosureInfo].configuration_linkopts,
    })
    return configuration

def _negative_initializer_evidence(ctx):
    target = ctx.attr.foreign_alwayslink_initializer
    if CcInfo not in target or _ApgarNegativeInitializerInfo not in target:
        fail("negative startup fixture lost its CcInfo or live object provider")
    if _ApgarLinkClosureInfo not in target:
        fail("negative startup fixture lost its linker-closure audit provider")

    closure = target[_ApgarLinkClosureInfo]
    actual_linker_inputs = target[CcInfo].linking_context.linker_inputs.to_list()
    if len(actual_linker_inputs) != 1:
        fail("negative startup fixture must expose exactly one real LinkerInput")
    actual_record = _linker_input_record(actual_linker_inputs[0])
    aspect_records = closure.linker_inputs.to_list()
    if aspect_records != [actual_record]:
        fail(
            "negative startup fixture aspect did not serialize its real LinkerInput: %s != %s" %
            (aspect_records, [actual_record]),
        )
    if not closure.alwayslink:
        fail("negative startup fixture target lost alwayslink=True")
    libraries = actual_linker_inputs[0].libraries
    if len(libraries) != 1 or not libraries[0].alwayslink:
        fail("negative startup fixture LinkerInput lost alwayslink=True")

    object_file = target[_ApgarNegativeInitializerInfo].object
    artifact_records = closure.linker_artifacts
    if (
        len(artifact_records) != 2 or
        len([record for record in artifact_records if record.file == object_file]) != 1 or
        len([record for record in artifact_records if record.file.basename.endswith(".lo")]) != 1 or
        [record for record in artifact_records if record.owner != str(target.label)]
    ):
        fail(
            "negative startup fixture object/provider provenance drifted: object=%s records=%s" %
            (object_file, artifact_records),
        )
    if (
        _linker_inventory_violation([actual_record], []) !=
        _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER
    ):
        fail(
            "the real foreign alwayslink initializer did not trigger [%s]" %
            _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER,
        )
    return struct(
        alwayslink = closure.alwayslink,
        artifact_records = artifact_records,
        linker_input_records = aspect_records,
        object = object_file,
        owner = str(target.label),
        target_label = str(target.label),
    )

def _run_startup_provenance_audit(ctx, kind):
    if ctx.attr._startup_provenance_checker.label != _STARTUP_PROVENANCE_CHECKER:
        fail("startup provenance checker label substitution")
    if (
        str(ctx.attr._llvm_readobj.label) != _LLVM_READOBJ_RESOLVED_OWNER or
        str(ctx.file._llvm_readobj.owner) != _LLVM_READOBJ_RESOLVED_OWNER or
        ctx.file._llvm_readobj.basename != "llvm-readobj"
    ):
        fail(
            "startup provenance llvm-readobj substitution: label=%s owner=%s file=%s" % (
                ctx.attr._llvm_readobj.label,
                ctx.file._llvm_readobj.owner,
                ctx.file._llvm_readobj.basename,
            ),
        )

    targets = _audit_targets(ctx)
    direct_test_elf = _expect_binary_provider(
        targets[2][DefaultInfo],
        _DIRECT_TEST,
        _DIRECT_TEST_PATH,
        "the direct same-run producer-preflight test",
    )
    records = _startup_artifact_records(targets)
    owners_by_path = {}
    for record in records:
        if record.file.path not in owners_by_path:
            owners_by_path[record.file.path] = []
        owners_by_path[record.file.path].append(record.owner)
    ambiguous = [
        path
        for path, owners in owners_by_path.items()
        if [owner for owner in owners if owner != owners[0]]
    ]
    if ambiguous:
        fail("linker artifacts have ambiguous owners: %s" % sorted(ambiguous))

    configuration = _semantic_configuration(ctx)
    negative_fixture = None
    if kind == "negative":
        negative_fixture = _negative_initializer_evidence(ctx)

    negative_fixtures = None
    if negative_fixture != None:
        negative_fixtures = [
            {
                "alwayslink": False,
                "diagnostic_id": _DIAGNOSTIC_ALLOWLISTED_OWNER_INITIALIZER,
                "owner": _ALLOWLISTED_INITIALIZER_OWNER,
                "path": negative_fixture.object.path,
                "short_path": negative_fixture.object.short_path,
            },
            {
                "alwayslink": negative_fixture.alwayslink,
                "diagnostic_id": _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER,
                "owner": negative_fixture.owner,
                "path": negative_fixture.object.path,
                "short_path": negative_fixture.object.short_path,
            },
        ]

    manifest = ctx.actions.declare_file(
        "%s.startup-provenance-manifest.json" % ctx.label.name,
    )
    ctx.actions.write(
        output = manifest,
        content = json.encode_indent({
            "configuration": configuration,
            "direct_test_elf": {
                "owner": str(direct_test_elf.owner),
                "path": direct_test_elf.path,
                "short_path": direct_test_elf.short_path,
            },
            "kind": kind,
            "llvm_readobj_label": str(_LLVM_READOBJ),
            "llvm_readobj_resolved_owner": _LLVM_READOBJ_RESOLVED_OWNER,
            "negative_fixture_provider": None if negative_fixture == None else {
                "alwayslink": negative_fixture.alwayslink,
                "artifacts": [
                    {
                        "owner": record.owner,
                        "path": record.file.path,
                        "short_path": record.file.short_path,
                    }
                    for record in negative_fixture.artifact_records
                ],
                "linker_inputs": negative_fixture.linker_input_records,
                "target_label": negative_fixture.target_label,
            },
            "negative_fixtures": negative_fixtures,
            "records": [
                {
                    "owner": record.owner,
                    "path": record.file.path,
                    "short_path": record.file.short_path,
                }
                for record in records
            ],
            "schema_version": 1,
            "targets": [
                {
                    "configuration_options": _configuration_manifest(target),
                    "label": str(target.label),
                    "label_attributes": {
                        name: _label_strings(
                            target[_ApgarLinkClosureInfo].direct_attribute_labels[name],
                        )
                        for name in _TRACKED_LABEL_ATTRIBUTES
                    },
                    "value_attributes": target[_ApgarLinkClosureInfo].direct_attribute_values,
                }
                for target in targets
            ],
        }) + "\n",
    )

    stamp = ctx.actions.declare_file(
        "%s.startup-provenance.ok" % ctx.label.name,
    )
    arguments = ctx.actions.args()
    arguments.add("--startup-owner-audit")
    arguments.add("--llvm-readobj", ctx.file._llvm_readobj.path)
    arguments.add("--manifest", manifest.path)
    arguments.add("--stamp", stamp.path)
    direct_inputs = [manifest, direct_test_elf] + [record.file for record in records]
    if negative_fixture != None:
        direct_inputs.append(negative_fixture.object)
        direct_inputs.extend([
            record.file
            for record in negative_fixture.artifact_records
        ])
    ctx.actions.run(
        mnemonic = "Phase4SameRunStartupProvenanceAudit",
        progress_message = "Auditing ADR-067 startup provenance (%s, %s)" % (
            kind,
            configuration,
        ),
        executable = ctx.executable._startup_provenance_checker,
        arguments = [arguments],
        inputs = depset(direct_inputs),
        tools = [
            ctx.executable._startup_provenance_checker,
            ctx.file._llvm_readobj,
        ],
        outputs = [stamp],
    )
    return struct(
        manifest = manifest,
        record_files = depset(direct_inputs[1:]).to_list(),
        stamp = stamp,
    )

def _positive_audit_impl(ctx):
    if not ctx.attr.cpp_configuration_baseline:
        fail("ADR-067 requires the exact canonical C++ configuration baseline")
    _expect_target_contracts(_semantic_configuration(ctx), *_audit_targets(ctx))
    startup_audit = _run_startup_provenance_audit(ctx, "positive")
    stamp_info = _semantic_audit(
        ctx,
        "positive",
        extra_inputs = startup_audit.record_files,
        provider_inventories = [startup_audit.manifest],
        provider_proofs = [startup_audit.stamp],
    )
    return [
        stamp_info,
        DefaultInfo(files = depset([stamp_info.stamp])),
    ]

def _exercise_runfiles_classifiers(ctx):
    # Reuse declared semantic-audit inputs so this analysis-only classifier
    # proof cannot create an orphan output beside the one required stamp.
    executable = ctx.executable._semantic_audit_checker
    extra = ctx.file._semantic_audit_ubsan_ignorelist
    probes = (
        (ctx.runfiles(files = [executable, extra]), "unexpected file"),
        (ctx.runfiles(files = [extra]), "missing executable"),
        (
            ctx.runfiles(files = [executable], symlinks = {"forbidden/runner": extra}),
            "workspace symlink",
        ),
        (
            ctx.runfiles(files = [executable], root_symlinks = {"forbidden/runner": extra}),
            "root symlink",
        ),
        (
            struct(
                files = depset([executable]),
                symlinks = depset(),
                root_symlinks = depset(),
                empty_filenames = depset(["forbidden/empty"]),
            ),
            "empty filename",
        ),
    )
    for runfiles, expected in probes:
        if _runfiles_violation(runfiles, executable) != expected:
            fail("the runfiles audit failed to detect a synthetic %s" % expected)

def _exercise_linker_classifiers():
    baseline = ["owner=//:allowed;libraries=alwayslink=0;flags=;additional=;linkstamps="]
    probes = (
        ([], "missing owner"),
        (baseline + ["owner=//:foreign;libraries=;flags=;additional=;linkstamps="], "unexpected owner"),
        (
            baseline + [
                "owner=//:foreign;libraries=alwayslink=1,pic_objects=foreign_initializer.pic.o;flags=;additional=;linkstamps=",
            ],
            _DIAGNOSTIC_FOREIGN_ALWAYSLINK_INITIALIZER,
        ),
        (["owner=//:allowed;libraries=alwayslink=1;flags=;additional=;linkstamps="], "always-link input"),
        (["owner=//:allowed;libraries=alwayslink=0;flags=;additional=foreign.o;linkstamps="], "additional linker input"),
        (["owner=//:allowed;libraries=alwayslink=0;flags=;additional=;linkstamps=foreign.cc"], "linkstamp"),
        (["owner=//:allowed;libraries=alwayslink=0;flags=-lforeign;additional=;linkstamps="], "linker artifact or flag drift"),
    )
    for actual, expected in probes:
        if _linker_inventory_violation(actual, baseline) != expected:
            fail("the linker-input audit failed to detect a synthetic %s" % expected)

def _python_test_execution_violation(
        args,
        env,
        env_inherit,
        tags,
        flaky,
        local,
        shard_count):
    if args:
        return "argument"
    if env:
        return "environment"
    if env_inherit:
        return "inherited environment"
    if tags:
        return "tag"
    if flaky:
        return "flaky execution"
    if local:
        return "local execution"
    if shard_count != -1:
        return "sharding"
    return None

def _exercise_python_test_execution_classifiers():
    baseline = struct(
        args = [],
        env = {},
        env_inherit = [],
        flaky = False,
        local = False,
        shard_count = -1,
        tags = [],
    )
    probes = (
        (struct(args = ["--filter"], env = {}, env_inherit = [], flaky = False, local = False, shard_count = -1, tags = []), "argument"),
        (struct(args = [], env = {"GTEST_FILTER": "*"}, env_inherit = [], flaky = False, local = False, shard_count = -1, tags = []), "environment"),
        (struct(args = [], env = {}, env_inherit = ["PYTHONWARNINGS"], flaky = False, local = False, shard_count = -1, tags = []), "inherited environment"),
        (struct(args = [], env = {}, env_inherit = [], flaky = False, local = False, shard_count = -1, tags = ["manual"]), "tag"),
        (struct(args = [], env = {}, env_inherit = [], flaky = True, local = False, shard_count = -1, tags = []), "flaky execution"),
        (struct(args = [], env = {}, env_inherit = [], flaky = False, local = True, shard_count = -1, tags = []), "local execution"),
        (struct(args = [], env = {}, env_inherit = [], flaky = False, local = False, shard_count = 2, tags = []), "sharding"),
    )
    if _python_test_execution_violation(
        baseline.args,
        baseline.env,
        baseline.env_inherit,
        baseline.tags,
        baseline.flaky,
        baseline.local,
        baseline.shard_count,
    ) != None:
        fail("the Python test execution classifier rejected its exact baseline")
    for probe, expected in probes:
        actual = _python_test_execution_violation(
            probe.args,
            probe.env,
            probe.env_inherit,
            probe.tags,
            probe.flaky,
            probe.local,
            probe.shard_count,
        )
        if actual != expected:
            fail("the Python test execution audit failed to detect %s" % expected)

def _synthetic_semantic_stamp_actual(
        stamp,
        files,
        target_label = _POSITIVE_AUDIT,
        stamp_owner = _POSITIVE_AUDIT):
    return struct(
        configuration = "normal",
        files = files,
        info_basename = _POSITIVE_BASENAME,
        invariant = _POSITIVE_INVARIANT,
        kind = "positive",
        stamp = stamp,
        stamp_basename = _POSITIVE_BASENAME,
        stamp_owner = stamp_owner,
        target_label = target_label,
    )

def _exercise_semantic_stamp_classifiers(ctx):
    stamp = ctx.file._semantic_audit_ubsan_ignorelist
    extra = ctx.executable._semantic_audit_checker
    expected = struct(
        basename = _POSITIVE_BASENAME,
        configuration = "normal",
        invariant = _POSITIVE_INVARIANT,
        kind = "positive",
        owner = _POSITIVE_AUDIT,
    )
    baseline = _synthetic_semantic_stamp_actual(stamp, [stamp])
    if _semantic_stamp_contract_violation(baseline, expected) != None:
        fail("the semantic-stamp classifier rejected its exact baseline")
    probes = (
        (
            _synthetic_semantic_stamp_actual(
                stamp,
                [stamp],
                target_label = _NEGATIVE_AUDIT,
            ),
            _DIAGNOSTIC_STAMP_OWNER_WRONG,
        ),
        (
            _synthetic_semantic_stamp_actual(
                stamp,
                [stamp],
                stamp_owner = _NEGATIVE_AUDIT,
            ),
            _DIAGNOSTIC_STAMP_OWNER_WRONG,
        ),
        (
            _synthetic_semantic_stamp_actual(stamp, []),
            _DIAGNOSTIC_AUDIT_OUTPUT_MISSING,
        ),
        (
            _synthetic_semantic_stamp_actual(stamp, [stamp, extra]),
            _DIAGNOSTIC_AUDIT_OUTPUT_UNREQUESTED,
        ),
    )
    for actual, diagnostic_id in probes:
        if _semantic_stamp_contract_violation(actual, expected) != diagnostic_id:
            fail(
                "the semantic-stamp audit failed to detect [%s]" %
                diagnostic_id,
            )

def _negative_audit_impl(ctx):
    if not ctx.attr.cpp_configuration_baseline:
        fail("ADR-067 requires the exact canonical C++ configuration baseline")
    _expect_target_contracts(_semantic_configuration(ctx), *_audit_targets(ctx))
    _exercise_runfiles_classifiers(ctx)
    _exercise_linker_classifiers()
    _exercise_python_test_execution_classifiers()
    _exercise_semantic_stamp_classifiers(ctx)
    startup_audit = _run_startup_provenance_audit(ctx, "negative")
    stamp_info = _semantic_audit(
        ctx,
        "negative",
        extra_inputs = startup_audit.record_files,
        provider_inventories = [startup_audit.manifest],
        provider_proofs = [startup_audit.stamp],
    )
    return [
        stamp_info,
        DefaultInfo(files = depset([stamp_info.stamp])),
    ]

def _merge_attrs(primary, secondary):
    result = dict(primary)
    result.update(secondary)
    return result

def _audited_target_attr(label, reset = False):
    kwargs = {
        "aspects": [
            _apgar_link_closure_aspect,
            phase4_same_run_semantic_target_aspect,
        ],
        "default": label,
    }
    kwargs["cfg"] = (
        _reset_apgar_sanitizers if reset else _preserve_apgar_sanitizers
    )
    return attr.label(**kwargs)

_AUDIT_TARGET_ATTRS = {
    "cpp_configuration_baseline": attr.bool(mandatory = True),
    "_production": _audited_target_attr(_PRODUCTION, reset = True),
    "_test_support": _audited_target_attr(_TEST_SUPPORT),
    "_direct_test": _audited_target_attr(_DIRECT_TEST),
    "_runner": _audited_target_attr(_RUNNER, reset = True),
    "_forced_runner": _audited_target_attr(_FORCED_RUNNER, reset = True),
    "_llvm_readobj": attr.label(
        allow_single_file = True,
        cfg = "exec",
        default = _LLVM_READOBJ,
    ),
    "_startup_provenance_checker": attr.label(
        cfg = "exec",
        default = _STARTUP_PROVENANCE_CHECKER,
        executable = True,
    ),
    "_allowlist_function_transition": attr.label(
        default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
    ),
}

_positive_audit = rule(
    implementation = _positive_audit_impl,
    attrs = _merge_attrs(_AUDIT_TARGET_ATTRS, PHASE4_SAME_RUN_SEMANTIC_AUDIT_ATTRS),
)

_negative_audit = rule(
    implementation = _negative_audit_impl,
    attrs = _merge_attrs(
        _merge_attrs(_AUDIT_TARGET_ATTRS, PHASE4_SAME_RUN_SEMANTIC_AUDIT_ATTRS),
        {
            "foreign_alwayslink_initializer": attr.label(
                aspects = [_apgar_link_closure_aspect],
                providers = [CcInfo, _ApgarNegativeInitializerInfo],
            ),
        },
    ),
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _cpp_configuration_baseline_guard(name):
    guard = name + "_cpp_configuration_baseline"
    native.config_setting(
        name = guard,
        testonly = True,
        values = _CPP_CONFIGURATION_BASELINE_VALUES,
        visibility = ["//visibility:private"],
    )
    return select({
        ":" + guard: True,
        "//conditions:default": False,
    })

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract(name):
    _positive_audit(
        name = name,
        cpp_configuration_baseline = _cpp_configuration_baseline_guard(name),
        testonly = True,
        visibility = ["//visibility:private"],
    )

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract(name):
    fixture_name = name + "_foreign_alwayslink_initializer"
    _negative_initializer(
        name = fixture_name,
        alwayslink = True,
        testonly = True,
        visibility = ["//visibility:private"],
    )
    _negative_audit(
        name = name,
        foreign_alwayslink_initializer = ":" + fixture_name,
        cpp_configuration_baseline = _cpp_configuration_baseline_guard(name),
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _audited_process_artifact(ctx, target, owner, path, subject):
    executable = _expect_binary_provider(target[DefaultInfo], owner, path, subject)
    return [
        DefaultInfo(
            files = depset([executable]),
            runfiles = ctx.runfiles(files = [executable]),
        ),
    ]

def _runner_artifact_impl(ctx):
    return _audited_process_artifact(
        ctx,
        ctx.attr._runner[0],
        _RUNNER,
        _RUNNER_PATH,
        "the same-run producer-preflight runner",
    )

_runner_artifact = rule(
    implementation = _runner_artifact_impl,
    attrs = {
        "_runner": attr.label(cfg = _reset_apgar_sanitizers, default = _RUNNER),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_artifact(name):
    _runner_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _forced_runner_artifact_impl(ctx):
    return _audited_process_artifact(
        ctx,
        ctx.attr._runner[0],
        _FORCED_RUNNER,
        _FORCED_RUNNER_PATH,
        "the forced-unpublishable same-run producer-preflight runner",
    )

_forced_runner_artifact = rule(
    implementation = _forced_runner_artifact_impl,
    attrs = {
        "_runner": attr.label(cfg = _reset_apgar_sanitizers, default = _FORCED_RUNNER),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_audit_artifact(name):
    _forced_runner_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _configuration(ctx):
    asan = ctx.attr._audit_asan_setting[BuildSettingInfo].value
    ubsan = ctx.attr._audit_ubsan_setting[BuildSettingInfo].value
    if asan and ubsan:
        fail("ASan and UBSan cannot both be enabled for ADR-067 evidence")
    if asan:
        return "asan"
    if ubsan:
        return "ubsan"
    return "normal"

def _semantic_stamp_contract_violation(actual, expected):
    if actual.target_label != expected.owner or actual.stamp_owner != expected.owner:
        return _DIAGNOSTIC_STAMP_OWNER_WRONG
    if actual.stamp_basename != expected.basename or actual.info_basename != expected.basename:
        return "stamp-basename-wrong"
    if actual.kind != expected.kind or actual.invariant != expected.invariant:
        return "stamp-content-wrong"
    if actual.configuration != expected.configuration:
        return "stamp-configuration-wrong"
    if actual.stamp not in actual.files:
        return _DIAGNOSTIC_AUDIT_OUTPUT_MISSING
    if len(actual.files) != 1:
        return _DIAGNOSTIC_AUDIT_OUTPUT_UNREQUESTED
    return None

def _expect_semantic_stamp(target, owner, kind, basename, invariant, configuration):
    info = target[Phase4SameRunSemanticAuditStampInfo]
    violation = _semantic_stamp_contract_violation(
        struct(
            configuration = info.configuration,
            files = target[DefaultInfo].files.to_list(),
            info_basename = info.basename,
            invariant = info.invariant,
            kind = info.kind,
            stamp = info.stamp,
            stamp_basename = info.stamp.basename,
            stamp_owner = info.stamp.owner,
            target_label = target.label,
        ),
        struct(
            basename = basename,
            configuration = configuration,
            invariant = invariant,
            kind = kind,
            owner = owner,
        ),
    )
    if violation != None:
        fail(
            "semantic-audit stamp contract violation [%s] for %s" %
            (violation, target.label),
        )
    return info.stamp

def _process_artifacts_impl(ctx):
    configuration = _configuration(ctx)
    positive_stamp = _expect_semantic_stamp(
        ctx.attr.semantic_positive,
        _POSITIVE_AUDIT,
        "positive",
        _POSITIVE_BASENAME,
        _POSITIVE_INVARIANT,
        configuration,
    )
    negative_stamp = _expect_semantic_stamp(
        ctx.attr.semantic_negative,
        _NEGATIVE_AUDIT,
        "negative",
        _NEGATIVE_BASENAME,
        _NEGATIVE_INVARIANT,
        configuration,
    )
    runner = _expect_binary_provider(
        ctx.attr._runner[0][DefaultInfo],
        _RUNNER,
        _RUNNER_PATH,
        "the same-run producer-preflight runner",
    )
    forced_runner = _expect_binary_provider(
        ctx.attr._forced_runner[0][DefaultInfo],
        _FORCED_RUNNER,
        _FORCED_RUNNER_PATH,
        "the forced-unpublishable same-run producer-preflight runner",
    )
    files = [positive_stamp, negative_stamp, runner, forced_runner]
    return [
        DefaultInfo(
            files = depset(files),
            runfiles = ctx.runfiles(files = files),
        ),
    ]

_process_artifacts = rule(
    implementation = _process_artifacts_impl,
    attrs = {
        "semantic_positive": attr.label(
            default = _POSITIVE_AUDIT,
            providers = [Phase4SameRunSemanticAuditStampInfo],
        ),
        "semantic_negative": attr.label(
            default = _NEGATIVE_AUDIT,
            providers = [Phase4SameRunSemanticAuditStampInfo],
        ),
        "_runner": attr.label(cfg = _reset_apgar_sanitizers, default = _RUNNER),
        "_forced_runner": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _FORCED_RUNNER,
        ),
        "_audit_asan_setting": attr.label(default = "@llvm//config:asan"),
        "_audit_ubsan_setting": attr.label(default = "@llvm//config:ubsan"),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_process_artifacts(name):
    _process_artifacts(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_startup_provenance_checker(name):
    py_binary(
        name = name,
        testonly = True,
        srcs = [
            "tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test.py",
        ],
        legacy_create_init = 0,
        main = "tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test.py",
        python_version = "3.13",
    )

def phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_process_test(name):
    py_test(
        name = name,
        size = "small",
        srcs = [
            "tests/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_process_test.py",
        ],
        args = [],
        data = [
            ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_process_artifacts",
        ],
        env = {},
        env_inherit = [],
        flaky = False,
        legacy_create_init = 0,
        local = False,
        main = "tests/benchmark/phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_process_test.py",
        python_version = "3.13",
        tags = [],
        testonly = True,
    )

def phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test(name):
    py_test(
        name = name,
        size = "small",
        srcs = [
            "tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test.py",
        ],
        args = select({
            ":asan_enabled": [
                "asan",
                "$(rootpath @llvm//tools:llvm-nm)",
                "$(rootpath @llvm//tools:llvm-readobj)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact)",
                "$(rootpath :phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact)",
            ],
            ":ubsan_enabled": [
                "ubsan",
                "$(rootpath @llvm//tools:llvm-nm)",
                "$(rootpath @llvm//tools:llvm-readobj)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact)",
                "$(rootpath :phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact)",
            ],
            "//conditions:default": [
                "normal",
                "$(rootpath @llvm//tools:llvm-nm)",
                "$(rootpath @llvm//tools:llvm-readobj)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract)",
                "$(rootpath :phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test)",
                "$(rootpath :phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner)",
                "$(rootpath :phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner)",
            ],
        }),
        data = [
            ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_contract",
            ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_audit_negative_contract",
            ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_process_artifacts",
            ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_test",
            "@llvm//tools:llvm-nm",
            "@llvm//tools:llvm-readobj",
        ] + select({
            ":asan_enabled": [
                ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact",
                ":phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact",
            ],
            ":ubsan_enabled": [
                ":phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_unsanitized_artifact",
                ":phase4_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_unsanitized_artifact",
            ],
            "//conditions:default": [
                ":phase4_confirmatory_h4096_session_v5_same_run_per_net_report_producer_preflight_test_runner",
                ":phase4_confirmatory_h4096_session_v5_same_run_unpublishable_source_per_net_report_producer_preflight_test_runner",
            ],
        }),
        env = {},
        env_inherit = [],
        flaky = False,
        legacy_create_init = 0,
        local = False,
        main = "tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_link_surface_test.py",
        python_version = "3.13",
        tags = [],
        testonly = True,
    )
