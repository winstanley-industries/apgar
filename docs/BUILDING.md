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
`.bazelrc.user`, which is imported after the ordinary repository defaults.
The publication-only `benchmark` configuration is declared after that import,
so its checked-in optimization, stamping, and workspace-status settings retain
precedence. Explicit command-line options remain later Bazel inputs and define
a different, noncanonical publication invocation when they override those
settings.

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

The ASan and UBSan configurations instrument the pinned LLVM CPU toolchain.
ASan excludes tests tagged `exact-address-space-envelope`: those subprocess
tests must enforce the canonical 64 GiB `RLIMIT_AS`, while the pinned ASan
runtime reserves roughly 14 TiB of virtual shadow address space before
`main`. The ordinary and UBSan gates run those exact-envelope tests without
weakening their resource contract; the ASan gate continues to run all
compatible CPU targets.
CUDA targets are explicitly incompatible with either sanitizer configuration:
the pinned CUDA compiler and NVIDIA driver boundary cannot currently be
instrumented end-to-end by those runtimes. Bazel therefore rejects commands
that combine `--config=cuda` with `--config=asan` or `--config=ubsan`, rather
than silently producing a partially instrumented result. Run the CPU sanitizer
gates above and the CUDA differential/replay gates separately.

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

CUDA is an opt-in Phase 2 execution platform. `--config=cuda` selects
checksum-pinned CUDA Toolkit 13.0.2 redistributable components, a
checksum-pinned GCC 15.2.0 host compiler and sysroot, and native plus PTX code
for compute capability 12.0. The pinned libstdc++ and libgcc runtimes are linked
statically into CUDA executables; neither CUDA nor any GCC compiler, headers,
link inputs, or runtime library is read from the host. Host glibc and the NVIDIA
kernel driver remain part of the declared Linux execution ABI.
CUDA targets carry the `manual` and `requires-gpu` tags, so default `//...`
builds remain CPU-only on Linux and macOS.

Run the platform smoke test before any other CUDA target:

```sh
bazel test --config=cuda //:cuda_smoke_test
```

Run the differential and durable replay gates separately from sanitizers:

```sh
bazel test --config=cuda \
  //:cuda_planar_route_test \
  //:gpu_replay_test \
  //:gpu_batch_replay_test
```

The smoke test reports backend/device/runtime metadata, rejects a dynamically
loaded libstdc++ or libgcc, and compares a deterministic device result with a
CPU oracle. CUDA execution currently
requires a Linux x86-64 host and a driver capable of running the pinned toolkit
and the configured compute capability; this does not change the supported
hosts for default CPU-only builds.

Phase 2 dispatch measurements use the pinned Google Benchmark 1.9.5 module,
not a repository-local timing loop. Run the optimized harness with an explicit
source commit and ask Google Benchmark to write its JSON report:

```sh
bazel run --config=cuda --config=benchmark //:planar_benchmark -- \
  --apgar_commit=EXACT_40_CHARACTER_COMMIT \
  --benchmark_out=/tmp/apgar-planar-bakeoff.json \
  --benchmark_out_format=json
```

The harness programmatically fixes 20 repetitions, a 0.02-second minimum
measurement time, a 0.01-second Google Benchmark warm-up, wall-clock timing,
and microsecond output. Each corpus CompiledBoard is uploaded once before
timing; the measured GPU scope is execution, readback, reconstruction, and
untrusted-result validation against that immutable prepared view. APGAR
counters add differential semantics, work, rounds, kernel time, deterministic
geometry fingerprints, and per-route owned device memory to Google Benchmark's
JSON. `--benchmark_dry_run` is useful only for bring-up and does not produce
publishable measurements.

Phase 3 candidate evidence additionally binds the exact source commit and dirty
state into the manual benchmark binary through Bazel stable workspace status.
The `benchmark` configuration invokes `tools/workspace_status.sh`; the runtime
label must match that VCS-derived commit and the source tree must be clean:

```sh
bazel run --config=cuda --config=benchmark \
  //:phase3_candidate_benchmark -- \
  --apgar_commit=EXACT_40_CHARACTER_COMMIT \
  --apgar_nvidia_kmd_driver=DRIVER_VERSION_FROM_NVIDIA_SMI \
  --benchmark_out=/tmp/apgar-phase3-candidate-bakeoff.json \
  --benchmark_out_format=json
```

The workspace-status command is a publication-time VCS metadata probe; it is
not a compiler or CUDA toolchain input. It runs only under `--config=benchmark`.
The benchmark continues to compile and link with the checksum-pinned Bazel C++,
CUDA, CUDA-host, and Google Benchmark dependencies described below.

The checked-in `.bazelrc` imports `.bazelrc.user` after ordinary developer
defaults but before defining the benchmark configuration. Developer settings
therefore retain their normal precedence while a user file cannot replace the
checked-in benchmark workspace-status command. The status probe resolves the repository that owns the script,
neutralizes inherited Git work-tree/index/config redirection, disables file
system monitors and the untracked cache, and treats `assume-unchanged` or
`skip-worktree` index entries as dirty. The published context identifies this
as `canonical_checked_in_invocation_v1`.

This binding assumes the documented command is run from a trusted checkout on
a trusted runner. An operator who controls command-line Bazel options, `PATH`,
the workspace-status executable, or `.git` metadata can construct a different
build and therefore cannot be authenticated by a source file inside that same
checkout. Publication automation must use the canonical checked-in invocation
and independent CI/build provenance when hostile-operator attestation is a
requirement. A command-line `--workspace_status_command` override is not valid
Phase 3 evidence even if the resulting JSON is shape-correct.

Obtain the KMD label with
`nvidia-smi --query-gpu=driver_version --format=csv,noheader,nounits` immediately
before the run. The binary rejects missing, malformed, duplicate, or mismatched
evidence labels, unstamped builds, and dirty source trees. Its
`process_lifetime_peak_rss_bytes` counter is the process-global `RUSAGE_SELF`
high-water mark and is not attributable to one benchmark row; deterministic
owned host payload and device-memory counters remain the subsystem comparisons.

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
