"""Test-only rule for declaring a runfile at an adversarial logical path."""

def _test_runfile_impl(ctx):
    output = ctx.actions.declare_file(ctx.attr.path)
    ctx.actions.symlink(
        output = output,
        target_file = ctx.file.src,
    )
    return [DefaultInfo(files = depset([output]))]

_test_runfile = rule(
    implementation = _test_runfile_impl,
    attrs = {
        "path": attr.string(mandatory = True),
        "src": attr.label(
            allow_single_file = True,
            mandatory = True,
        ),
    },
)

def test_runfile(name, src, path):
    """Declare one test-only runfile with a caller-selected logical path."""
    _test_runfile(
        name = name,
        src = src,
        path = path,
        testonly = True,
    )

def _test_standalone_runfiles_impl(ctx):
    files_to_run = ctx.attr.src[DefaultInfo].files_to_run
    if files_to_run == None or files_to_run.executable == None:
        fail("target-specific runfiles require an executable")
    marker = ctx.actions.declare_file(ctx.label.name + ".runfiles_ready")
    ctx.actions.run(
        arguments = [
            files_to_run.executable.path,
            marker.path,
            "probe" if ctx.attr.probe else "materialize",
        ],
        executable = ctx.executable._marker_tool,
        mnemonic = "MaterializeTestStandaloneRunfiles",
        outputs = [marker],
        tools = [files_to_run],
    )
    return [DefaultInfo(files = depset([marker]))]

_test_standalone_runfiles = rule(
    implementation = _test_standalone_runfiles_impl,
    attrs = {
        "_marker_tool": attr.label(
            default = "//bazel:test_standalone_runfiles_marker",
            executable = True,
            cfg = "exec",
        ),
        "probe": attr.bool(default = True),
        "src": attr.label(mandatory = True),
    },
)

def test_standalone_runfiles(name, src, probe = True):
    """Force one executable's target-specific runfiles tree for a nested test."""
    _test_standalone_runfiles(
        name = name,
        probe = probe,
        src = src,
        testonly = True,
    )
