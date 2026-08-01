# APGAR

**A Pretty Good Auto Router**

APGAR is an experimental, CAD-neutral PCB autorouting library. Its goal is to
use GPU throughput to explore substantially more routing alternatives and then
optimize route choices across the whole board—not merely to make a traditional
single-net router faster.

The project is currently in architecture and foundation work. It is not yet a
usable PCB autorouter.

## Core ideas

- Exact integer PCB geometry is authoritative.
- GPU search fields and graphs are disposable, conservative compiled artifacts.
- Route generators produce diverse, reusable candidates.
- A global allocator selects combinations of candidates under shared resource
  prices.
- Exact APGAR validation and the host CAD engine gate every committed route.
- CPU reference implementations serve as correctness oracles for GPU kernels.

## Documentation

- [Architecture specification](docs/APGAR_Architecture_Specification_v0.1.md) —
  the authoritative architecture, requirements, roadmap, and M1 definition of
  done.
- [Project kickoff](docs/KICKOFF.md) — the original research framing and design
  rationale.
- [Documentation index](docs/README.md) — document roles and update rules.
- [Contributor guide](CONTRIBUTING.md) — development and validation expectations.

Automated agents should also read [AGENTS.md](AGENTS.md) before changing the
repository.

## Build

APGAR uses Bazel with Bzlmod and pinned, downloaded, hermetic LLVM and Python
toolchains:

```sh
bazel lint
bazel build //...
bazel test //...
bazel run //:apgar_smoke
```

The `bazel` command must be Bazelisk. The repository wrapper adds `bazel lint`
and forwards normal commands to the version pinned in `.bazelversion`. See
[Building APGAR](docs/BUILDING.md) for toolchain, sanitizer, lint, and
hermeticity details.

GitHub Actions runs the Bazel gates on Linux and macOS, exercises ASan and UBSan,
and checks workflow security with `zizmor`. After those gates pass on a
same-repository pull request, Claude Code performs an APGAR-specific,
architecture-first review and requires one fresh formal verdict for the exact PR
head. The review uses the organization Actions secret
`CLAUDE_CODE_OAUTH_TOKEN`, with APGAR granted access; fork and Dependabot pull
requests do not consume the token.
