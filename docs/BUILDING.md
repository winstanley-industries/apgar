# Building APGAR

Bazel is the canonical APGAR build interface. The repository uses Bzlmod for
dependency resolution and a downloaded LLVM distribution for C and C++
compilation.

## Prerequisites

- Bazelisk, which reads the pinned Bazel version from `.bazelversion`.
- Network access on the first build so Bazel can fetch checksum-verified module
  and toolchain archives.
- On macOS, an installed Apple developer SDK selected by `xcode-select`.

No host C or C++ compiler is required. Bazel downloads and selects the LLVM
version declared in `MODULE.bazel`.

Developer-specific Bazel settings may be placed in the ignored
`.bazelrc.user`, which is imported after the repository defaults.

Install Bazelisk as `bazel` on `PATH`, as the standard Bazelisk packages do.
Bazelisk delegates to `tools/bazel`; APGAR's wrapper adds repository commands
and forwards all native Bazel commands to the downloaded binary selected by
`.bazelversion`. Invoking `tools/bazel` directly is intentionally unsupported
because that would bypass version selection.

## Standard commands

```sh
bazel build //...
bazel test //...
bazel run //:apgar_smoke
```

Release and sanitizer configurations are explicit:

```sh
bazel build --config=release //...
bazel test --config=asan //...
bazel test --config=ubsan //...
```

When changing `MODULE.bazel`, update and inspect `MODULE.bazel.lock` with a
successful build or `bazel mod deps`. In validation and CI, use
`--lockfile_mode=error` to reject an out-of-date lockfile:

```sh
bazel test --lockfile_mode=error //...
```

## Linting and formatting

Run every repository linter in check mode with one command:

```sh
bazel lint
```

Apply safe formatter and lint fixes with `bazel lint --fix`. To run a single
driver while iterating, use `bazel lint --only cpp` or
`bazel lint --only starlark`.

The dispatcher in `bazel/lint/lint.sh` runs language-specific drivers over
tracked and untracked, non-ignored files. The C/C++ driver uses the pinned
LLVM `clang-format`; the Bazel/Starlark driver uses the pinned Buildifier. Both
tools are launched through the real Bazel binary supplied to `tools/bazel` by
Bazelisk.

When a language is added, add its executable driver under `bazel/lint/` and
register it in the dispatcher's default language list. Drivers receive the
repository root, requested mode, and real Bazel path through the documented
`APGAR_*` environment variables used by the existing drivers.

## Hermeticity boundary

The Bazel binary, module graph, LLVM compiler binaries, compiler builtin
headers, and normal build actions are pinned or sandboxed. Toolchain archives
are checksum-verified, and Bazel records the resolved module graph and extension
inputs in `MODULE.bazel.lock`.

On macOS, the Apple SDK supplies platform headers, libc++, system libraries, and
linker integration. Those remain host platform inputs because Apple SDK
redistribution is constrained. Fully reproducible cross-host macOS artifacts
will require an explicitly provisioned SDK execution environment. A future
Linux remote-execution platform can use a pinned sysroot for a fully
self-contained C++ action environment.

CUDA is intentionally not part of this first foundation. It will be introduced
as a separately pinned toolchain and execution platform so CPU-only development
and CI do not depend on a local CUDA installation or GPU.
