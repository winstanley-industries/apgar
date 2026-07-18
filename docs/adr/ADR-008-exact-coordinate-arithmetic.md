# ADR-008: Exact Coordinate Arithmetic Envelope

**Status:** Accepted
**Date:** July 17, 2026
**Applies to:** Exact Board IR v1 and CPU exact-geometry predicates

## Context

ADR-005 requires signed 64-bit global database coordinates, but storage width
alone does not make geometric predicates exact. Coordinate subtraction,
orientation, squared distance, and rational point-to-segment comparisons have
wider intermediate results. Unchecked native-width arithmetic could therefore
turn an illegal movement into an apparently legal one.

The first KiCad fixture adapter also needs to quantize decimal millimeter input
exactly once. KiCad's nanometer coordinates can place a rectangular boundary at
half a nanometer when an integer-nanometer pad size is centered on an
integer-nanometer coordinate.

## Decision

- Board IR coordinates remain signed 64-bit integers.
- Board IR v1 validates every coordinate against the inclusive envelope
  `[-1,000,000,000,000, +1,000,000,000,000]` database units before publishing
  an immutable snapshot.
- The KiCad M1 fixture profile uses 2,000,000 database units per millimeter,
  making one database unit 0.5 nm. Decimal input is parsed as base-10 text with
  one to six fractional millimeter digits when a decimal point is present;
  leading plus signs and trailing decimal points are rejected, and floating
  point is not used. The unit scale belongs to the adapter; the Board IR has no
  CAD-specific default.
- CPU orientation and dot-product intermediates use the pinned Clang
  toolchain's signed 128-bit integer type. Comparisons that square those values
  use an internal fixed 256-bit unsigned product.
- Public exact predicates return a structured result that distinguishes invalid
  input from a genuine clearance failure and reject inputs outside the validated
  envelope.
- Board IR and exact geometry use the same public coordinate-envelope helpers,
  and coordinate-envelope validation reports `kInvalidCoordinate` separately
  from malformed in-range geometry.
- Clearance equality is legal. A centerline whose exact swept envelope is one
  database unit closer than the required boundary is illegal.
- Overflow, unsupported geometry, and unrepresentable input are reported as
  errors. They are never converted into a permissive legality result.

The chosen coordinate envelope is much larger than an M1 PCB while proving that
all 128-bit orientation and projection intermediates remain bounded. The
256-bit comparison preserves exact Euclidean clearance tests without floating
point or an external geometry dependency.

## Consequences

- CPU exact predicates provide a deterministic oracle for compiled fields and
  future GPU kernels.
- A future backend without signed 128-bit host integers must provide an
  equivalent exact implementation; weakening the predicate is not compatible.
- Expanding the coordinate envelope or changing the KiCad unit scale requires a
  new Board IR schema version or an architecture decision with new arithmetic
  bounds and boundary tests.
- Curves, rotated pads, arbitrary polygons, and additional KiCad constructs
  remain unsupported by the strict M1 fixture profile until exact predicates
  and adapter declarations are added deliberately.
