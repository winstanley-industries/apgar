"""Exposes the ordinary Session-v5 H4096 generator to sanitizer link audits."""

_GENERATOR = Label("//:phase4_v2_h4096_session_v5_canonical_budget_roster")
_GENERATOR_PATH = "phase4_v2_h4096_session_v5_canonical_budget_roster"

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

def _phase4_h4096_session_v5_generator_audit_artifact_impl(ctx):
    # A Starlark-transitioned label is presented as a single-element list.
    generator = ctx.attr._generator[0][DefaultInfo]
    files = generator.files.to_list()
    if len(files) != 1:
        fail("the Session-v5 H4096 generator audit requires exactly one artifact")
    artifact = files[0]
    if artifact.short_path != _GENERATOR_PATH:
        fail("the Session-v5 H4096 generator audit received an unexpected artifact path")
    if artifact.owner != _GENERATOR:
        fail("the Session-v5 H4096 generator audit received an unexpected artifact owner")
    return [
        DefaultInfo(
            files = depset([artifact]),
            runfiles = ctx.runfiles(files = [artifact]),
        ),
    ]

_phase4_h4096_session_v5_generator_audit_artifact = rule(
    implementation = _phase4_h4096_session_v5_generator_audit_artifact_impl,
    attrs = {
        "_generator": attr.label(
            cfg = _reset_apgar_sanitizers,
            default = _GENERATOR,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def phase4_h4096_session_v5_generator_audit_artifact(name):
    _phase4_h4096_session_v5_generator_audit_artifact(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_generator_audit_contract_impl(ctx):
    info = ctx.attr._artifact[DefaultInfo]
    files = info.files.to_list()
    if len(files) != 1:
        fail("the Session-v5 H4096 generator audit contract requires exactly one file")
    artifact = files[0]
    if artifact.short_path != _GENERATOR_PATH:
        fail("the Session-v5 H4096 generator audit contract observed path substitution")
    if artifact.owner != _GENERATOR:
        fail("the Session-v5 H4096 generator audit contract observed owner substitution")
    for kind, runfiles in (
        ("default", info.default_runfiles.files.to_list()),
        ("data", info.data_runfiles.files.to_list()),
    ):
        extras = [file for file in runfiles if file != artifact]
        if extras:
            fail(
                "the Session-v5 H4096 generator audit forwarded unexpected %s runfiles" % kind,
            )
    if artifact not in info.default_runfiles.files.to_list():
        fail("the Session-v5 H4096 generator audit omitted its exact artifact from runfiles")
    return [DefaultInfo()]

_phase4_h4096_session_v5_generator_audit_contract = rule(
    implementation = _phase4_h4096_session_v5_generator_audit_contract_impl,
    attrs = {
        "_artifact": attr.label(
            default = "//:phase4_v2_h4096_session_v5_canonical_budget_roster_unsanitized_artifact",
        ),
    },
)

def phase4_h4096_session_v5_generator_audit_contract(name):
    _phase4_h4096_session_v5_generator_audit_contract(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )
