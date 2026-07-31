"""Audits the Session-v5 H4096 per-net validator runfiles closure."""

_VALIDATOR = Label(
    "//:phase4_confirmatory_h4096_session_v5_per_net_report_validator",
)
_VALIDATOR_PATH = (
    "phase4_confirmatory_h4096_session_v5_per_net_report_validator"
)
_PYTHON_RUNTIME_PREFIX = (
    "../rules_python++python+python_3_13_x86_64-unknown-linux-gnu/"
)

_ALLOWED_APGAR_RUNFILES = (
    "schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v3.json",
    "schemas/benchmark/phase4_confirmatory_canonical_algorithm_budget_roster_v4.json",
    "schemas/benchmark/phase4_confirmatory_decision_protocol_v1.json",
    "schemas/benchmark/phase4_confirmatory_decision_protocol_v2.json",
    "schemas/benchmark/phase4_confirmatory_decision_protocol_v3.json",
    "schemas/benchmark/phase4_representative_manifest_v1.json",
    "schemas/benchmark/phase4_representative_manifest_v2.json",
    "schemas/benchmark/phase4_statistical_decision_protocol_v1.json",
    "schemas/benchmark/phase4_statistical_decision_protocol_v2.json",
    "schemas/benchmark/phase4_statistical_decision_protocol_v3.json",
    "schemas/benchmark/phase4_statistical_decision_protocol_v4.json",
    "schemas/benchmark/phase4_workload_net_roster_manifest_v1.json",
    "schemas/benchmark/phase4_workload_net_roster_manifest_v2.json",
    "tools/phase4_bounded_json_input.py",
    "tools/validate_phase4_confirmatory_canonical_budget_roster_v3.py",
    "tools/validate_phase4_confirmatory_canonical_budget_roster_v4.py",
    "tools/validate_phase4_confirmatory_decision_protocol.py",
    "tools/validate_phase4_confirmatory_decision_protocol_v2.py",
    "tools/validate_phase4_confirmatory_decision_protocol_v3.py",
    "tools/validate_phase4_confirmatory_h4096_session_v5_per_net_report.py",
    "tools/validate_phase4_confirmatory_h4096_session_v5_raw_evidence.py",
    "tools/validate_phase4_per_net_report.py",
    "tools/validate_phase4_raw_evidence.py",
    "tools/validate_phase4_representative_manifest_v2.py",
    "tools/validate_phase4_statistical_protocol.py",
    "tools/validate_phase4_statistical_protocol_v2.py",
    "tools/validate_phase4_statistical_protocol_v3.py",
    "tools/validate_phase4_statistical_protocol_v4.py",
    "tools/validate_phase4_workload_net_roster_manifest.py",
)

def _is_generated_validator_runtime(file):
    path = file.short_path
    if path == _VALIDATOR_PATH:
        return True
    if path == _VALIDATOR_PATH + ".build_data.txt":
        return True
    return (
        path.startswith("_" + _VALIDATOR_PATH + "_") or
        path.startswith("_" + _VALIDATOR_PATH + ".venv/")
    )

def _audit_runfile(file, kind):
    path = file.short_path

    if path.startswith(_PYTHON_RUNTIME_PREFIX):
        return

    if _is_generated_validator_runtime(file):
        if file.owner != _VALIDATOR:
            fail(
                "the Session-v5 per-net validator audit observed substituted " +
                "%s runtime owner for %s" % (kind, path),
            )
        return

    if path in _ALLOWED_APGAR_RUNFILES:
        return

    fail(
        "the Session-v5 per-net validator %s runfiles contain an unexpected " +
        "path: %s; only the exact pure-validation closure, fixed authority " +
        "data, and checksum-pinned Python runtime are allowed" % (kind, path),
    )

def _runfiles_link_kind(runfiles):
    if runfiles.symlinks.to_list():
        return "workspace symlink"
    if runfiles.root_symlinks.to_list():
        return "root symlink"
    if runfiles.empty_filenames.to_list():
        return "empty filename"
    return None

def _audit_runfiles(runfiles, kind):
    link_kind = _runfiles_link_kind(runfiles)
    if link_kind != None:
        fail(
            "the Session-v5 per-net validator %s runfiles contain a " +
            "forbidden %s entry" % (kind, link_kind),
        )
    for file in runfiles.files.to_list():
        _audit_runfile(file, kind)

def _phase4_h4096_session_v5_per_net_report_validator_audit_impl(ctx):
    info = ctx.attr._validator[DefaultInfo]
    files = info.files.to_list()

    executable = info.files_to_run.executable
    if executable == None or executable not in files:
        fail("the Session-v5 per-net validator audit requires one exact executable")
    if executable.short_path != _VALIDATOR_PATH:
        fail("the Session-v5 per-net validator audit observed path substitution")
    if executable.owner != _VALIDATOR:
        fail("the Session-v5 per-net validator audit observed owner substitution")

    for file in files:
        _audit_runfile(file, "declared output")

    default_runfiles = info.default_runfiles.files.to_list()
    if executable not in default_runfiles:
        fail("the Session-v5 per-net validator audit omitted its executable from runfiles")

    _audit_runfiles(info.default_runfiles, "default")
    _audit_runfiles(info.data_runfiles, "data")

    return [DefaultInfo()]

_phase4_h4096_session_v5_per_net_report_validator_audit = rule(
    implementation = _phase4_h4096_session_v5_per_net_report_validator_audit_impl,
    attrs = {
        "_validator": attr.label(default = _VALIDATOR),
    },
)

def phase4_h4096_session_v5_per_net_report_validator_audit(name):
    _phase4_h4096_session_v5_per_net_report_validator_audit(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )

def _phase4_h4096_session_v5_per_net_report_validator_audit_negative_contract_impl(ctx):
    sentinel = ctx.actions.declare_file(ctx.label.name + ".sentinel")
    ctx.actions.write(sentinel, "analysis-only runfiles audit sentinel\n")
    workspace_symlink = ctx.runfiles(
        symlinks = {"forbidden/evidence_runner": sentinel},
    )
    root_symlink = ctx.runfiles(
        root_symlinks = {"forbidden/evidence_runner": sentinel},
    )
    if _runfiles_link_kind(workspace_symlink) != "workspace symlink":
        fail("the runfiles audit failed to detect a synthetic workspace symlink")
    if _runfiles_link_kind(root_symlink) != "root symlink":
        fail("the runfiles audit failed to detect a synthetic root symlink")
    return [DefaultInfo(files = depset([sentinel]))]

_phase4_h4096_session_v5_per_net_report_validator_audit_negative_contract = rule(
    implementation = (
        _phase4_h4096_session_v5_per_net_report_validator_audit_negative_contract_impl
    ),
)

def phase4_h4096_session_v5_per_net_report_validator_audit_negative_contract(name):
    _phase4_h4096_session_v5_per_net_report_validator_audit_negative_contract(
        name = name,
        testonly = True,
        visibility = ["//visibility:private"],
    )
