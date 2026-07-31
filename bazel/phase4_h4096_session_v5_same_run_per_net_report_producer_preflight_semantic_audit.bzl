"""Hermetic structured semantic audit support for ADR-067.

The aspect serializes the source, compile, include, link, and execution
surface of each audited C++ target.  The caller-owned audit action consumes
those inventories with the repository checker and the selected LLVM
toolchain, and is the only action allowed to create the contract stamp.
"""

load("@bazel_skylib//rules:common_settings.bzl", "BuildSettingInfo")
load("@rules_cc//cc:action_names.bzl", "CPP_COMPILE_ACTION_NAME")
load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

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
_STALE_INPUT_DIAGNOSTIC = "stamp-stale-input"

Phase4SameRunSemanticAuditStampInfo = provider(
    doc = "ADR-067 target-owned semantic-audit stamp metadata.",
    fields = {
        "basename": "the frozen stamp basename",
        "configuration": "normal, asan, or ubsan",
        "invariant": "the frozen first stamp line",
        "kind": "positive or negative",
        "stamp": "the target-owned stamp File",
    },
)

Phase4SameRunSemanticTargetInfo = provider(
    doc = "Canonical structured inventory for one audited C++ target.",
    fields = {
        "configuration": "normal, asan, or ubsan",
        "inputs": "all files represented by the inventory",
        "inventory": "canonical JSON inventory File",
        "label": "the audited target Label",
        "tool_files": "resolved C++ toolchain files needed by the checker",
    },
)

def _configuration(asan, ubsan, subject):
    if asan and ubsan:
        fail("%s cannot enable ASan and UBSan together" % subject)
    if asan:
        return "asan"
    if ubsan:
        return "ubsan"
    return "normal"

def _setting_value(target):
    if BuildSettingInfo not in target:
        fail("semantic audit sanitizer setting lacks BuildSettingInfo: %s" % target.label)
    return target[BuildSettingInfo].value

def _target_configuration(ctx):
    return _configuration(
        _setting_value(ctx.attr._semantic_audit_asan_setting),
        _setting_value(ctx.attr._semantic_audit_ubsan_setting),
        str(ctx.label),
    )

def _file_paths(files):
    return sorted([file.path for file in files])

def _short_paths(files):
    return sorted([file.short_path for file in files])

def _labels(values):
    result = []
    for value in values:
        if hasattr(value, "label"):
            result.append(str(value.label))
        else:
            result.append(str(value))
    return sorted(result)

def phase4_same_run_semantic_action_input_inventory_violation(actual, expected):
    """Returns the ADR-067 diagnostic for any missing/extra/stale action input."""
    if sorted(actual) != sorted(expected):
        return _STALE_INPUT_DIAGNOSTIC
    return None

def _attr_values(ctx, name):
    if not hasattr(ctx.rule.attr, name):
        return []
    value = getattr(ctx.rule.attr, name)
    if value == None:
        return []
    if type(value) == "list":
        return value
    return [value]

def _string_attr(ctx, name, default = ""):
    if not hasattr(ctx.rule.attr, name):
        return default
    value = getattr(ctx.rule.attr, name)
    if value == None:
        return default
    return str(value)

def _bool_attr(ctx, name, default = False):
    if not hasattr(ctx.rule.attr, name):
        return default
    return getattr(ctx.rule.attr, name)

def _int_attr(ctx, name, default = 0):
    if not hasattr(ctx.rule.attr, name):
        return default
    return getattr(ctx.rule.attr, name)

def _string_list_attr(ctx, name):
    return [str(value) for value in _attr_values(ctx, name)]

def _files_attr(ctx, name):
    if not hasattr(ctx.rule.files, name):
        return []
    return getattr(ctx.rule.files, name)

def _is_translation_unit(file):
    return (
        file.basename.endswith(".c") or
        file.basename.endswith(".cc") or
        file.basename.endswith(".cpp") or
        file.basename.endswith(".cxx") or
        file.basename.endswith(".C")
    )

def _feature_lists(ctx):
    requested = []
    unsupported = []
    for feature in _string_list_attr(ctx, "features"):
        if feature.startswith("-"):
            unsupported.append(feature[1:])
        else:
            requested.append(feature)
    for feature in ctx.features:
        if feature not in requested:
            requested.append(feature)
    for feature in ctx.disabled_features:
        if feature not in unsupported:
            unsupported.append(feature)
    return (requested, unsupported)

