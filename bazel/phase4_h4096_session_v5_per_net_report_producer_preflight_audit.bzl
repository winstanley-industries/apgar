"""Audits the Session-v5 ordinary report-producer preflight test boundary."""

load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

_DIRECT_TEST = Label(
    "//:phase4_h4096_session_v5_per_net_report_producer_preflight_test",
)
_DIRECT_TEST_PATH = (
    "phase4_h4096_session_v5_per_net_report_producer_preflight_test"
)
_TEST_SUPPORT = Label(
    "//:phase4_h4096_session_v5_per_net_report_producer_preflight_test_support",
)
_EXECUTION_TEST_SUPPORT = Label(
    "//:phase4_h4096_session_v5_execution_preflight_test_support",
)
_PREFLIGHT = Label(
    "//:phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner",
)
_PREFLIGHT_PATH = (
    "phase4_confirmatory_h4096_session_v5_per_net_report_producer_preflight_test_runner"
)
_UNPUBLISHABLE_PREFLIGHT = Label(
    "//:phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner",
)
_UNPUBLISHABLE_PREFLIGHT_PATH = (
    "phase4_confirmatory_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_test_runner"
)
_GTEST_MAIN = Label("@googletest//:gtest_main")
_RULES_CC_LINK_EXTRA_LIB = Label("@rules_cc//:link_extra_lib")

