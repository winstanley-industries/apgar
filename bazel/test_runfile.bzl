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