def _link_inventory(target):
    if CcInfo not in target:
        return []
    result = []
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        libraries = []
        for library in linker_input.libraries:
            entry = {}
            for field in (
                "static_library",
                "pic_static_library",
                "dynamic_library",
                "interface_library",
            ):
                value = getattr(library, field)
                if value != None:
                    entry[field] = value.path
            entry["alwayslink"] = library.alwayslink
            libraries.append(entry)
        result.append({
            "additional_inputs": _file_paths(linker_input.additional_inputs),
            "libraries": libraries,
            "owner": str(linker_input.owner),
            "user_link_flags": [str(flag) for flag in linker_input.user_link_flags],
        })
    return result

def _link_files(target):
    if CcInfo not in target:
        return []
    result = []
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        result.extend(linker_input.additional_inputs)
        for library in linker_input.libraries:
            for field in (
                "static_library",
                "pic_static_library",
                "dynamic_library",
                "interface_library",
            ):
                value = getattr(library, field)
                if value != None:
                    result.append(value)
    return result

def _phase4_same_run_semantic_target_aspect_impl(target, ctx):
    if CcInfo not in target:
        fail("ADR-067 semantic audit requires CcInfo from %s" % target.label)

    configuration = _target_configuration(ctx)
    cc_toolchain = find_cc_toolchain(ctx)
    requested_features, unsupported_features = _feature_lists(ctx)
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = requested_features,
        unsupported_features = unsupported_features,
    )
    compiler = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = CPP_COMPILE_ACTION_NAME,
    )

    compilation_context = target[CcInfo].compilation_context
    sources = _files_attr(ctx, "srcs")
    public_headers = _files_attr(ctx, "hdrs")
    textual_headers = _files_attr(ctx, "textual_hdrs")
    additional_compiler_inputs = _files_attr(ctx, "additional_compiler_inputs")
    additional_linker_inputs = _files_attr(ctx, "additional_linker_inputs")

    user_compile_flags = (
        ctx.fragments.cpp.copts +
        ctx.fragments.cpp.cxxopts +
        _string_list_attr(ctx, "copts")
    )
    commands = []
    translation_units = [source for source in sources if _is_translation_unit(source)]
    for index, source in enumerate(translation_units):
        output_path = "%s/%s.semantic-audit-%d.pic.o" % (
            ctx.bin_dir.path,
            target.label.name,
            index,
        )
        variables = cc_common.create_compile_variables(
            cc_toolchain = cc_toolchain,
            feature_configuration = feature_configuration,
            source_file = source.path,
            output_file = output_path,
            user_compile_flags = user_compile_flags,
            include_directories = compilation_context.includes,
            quote_include_directories = compilation_context.quote_includes,
            system_include_directories = compilation_context.system_includes,
            framework_include_directories = compilation_context.framework_includes,
            preprocessor_defines = depset(
                direct = _string_list_attr(ctx, "defines") + _string_list_attr(ctx, "local_defines"),
                transitive = [compilation_context.defines, compilation_context.local_defines],
            ),
            # Every ADR-067 audited native cc_library/cc_binary/cc_test action is
            # a PIC compile.  This is semantic, not an output-path detail:
            # __PIC__ is predefined only for this mode and target-owned source
            # may condition on it.  Keep the parallel Clang parse in exactly
            # the same language mode as the real CppCompile action.
            use_pic = True,
        )
        environment = dict(cc_common.get_environment_variables(
            feature_configuration = feature_configuration,
            action_name = CPP_COMPILE_ACTION_NAME,
            variables = variables,
        ))

        # Native CppCompile also receives this fixed working-directory marker
        # from Bazel.  The checker rejects every other environment key.
        environment["PATH"] = "/bin:/usr/bin:/usr/local/bin"
        environment["PWD"] = "/proc/self/cwd"
        commands.append({
            "arguments": cc_common.get_memory_inefficient_command_line(
                feature_configuration = feature_configuration,
                action_name = CPP_COMPILE_ACTION_NAME,
                variables = variables,
            ),
            "environment": environment,
            "source": source.path,
            "source_short_path": source.short_path,
        })

    linkstamp_files = _files_attr(ctx, "linkstamp")
    transitive_headers = compilation_context.headers.to_list()
    target_outputs = target[DefaultInfo].files.to_list()
    actual_compile_outputs = []
    if OutputGroupInfo in target and hasattr(target[OutputGroupInfo], "compilation_outputs"):
        actual_compile_outputs = target[OutputGroupInfo].compilation_outputs.to_list()
    inventory = ctx.actions.declare_file(
        "%s.same-run-semantic-inventory.json" % target.label.name,
    )
    ctx.actions.write(
        output = inventory,
        content = json.encode_indent({
            "attributes": {
                "additional_compiler_inputs": _short_paths(additional_compiler_inputs),
                "additional_linker_inputs": _short_paths(additional_linker_inputs),
                "args": _string_list_attr(ctx, "args"),
                "alwayslink": _bool_attr(ctx, "alwayslink"),
                "copts": _string_list_attr(ctx, "copts"),
                "data": _labels(_attr_values(ctx, "data")),
                "defines": _string_list_attr(ctx, "defines"),
                "dynamic_deps": _labels(_attr_values(ctx, "dynamic_deps")),
                "env": getattr(ctx.rule.attr, "env", {}) if hasattr(ctx.rule.attr, "env") else {},
                "env_inherit": _string_list_attr(ctx, "env_inherit"),
                "features": _string_list_attr(ctx, "features"),
                "flaky": _bool_attr(ctx, "flaky"),
                "implementation_deps": _labels(_attr_values(ctx, "implementation_deps")),
                "include_prefix": _string_attr(ctx, "include_prefix"),
                "includes": _string_list_attr(ctx, "includes"),
                "link_extra_lib": _labels(_attr_values(ctx, "link_extra_lib")),
                "linkopts": _string_list_attr(ctx, "linkopts"),
                "linkstatic": _bool_attr(ctx, "linkstatic"),
                "linkstamp": _short_paths(linkstamp_files),
                "local": _bool_attr(ctx, "local"),
                "local_defines": _string_list_attr(ctx, "local_defines"),
                "malloc": _labels(_attr_values(ctx, "malloc")),
                "nocopts": _string_attr(ctx, "nocopts"),
                "runtime_deps": _labels(_attr_values(ctx, "runtime_deps")),
                "shard_count": _int_attr(ctx, "shard_count"),
                "size": _string_attr(ctx, "size"),
                "stamp": _int_attr(ctx, "stamp", -1),
                "strip_include_prefix": _string_attr(ctx, "strip_include_prefix"),
                "tags": _string_list_attr(ctx, "tags"),
                "target_compatible_with": _labels(_attr_values(ctx, "target_compatible_with")),
                "testonly": _bool_attr(ctx, "testonly"),
            },
            "actual_compile_outputs": _short_paths(actual_compile_outputs),
            "commands": commands,
            "compiler": compiler,
            "configuration": configuration,
            "direct_dependency_labels": _labels(_attr_values(ctx, "deps")),
            "headers": _short_paths(public_headers),
            "label": str(target.label),
            "link_context": _link_inventory(target),
            "requested_features": requested_features,
            "schema_version": 1,
            "sources": _short_paths(sources),
            "target_output_paths": _file_paths(target_outputs),
            "target_outputs": _short_paths(target_outputs),
            "textual_headers": _short_paths(textual_headers),
            "transitive_headers": _short_paths(transitive_headers),
            "unsupported_features": unsupported_features,
        }) + "\n",
    )

    represented_inputs = depset(
        direct = sources + public_headers + textual_headers + additional_compiler_inputs +
                 additional_linker_inputs + linkstamp_files + target_outputs + actual_compile_outputs +
                 _link_files(target),
        transitive = [compilation_context.headers],
    )
    return [
        Phase4SameRunSemanticTargetInfo(
            configuration = configuration,
            inputs = represented_inputs,
            inventory = inventory,
            label = target.label,
            tool_files = cc_toolchain.all_files,
        ),
        # Expose the canonical inventory for direct aquery-vs-aspect evidence
        # without making it part of the audited target's DefaultInfo surface.
        OutputGroupInfo(
            phase4_same_run_semantic_inventory = depset([inventory]),
        ),
    ]