# This is the exact APGAR rule closure already exposed by
# phase4_h4096_session_v5_execution_preflight_test_support, plus the new
# producer test and its one test-support seam. Source files and external
# GoogleTest/toolchain nodes are deliberately not APGAR code-dependency labels.
_ALLOWED_DIRECT_TEST_APGAR_LABELS = (
    _DIRECT_TEST,
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

_ApgarDependencyClosureInfo = provider(
    "The C++ dependency labels visible through the APGAR audit aspect.",
    fields = {
        "apgar_labels": "transitive APGAR C++ rule labels",
        "direct_labels": "all direct labels in the deps attribute",
        "direct_apgar_labels": "direct APGAR C++ rule labels",
    },
)

def _is_apgar_label(label):
    return label.workspace_name == ""

def _targets(value):
    if value == None:
        return []
    if type(value) == "list":
        return value
    return [value]

def _dependency_values(ctx, attribute_names):
    values = []
    for name in attribute_names:
        if hasattr(ctx.rule.attr, name):
            values.extend(_targets(getattr(ctx.rule.attr, name)))
    return values

def _apgar_dependency_closure_aspect_impl(target, ctx):
    direct_dependencies = _dependency_values(ctx, ("deps",))
    traversed_dependencies = _dependency_values(
        ctx,
        (
            "deps",
            "implementation_deps",
            "dynamic_deps",
            "malloc",
        ),
    )

    transitive = []
    for dependency in traversed_dependencies:
        if _ApgarDependencyClosureInfo in dependency:
            transitive.append(
                dependency[_ApgarDependencyClosureInfo].apgar_labels,
            )

    direct_apgar_labels = [
        dependency.label
        for dependency in direct_dependencies
        if _is_apgar_label(dependency.label) and CcInfo in dependency
    ]
    own_labels = []
    if _is_apgar_label(target.label) and CcInfo in target:
        own_labels.append(target.label)

    return [
        _ApgarDependencyClosureInfo(
            apgar_labels = depset(
                direct = own_labels + direct_apgar_labels,
                transitive = transitive,
            ),
            direct_labels = depset(
                direct = [dependency.label for dependency in direct_dependencies],
            ),
            direct_apgar_labels = depset(direct = direct_apgar_labels),
        ),
    ]

_apgar_dependency_closure_aspect = aspect(
    implementation = _apgar_dependency_closure_aspect_impl,
    attr_aspects = [
        "deps",
        "implementation_deps",
        "dynamic_deps",
        "malloc",
    ],
)

def _reset_apgar_sanitizers_impl(_settings, _attr):
    return {
        "@llvm//config:asan": False,
        "@llvm//config:ubsan": False,
    }

_reset_apgar_sanitizers = transition(
    implementation = _reset_apgar_sanitizers_impl,
    inputs = [],
    outputs = [
        "@llvm//config:asan",
        "@llvm//config:ubsan",
    ],
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
    files = info.files.to_list()
    executable = info.files_to_run.executable
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

    missing = [label for label in expected_strings if label not in actual_strings]
    unexpected = [label for label in actual_strings if label not in expected_strings]
    fail(
        "%s differs from its frozen APGAR dependency allowlist; missing=%s unexpected=%s" %
        (subject, missing, unexpected),
    )

def _expect_direct_test_dependency_closure(direct_test, test_support):
    direct_info = direct_test[_ApgarDependencyClosureInfo]
    support_info = test_support[_ApgarDependencyClosureInfo]

    _expect_exact_labels(
        direct_info.apgar_labels.to_list(),
        _ALLOWED_DIRECT_TEST_APGAR_LABELS,
        "the report-producer preflight direct test",
    )
    _expect_exact_labels(
        direct_info.direct_apgar_labels.to_list(),
        (_TEST_SUPPORT,),
        "the report-producer preflight direct test direct APGAR dependencies",
    )
    _expect_exact_labels(
        support_info.direct_apgar_labels.to_list(),
        (_EXECUTION_TEST_SUPPORT,),
        "the report-producer preflight test-support direct APGAR dependencies",
    )

    direct_labels = direct_info.direct_labels.to_list()
    if _GTEST_MAIN not in direct_labels:
        fail("the report-producer preflight direct test must depend on GoogleTest main")
    unexpected_external = [
        str(label)
        for label in direct_labels
        if not _is_apgar_label(label) and
           label not in (_GTEST_MAIN, _RULES_CC_LINK_EXTRA_LIB)
    ]
    if unexpected_external:
        fail(
            "the report-producer preflight direct test has unexpected external " +
            "dependencies: %s" % sorted(unexpected_external),
        )

def _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_contract_impl(ctx):
    direct_test = ctx.attr._direct_test
    test_support = ctx.attr._test_support
    preflight = ctx.attr._preflight[0]
    unpublishable_preflight = ctx.attr._unpublishable_preflight[0]

    _expect_binary_provider(
        direct_test[DefaultInfo],
        _DIRECT_TEST,
        _DIRECT_TEST_PATH,
        "the report-producer preflight direct test",
    )
    _expect_binary_provider(
        preflight[DefaultInfo],
        _PREFLIGHT,
        _PREFLIGHT_PATH,
        "the report-producer preflight runner",
    )
    _expect_binary_provider(
        unpublishable_preflight[DefaultInfo],
        _UNPUBLISHABLE_PREFLIGHT,
        _UNPUBLISHABLE_PREFLIGHT_PATH,
        "the unpublishable report-producer preflight runner",
    )
    _expect_direct_test_dependency_closure(direct_test, test_support)
    return [DefaultInfo()]

_phase4_h4096_session_v5_per_net_report_producer_preflight_audit_contract = rule(
    implementation = (
        _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_contract_impl
    ),
    attrs = {
        "_direct_test": attr.label(
            aspects = [_apgar_dependency_closure_aspect],
            default = _DIRECT_TEST,
        ),
        "_test_support": attr.label(
            aspects = [_apgar_dependency_closure_aspect],
            default = _TEST_SUPPORT,
        ),
        "_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _PREFLIGHT,
        ),
        "_unpublishable_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _UNPUBLISHABLE_PREFLIGHT,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_per_net_report_producer_preflight_audit_contract(name):
    _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_contract(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_negative_contract_impl(
        ctx):
    executable = ctx.actions.declare_file(ctx.label.name + ".executable")
    extra = ctx.actions.declare_file(ctx.label.name + ".extra")
    ctx.actions.write(executable, "analysis-only expected executable\n")
    ctx.actions.write(extra, "analysis-only forbidden runfiles entry\n")

    unexpected_file = ctx.runfiles(files = [executable, extra])
    if _runfiles_violation(unexpected_file, executable) != "unexpected file":
        fail("the runfiles audit failed to detect a synthetic unexpected file")

    workspace_symlink = ctx.runfiles(
        files = [executable],
        symlinks = {"forbidden/runner": extra},
    )
    if _runfiles_violation(workspace_symlink, executable) != "workspace symlink":
        fail("the runfiles audit failed to detect a synthetic workspace symlink")

    root_symlink = ctx.runfiles(
        files = [executable],
        root_symlinks = {"forbidden/runner": extra},
    )
    if _runfiles_violation(root_symlink, executable) != "root symlink":
        fail("the runfiles audit failed to detect a synthetic root symlink")

    empty_filename = struct(
        files = depset([executable]),
        symlinks = depset(),
        root_symlinks = depset(),
        empty_filenames = depset(["forbidden/empty"]),
    )
    if _runfiles_violation(empty_filename, executable) != "empty filename":
        fail("the runfiles audit failed to detect a synthetic empty filename")

    missing_executable = ctx.runfiles(files = [extra])
    if _runfiles_violation(missing_executable, executable) != "missing executable":
        fail("the runfiles audit failed to detect a missing executable")

    return [DefaultInfo(files = depset([executable, extra]))]

_phase4_h4096_session_v5_per_net_report_producer_preflight_audit_negative_contract = rule(
    implementation = (
        _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_negative_contract_impl
    ),
)

def phase4_h4096_session_v5_per_net_report_producer_preflight_audit_negative_contract(name):
    _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_negative_contract(
        name = name,
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

def _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_artifact_impl(ctx):
    return _audited_process_artifact(
        ctx,
        ctx.attr._preflight[0],
        _PREFLIGHT,
        _PREFLIGHT_PATH,
        "the report-producer preflight runner",
    )

_phase4_h4096_session_v5_per_net_report_producer_preflight_audit_artifact = rule(
    implementation = (
        _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_artifact_impl
    ),
    attrs = {
        "_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _PREFLIGHT,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_per_net_report_producer_preflight_audit_artifact(name):
    _phase4_h4096_session_v5_per_net_report_producer_preflight_audit_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_audit_artifact_impl(
        ctx):
    return _audited_process_artifact(
        ctx,
        ctx.attr._preflight[0],
        _UNPUBLISHABLE_PREFLIGHT,
        _UNPUBLISHABLE_PREFLIGHT_PATH,
        "the unpublishable report-producer preflight runner",
    )

_phase4_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_audit_artifact = rule(
    implementation = (
        _phase4_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_audit_artifact_impl
    ),
    attrs = {
        "_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _UNPUBLISHABLE_PREFLIGHT,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_audit_artifact(
        name):
    _phase4_h4096_session_v5_unpublishable_source_per_net_report_producer_preflight_audit_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_per_net_report_producer_preflight_process_artifacts_impl(ctx):
    expected = (
        (
            ctx.attr._preflight[0],
            _PREFLIGHT,
            _PREFLIGHT_PATH,
            "the report-producer preflight runner",
        ),
        (
            ctx.attr._unpublishable_preflight[0],
            _UNPUBLISHABLE_PREFLIGHT,
            _UNPUBLISHABLE_PREFLIGHT_PATH,
            "the unpublishable report-producer preflight runner",
        ),
    )
    executables = []
    for target, owner, path, subject in expected:
        executables.append(
            _expect_binary_provider(target[DefaultInfo], owner, path, subject),
        )
    return [
        DefaultInfo(
            files = depset(executables),
            runfiles = ctx.runfiles(files = executables),
        ),
    ]

_phase4_h4096_session_v5_per_net_report_producer_preflight_process_artifacts = rule(
    implementation = (
        _phase4_h4096_session_v5_per_net_report_producer_preflight_process_artifacts_impl
    ),
    attrs = {
        "_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _PREFLIGHT,
        ),
        "_unpublishable_preflight": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _UNPUBLISHABLE_PREFLIGHT,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_per_net_report_producer_preflight_process_artifacts(name):
    _phase4_h4096_session_v5_per_net_report_producer_preflight_process_artifacts(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )
