"""Generates a C++ source-identity header from Bazel workspace status."""

def _source_stamp_header_impl(ctx):
    output = ctx.outputs.out
    ctx.actions.run(
        executable = ctx.executable._generator,
        arguments = [ctx.info_file.path, output.path],
        inputs = [ctx.info_file],
        outputs = [output],
        mnemonic = "ApgarSourceStamp",
        progress_message = "Generating APGAR source identity %{output}",
    )
    return [DefaultInfo(files = depset([output]))]

source_stamp_header = rule(
    implementation = _source_stamp_header_impl,
    attrs = {
        "out": attr.output(mandatory = True),
        "_generator": attr.label(
            default = "//bazel:source_stamp_generator",
            cfg = "exec",
            executable = True,
        ),
    },
)