phase4_same_run_semantic_target_aspect = aspect(
    implementation = _phase4_same_run_semantic_target_aspect_impl,
    attrs = {
        "_semantic_audit_asan_setting": attr.label(default = Label("@llvm//config:asan")),
        "_semantic_audit_ubsan_setting": attr.label(default = Label("@llvm//config:ubsan")),
    },
    fragments = ["cpp"],
    required_providers = [CcInfo],
    toolchains = use_cc_toolchain(),
)

PHASE4_SAME_RUN_SEMANTIC_AUDIT_ATTRS = {
    "_semantic_audit_checker": attr.label(
        default = Label("//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit"),
        cfg = "exec",
        executable = True,
    ),
    "_semantic_audit_negative_fixtures": attr.label(
        allow_files = True,
        default = Label("//:phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit_negative_fixtures"),
    ),
    "_semantic_audit_ubsan_ignorelist": attr.label(
        allow_single_file = True,
        default = Label("@llvm//sanitizers:ubsan_ignore"),
    ),
    "_semantic_audit_stale_input_probe": attr.label(
        allow_single_file = True,
        default = Label(
            "//:tests/tools/phase4_h4096_session_v5_same_run_per_net_report_producer_preflight_semantic_audit/stale_action_input_probe.txt",
        ),
    ),
    "_semantic_audit_asan_setting": attr.label(default = Label("@llvm//config:asan")),
    "_semantic_audit_ubsan_setting": attr.label(default = Label("@llvm//config:ubsan")),
}

