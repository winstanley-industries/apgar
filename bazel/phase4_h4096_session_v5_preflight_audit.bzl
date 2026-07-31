"""Exposes the fixtureless Session-v5 preflight runner to sanitizer link audits."""

_PREFLIGHT = Label("//:phase4_confirmatory_h4096_session_v5_preflight_test_runner")
_PREFLIGHT_PATH = "phase4_confirmatory_h4096_session_v5_preflight_test_runner"
_UNPUBLISHABLE_PREFLIGHT = Label(
    "//:phase4_confirmatory_h4096_session_v5_unpublishable_source_preflight_test_runner",
)
_UNPUBLISHABLE_PREFLIGHT_PATH = (
    "phase4_confirmatory_h4096_session_v5_unpublishable_source_preflight_test_runner"
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

def _phase4_h4096_session_v5_preflight_audit_artifact_impl(ctx):
    preflight = ctx.attr._preflight[0][DefaultInfo]
    files = preflight.files.to_list()
    if len(files) != 1:
        fail("the Session-v5 preflight audit requires exactly one artifact")
    artifact = files[0]
    if artifact.short_path != _PREFLIGHT_PATH:
        fail("the Session-v5 preflight audit received an unexpected artifact path")
    if artifact.owner != _PREFLIGHT:
        fail("the Session-v5 preflight audit received an unexpected artifact owner")
    return [
        DefaultInfo(
            files = depset([artifact]),
            runfiles = ctx.runfiles(files = [artifact]),
        ),
    ]

_phase4_h4096_session_v5_preflight_audit_artifact = rule(
    implementation = _phase4_h4096_session_v5_preflight_audit_artifact_impl,
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

def phase4_h4096_session_v5_preflight_audit_artifact(name):
    _phase4_h4096_session_v5_preflight_audit_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_unpublishable_preflight_audit_artifact_impl(ctx):
    preflight = ctx.attr._preflight[0][DefaultInfo]
    files = preflight.files.to_list()
    if len(files) != 1:
        fail("the unpublishable Session-v5 preflight audit requires exactly one artifact")
    artifact = files[0]
    if artifact.short_path != _UNPUBLISHABLE_PREFLIGHT_PATH:
        fail("the unpublishable Session-v5 preflight audit received an unexpected artifact path")
    if artifact.owner != _UNPUBLISHABLE_PREFLIGHT:
        fail("the unpublishable Session-v5 preflight audit received an unexpected artifact owner")
    return [
        DefaultInfo(
            files = depset([artifact]),
            runfiles = ctx.runfiles(files = [artifact]),
        ),
    ]

_phase4_h4096_session_v5_unpublishable_preflight_audit_artifact = rule(
    implementation = _phase4_h4096_session_v5_unpublishable_preflight_audit_artifact_impl,
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

def phase4_h4096_session_v5_unpublishable_preflight_audit_artifact(name):
    _phase4_h4096_session_v5_unpublishable_preflight_audit_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_preflight_audit_contract_impl(ctx):
    info = ctx.attr._artifact[DefaultInfo]
    files = info.files.to_list()
    if len(files) != 1:
        fail("the Session-v5 preflight audit contract requires exactly one file")
    artifact = files[0]
    if artifact.short_path != _PREFLIGHT_PATH:
        fail("the Session-v5 preflight audit contract observed path substitution")
    if artifact.owner != _PREFLIGHT:
        fail("the Session-v5 preflight audit contract observed owner substitution")
    for kind, runfiles in (
        ("default", info.default_runfiles.files.to_list()),
        ("data", info.data_runfiles.files.to_list()),
    ):
        extras = [file for file in runfiles if file != artifact]
        if extras:
            fail(
                "the Session-v5 preflight audit forwarded unexpected %s runfiles" % kind,
            )
    if artifact not in info.default_runfiles.files.to_list():
        fail("the Session-v5 preflight audit omitted its exact artifact from runfiles")
    return [DefaultInfo()]

_phase4_h4096_session_v5_preflight_audit_contract = rule(
    implementation = _phase4_h4096_session_v5_preflight_audit_contract_impl,
    attrs = {
        "_artifact": attr.label(
            default = "//:phase4_h4096_session_v5_preflight_unsanitized_artifact",
        ),
    },
)

def phase4_h4096_session_v5_preflight_audit_contract(name):
    _phase4_h4096_session_v5_preflight_audit_contract(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_unpublishable_preflight_audit_contract_impl(ctx):
    info = ctx.attr._artifact[DefaultInfo]
    files = info.files.to_list()
    if len(files) != 1:
        fail("the unpublishable Session-v5 preflight audit contract requires exactly one file")
    artifact = files[0]
    if artifact.short_path != _UNPUBLISHABLE_PREFLIGHT_PATH:
        fail("the unpublishable Session-v5 preflight audit contract observed path substitution")
    if artifact.owner != _UNPUBLISHABLE_PREFLIGHT:
        fail("the unpublishable Session-v5 preflight audit contract observed owner substitution")
    for kind, runfiles in (
        ("default", info.default_runfiles.files.to_list()),
        ("data", info.data_runfiles.files.to_list()),
    ):
        extras = [file for file in runfiles if file != artifact]
        if extras:
            fail(
                "the unpublishable Session-v5 preflight audit forwarded unexpected %s runfiles" %
                kind,
            )
    if artifact not in info.default_runfiles.files.to_list():
        fail("the unpublishable Session-v5 preflight audit omitted its exact artifact from runfiles")
    return [DefaultInfo()]

_phase4_h4096_session_v5_unpublishable_preflight_audit_contract = rule(
    implementation = _phase4_h4096_session_v5_unpublishable_preflight_audit_contract_impl,
    attrs = {
        "_artifact": attr.label(
            default = "//:phase4_h4096_session_v5_unpublishable_preflight_unsanitized_artifact",
        ),
    },
)

def phase4_h4096_session_v5_unpublishable_preflight_audit_contract(name):
    _phase4_h4096_session_v5_unpublishable_preflight_audit_contract(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_preflight_process_artifacts_impl(ctx):
    expected = (
        (
            ctx.attr._preflight[0][DefaultInfo],
            _PREFLIGHT,
            _PREFLIGHT_PATH,
        ),
        (
            ctx.attr._unpublishable_preflight[0][DefaultInfo],
            _UNPUBLISHABLE_PREFLIGHT,
            _UNPUBLISHABLE_PREFLIGHT_PATH,
        ),
    )
    artifacts = []
    for info, owner, path in expected:
        files = info.files.to_list()
        if len(files) != 1:
            fail("the Session-v5 process firewall requires one artifact per runner")
        artifact = files[0]
        if artifact.short_path != path:
            fail("the Session-v5 process firewall observed runner path substitution")
        if artifact.owner != owner:
            fail("the Session-v5 process firewall observed runner owner substitution")
        artifacts.append(artifact)
    return [
        DefaultInfo(
            files = depset(artifacts),
            runfiles = ctx.runfiles(files = artifacts),
        ),
    ]

_phase4_h4096_session_v5_preflight_process_artifacts = rule(
    implementation = _phase4_h4096_session_v5_preflight_process_artifacts_impl,
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

def phase4_h4096_session_v5_preflight_process_artifacts(name):
    _phase4_h4096_session_v5_preflight_process_artifacts(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )
