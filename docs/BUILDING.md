# Building APGAR

Bazel is the canonical APGAR build interface. The repository uses Bzlmod for
dependency resolution and a downloaded, zero-sysroot LLVM toolchain for C and
C++ compilation.

## Prerequisites

- Bazelisk, which reads the pinned Bazel version from `.bazelversion`.
- Network access on the first build so Bazel can fetch checksum-verified module
  and toolchain archives.

No host C or C++ compiler, platform SDK, or Python interpreter is required.
Bazel downloads and selects the LLVM and CPython versions declared in
`MODULE.bazel`. The LLVM toolchain supplies pinned C and C++ headers, libraries,
linker inputs, and sanitizer runtimes instead of searching the host system.

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
language while iterating, use `bazel lint --only cpp`,
`bazel lint --only python`, `bazel lint --only shell`, or
`bazel lint --only starlark`.

The `//bazel/lint:lint` Python binary runs under downloaded CPython 3.13.13 and
discovers tracked and untracked, non-ignored files. Its declarative language
registry invokes pinned tools through the real Bazel binary supplied by
Bazelisk: the hermetic LLVM `clang-format` for C, C++, and CUDA; Ruff for
Python; ShellCheck for Bash/shell scripts; and Buildifier for Bazel/Starlark.
Unit tests cover file matching, selection, discovery, command construction,
failure propagation, and the Python toolchain version. ShellCheck has no
in-place fix mode, so `bazel lint --fix` runs it as a check after applying other
supported fixes.

When a language is added, add a `Linter` entry in `bazel/lint/lint.py` and
extend `//bazel/lint:lint_test`. A linter declares its file patterns and one or
more check/fix invocations, so the default command continues to cover every
supported language without adding another orchestration script.

## Hermeticity boundary

The Bazel binary, module graph, LLVM compiler binaries, CPython runtime, lint
tools, compiler headers, platform headers, C and C++ runtimes, linker inputs,
and normal build actions are pinned or sandboxed. Toolchain archives are
checksum-verified, and Bazel records the resolved module graph and extension
inputs in `MODULE.bazel.lock`.

Linux builds use the toolchain's zero-sysroot mode and default pinned glibc ABI;
they do not read `/usr/include` or `/usr/lib`. macOS builds use the pinned SDK
declared by the LLVM module rather than the SDK selected on the build host.
Bazel's local C++ and Apple C++ toolchain discovery is disabled so an
incompatible registered toolchain fails resolution instead of silently falling
back to host tools.

CUDA is intentionally not part of this first foundation. It will be introduced
as a separately pinned toolchain and execution platform so CPU-only development
and CI do not depend on a local CUDA installation or GPU.

## Continuous integration

GitHub Actions runs lint, build, and test checks on Ubuntu and build and test
checks on macOS. Separate Linux jobs run the test suite under AddressSanitizer
and UndefinedBehaviorSanitizer. All CI builds reject dependency-lock changes.

The workflow uses GitHub-hosted `ubuntu-24.04` and `macos-15` runners by
default. To migrate a job to an on-premises runner without changing its status
check name or branch-protection rule, set the `APGAR_LINUX_RUNNER` or
`APGAR_MACOS_RUNNER` repository Actions variable to a label assigned to that
runner. Leave either variable unset to keep that platform on GitHub-hosted
infrastructure.

The standard GitHub-hosted Linux image does not have enough free space for the
LLVM toolchain and its generated runtimes alongside the preinstalled Android
SDK, so Linux jobs remove that unused SDK before Bazel starts. The cleanup is
guarded by `runner.environment` and never runs on self-hosted infrastructure.
