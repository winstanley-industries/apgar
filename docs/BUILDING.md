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

## Standard commands

```sh
bazelisk build //...
bazelisk test //...
bazelisk run //:apgar_smoke
```

Release and sanitizer configurations are explicit:

```sh
bazelisk build --config=release //...
bazelisk test --config=asan //...
bazelisk test --config=ubsan //...
```

When changing `MODULE.bazel`, update and inspect `MODULE.bazel.lock` with a
successful build or `bazelisk mod deps`. In validation and CI, use
`--lockfile_mode=error` to reject an out-of-date lockfile:

```sh
bazelisk test --lockfile_mode=error //...
```

## Formatting

The LLVM toolchain exposes its pinned `clang-format` binary through Bazel. Run
it with the source files being changed:

```sh
bazelisk run @llvm_toolchain//:clang-format -- \
  -i "$PWD/include/apgar/version.h" "$PWD/src/version.cc" \
  "$PWD/tests/version_test.cc" "$PWD/tools/apgar_smoke.cc"
```

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