def _expect_semantic_target(target, subject):
    if Phase4SameRunSemanticTargetInfo not in target:
        fail("%s lacks Phase4SameRunSemanticTargetInfo" % subject)
    return target[Phase4SameRunSemanticTargetInfo]

def _build_live_ubsan_probe(ctx):
    sources = [
        file
        for file in ctx.files._semantic_audit_negative_fixtures
        if file.basename == "ubsan_signed_overflow.cc"
    ]
    if len(sources) != 1:
        fail("UBSan semantic audit requires exactly one signed-overflow source fixture")

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
        name = "%s_ubsan_live_probe" % ctx.label.name,
        srcs = sources,
        user_compile_flags = ctx.fragments.cpp.copts + ["-fno-sanitize-recover=all"],
    )
    linking_outputs = cc_common.link(
        actions = ctx.actions,
        cc_toolchain = cc_toolchain,
        compilation_outputs = compilation_outputs,
        feature_configuration = feature_configuration,
        link_deps_statically = True,
        name = "%s_ubsan_live_probe" % ctx.label.name,
        output_type = "executable",
        stamp = 0,
    )
    if linking_outputs.executable == None:
        fail("UBSan semantic audit toolchain did not produce a live-probe executable")
    return linking_outputs.executable

def phase4_same_run_run_semantic_audit(
        ctx,
        kind,
        production,
        test_support,
        direct_test,
        runner,
        forced_runner,
        extra_inputs = [],
        provider_inventories = [],
        provider_proofs = []):
    """Registers one caller-owned ADR-067 audit action.

    Args:
      ctx: The positive or negative audit rule context.
      kind: Exact audit kind, either `positive` or `negative`.
      production: Sanitizer-reset production-library target.
      test_support: Test-support library in the invoking configuration.
      direct_test: Direct test in the invoking configuration.
      runner: Sanitizer-reset ordinary runner target.
      forced_runner: Sanitizer-reset forced-source runner target.
      extra_inputs: Additional provider-audit inventories to action-key.
      provider_inventories: Canonical provider/startup manifests that the
        semantic checker must parse and authenticate.
      provider_proofs: Provider-audit proof stamps whose exact bytes the
        semantic checker must authenticate.

    Returns:
      Phase4SameRunSemanticAuditStampInfo for the caller-owned stamp.
    """

    if kind not in ("positive", "negative"):
        fail("semantic audit kind must be positive or negative, got %s" % kind)
    configuration = _target_configuration(ctx)
    roles = (
        ("production", _expect_semantic_target(production, "production")),
        ("test_support", _expect_semantic_target(test_support, "test support")),
        ("direct_test", _expect_semantic_target(direct_test, "direct test")),
        ("runner", _expect_semantic_target(runner, "runner")),
        ("forced_runner", _expect_semantic_target(forced_runner, "forced runner")),
    )
    for role, info in roles:
        expected = configuration if role in ("test_support", "direct_test") else "normal"
        if info.configuration != expected:
            fail(
                "%s semantic inventory has configuration %s, expected %s" %
                (role, info.configuration, expected),
            )

    manifest = ctx.actions.declare_file("%s.semantic-audit-manifest.json" % ctx.label.name)
    analysis_diagnostics = []
    if kind == "negative":
        # The caller invokes its live analysis-time classifiers before it can
        # register this action.  Serializing their exact identities makes that
        # successful analysis outcome an action-keyed semantic input rather
        # than an unauthenticated, name-only roster assertion.
        analysis_diagnostics = [
            "stamp-owner-wrong",
            "audit-output-missing",
            "audit-output-unrequested",
            _STALE_INPUT_DIAGNOSTIC,
        ]
    ctx.actions.write(
        output = manifest,
        content = json.encode_indent({
            "analysis_diagnostics": analysis_diagnostics,
            "configuration": configuration,
            "kind": kind,
            "roles": [
                {
                    "inventory": info.inventory.path,
                    "label": str(info.label),
                    "role": role,
                }
                for role, info in roles
            ],
            "schema_version": 1,
        }) + "\n",
    )

    if kind == "positive":
        basename = _POSITIVE_BASENAME
        invariant = _POSITIVE_INVARIANT
    else:
        basename = _NEGATIVE_BASENAME
        invariant = _NEGATIVE_INVARIANT
    stamp = ctx.actions.declare_file(basename)
    live_probe = None
    if kind == "negative" and configuration == "ubsan":
        live_probe = _build_live_ubsan_probe(ctx)

    arguments = ctx.actions.args()
    arguments.add("--mode", kind)
    arguments.add("--configuration", configuration)
    arguments.add("--manifest", manifest.path)
    arguments.add("--ignorelist", ctx.file._semantic_audit_ubsan_ignorelist.path)
    arguments.add("--stamp", stamp.path)
    arguments.add_all(
        ctx.files._semantic_audit_negative_fixtures,
        before_each = "--fixture",
    )
    arguments.add_all(provider_inventories, before_each = "--provider-inventory")
    arguments.add_all(provider_proofs, before_each = "--provider-proof")
    if live_probe != None:
        arguments.add("--ubsan-live-probe", live_probe.path)

    inventories = [info.inventory for _, info in roles]
    transitive_inputs = [info.inputs for _, info in roles]
    tool_files = [info.tool_files for _, info in roles]
    direct_inputs = [
                        manifest,
                        ctx.file._semantic_audit_ubsan_ignorelist,
                        ctx.file._semantic_audit_stale_input_probe,
                    ] + inventories + \
                    ctx.files._semantic_audit_negative_fixtures + extra_inputs + \
                    provider_inventories + provider_proofs
    direct_tools = [ctx.executable._semantic_audit_checker]
    if live_probe != None:
        direct_inputs.append(live_probe)
        direct_tools.append(live_probe)
    actual_action_inputs = depset(
        direct = direct_inputs + direct_tools,
        transitive = transitive_inputs + tool_files,
    ).to_list()
    canonical_action_inputs = depset(
        direct = [
                     manifest,
                     ctx.file._semantic_audit_ubsan_ignorelist,
                     ctx.executable._semantic_audit_checker,
                     ctx.file._semantic_audit_stale_input_probe,
                 ] + inventories + ctx.files._semantic_audit_negative_fixtures + extra_inputs +
                 provider_inventories + provider_proofs +
                 ([] if live_probe == None else [live_probe]),
        transitive = [info.inputs for _, info in roles] +
                     [info.tool_files for _, info in roles],
    ).to_list()
    expected_action_input_paths = sorted([file.path for file in canonical_action_inputs])
    actual_action_input_paths = sorted([file.path for file in actual_action_inputs])
    if phase4_same_run_semantic_action_input_inventory_violation(
        actual_action_input_paths,
        expected_action_input_paths,
    ) != None:
        fail("semantic action registration contains duplicate or stale input identities")
    if kind == "negative":
        stale_path = ctx.file._semantic_audit_stale_input_probe.path
        if len([path for path in expected_action_input_paths if path == stale_path]) != 1:
            fail("stale-input probe is not exactly represented in the semantic action closure")
        probes = (
            [path for path in expected_action_input_paths if path != stale_path],
            expected_action_input_paths + [stale_path],
            [
                stale_path if path == expected_action_input_paths[0] else path
                for path in expected_action_input_paths
            ],
        )
        for probe in probes:
            if phase4_same_run_semantic_action_input_inventory_violation(
                probe,
                expected_action_input_paths,
            ) != _STALE_INPUT_DIAGNOSTIC:
                fail("semantic input classifier accepted a concrete missing/extra/stale File")
    audit_environment = {"PATH": "/bin:/usr/bin:/usr/local/bin"}
    if configuration == "ubsan":
        audit_environment["UBSAN_OPTIONS"] = "halt_on_error=1"
    ctx.actions.run(
        mnemonic = "Phase4SameRunSemanticAudit",
        progress_message = "Auditing ADR-067 %s semantics (%s)" % (kind, configuration),
        executable = ctx.executable._semantic_audit_checker,
        arguments = [arguments],
        inputs = depset(
            direct = direct_inputs,
            transitive = transitive_inputs,
        ),
        tools = depset(
            direct = direct_tools,
            transitive = tool_files,
        ),
        outputs = [stamp],
        env = audit_environment,
    )
    return Phase4SameRunSemanticAuditStampInfo(
        basename = basename,
        configuration = configuration,
        invariant = invariant,
        kind = kind,
        stamp = stamp,
    )
