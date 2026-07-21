# ADR-021: Bounded KiCad Multi-Net Fixture Roster

**Status:** Accepted for the seventh Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Strict KiCad fixture import, terminal/obstacle ownership, and
the version-1 imported Phase 4 corpus seed

## Context

The original strict fixture adapter promotes one configured target net's pads
to Board IR terminals. Every other pad remains an owned static obstacle. That
boundary preserves the Phase 2 and Phase 3 single-request corpus identity, but
it cannot construct the authentic imported multi-net workload required by
Section 29.2.

Simply promoting several nets with the original terminal-only path would be a
correctness defect. Exact compilation ignores obstacles owned by the prepared
routing net. If a promoted pad stopped being an obstacle, every foreign net's
compiled field could treat that pad copper as free. Later unintended-terminal
checks do not repair a false-free compiled movement contract.

## Decision

- Preserve `ImportKicadFixture` and its legacy normalized results. Add the
  separate `ImportKicadMultiNetFixture` entry point for Phase 4.
- Require an explicit, complete, order-insensitive routable-net roster and an
  explicit default routing net that occurs in that roster. The default remains
  Board IR v1's sole default profile as required by ADR-016.
- Bound the roster before parsing the fixture: at most 25,000 names, 4 KiB per
  name, and 4 MiB of aggregate name bytes. Reject empty, duplicate,
  invalid-UTF-8, undeclared, or default-omitting rosters. Resolve failures in
  canonical name order.
- Require exactly two supported terminal pads for every roster net.
- Emit every roster pad both as a terminal and as an owner-aware obstacle on
  each occupied imported copper layer. Non-roster pads remain obstacle-only.
  Stable terminal and obstacle identities use their existing separate entity
  domains.
- Adopt `schemas/benchmark/phase4_imported_multi_net_corpus_v1.md`. The corpus
  builder first authenticates the pinned raw byte length and in-process fixture
  identity, then imports the exact two-net fixture, supplies explicit
  front-layer endpoints and per-net routing profiles, and delegates canonical
  context preparation to Multi-Net Workload v1. Raw source drift, including
  normalization-preserving comments or whitespace, fails closed.

## Consequences

- Each routed net can connect through its own pad regions while every other
  routed net sees those pads as foreign copper in exact and compiled geometry.
- Roster and fixture object order do not affect Board IR or workload identity.
- The original single-net fixture, planar corpus, and replay identities remain
  unchanged.
- The imported corpus seed proves supported-rule multi-net ancestry, exact
  ownership, CPU reachability, and deterministic replay association. It does
  not claim general KiCad support, per-net rule import, endpoint-layer
  inference, through-via routing, representative scale, 4/8/16 candidate
  pools, allocator improvement, equal-budget evidence, GPU results, combined
  route legality, host-CAD validation, or Phase 4 completion.
