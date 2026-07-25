# ADR-050: Phase 4 Confirmatory Exact-Small Oracle

**Status:** Accepted for the sixth post-freeze confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Corpus v2 exact-development fixed-pool proof before heldout acquisition

## Context

ADR-047 opens the complete Same-Run Raw/telemetry/per-net diagnostic join for
exact development cell `(10100,4)`, and ADR-049 separately opens its operational
measurement authority. Confirmatory Decision Protocol v1 also freezes
`phase4_confirmatory_exact_small_oracle_v1` as a required exact-cell authority,
but the existing snapshot runner, independent admission-replay helper, and
Oracle-v2 publisher remain Corpus v1 entry points.

Exact-Small Snapshot v1 already hashes the corpus checksum, case identity,
complete candidate semantics including corpus version, Raw and report
associations, pools, capacities, and production selection. Its serialized shape
and checksum domains therefore do not need to change for an explicitly selected
Corpus v2 path. The corpus authority still cannot be inferred from the fresh
case ID, and a Corpus v2 snapshot cannot be accepted through a legacy publisher.

The first real `(10100,4)` development run exposed the intended completion
gate. Production selected objective `(6,3,112200)`, while exhaustive
enumeration of the eight admitted frozen-pool worlds found the unique optimum
`(6,1,159000)`. The existing one- and five-round terminal schedules repeatedly
return the production objective. This is authentic diagnostic evidence that
the frozen production search is not exact-optimal for this cell; it is not a
malformed snapshot, an oracle-comparator defect, or a publishable failed Oracle
Artifact.

## Decision

- Preserve Exact-Small Snapshot v1's payload shape, component bounds,
  serialization, and artifact/source checksum domains. Add separately selected
  Corpus v2 builder and validator entry points while keeping every legacy entry
  point Corpus v1-only.
- Add separately named production and test-only confirmatory snapshot runners.
  They require explicit Corpus 2, Raw Evidence schema 2, Raw Wire 2, and exactly
  `(10100,4)` with four workers and 20 repetitions before fixture access,
  representative-case construction, preparer creation, or candidate execution.
  The production runner requires a clean stamped source; only the test target
  recognizes the existing unstamped-source escape.
- Require the confirmatory snapshot producer to bind the Corpus v2 cell-plan
  checksum, Raw-v2 source-envelope domain, complete repetition-zero
  baseline-first Raw references, and the associated Wire-2 report artifact and
  source envelope. Every claimed artifact, envelope, and Raw reference must be
  nonzero before fixture resolution or candidate work. The in-process
  associations remain claims until the external publication validator loads and
  joins every authority.
- Add a fixed, separately compiled
  `phase4_confirmatory_exact_small_candidate_admission_replay` authority. Its
  bounded binary input explicitly names replay wire version 2, Corpus version 2,
  and case 10100. It rebuilds only the Corpus v2 case, replays every source-private
  non-authenticating exact candidate-admission check, requires exact EOF, and
  exposes no caller-selectable corpus or helper path. Use a compiled publisher
  launcher to authenticate its canonical executable, complete adjacent
  target-specific standalone runfiles tree, and hermetic Python interpreter,
  then delegate to the fixed private Python target through a one-use
  parent-bound handshake. An enclosing Bazel runfiles tree may contain an
  invocation symlink but is never selected as the Python/data authority.
  Resolve replay only from the authenticated standalone `.runfiles/_main`
  tree; ambient runfiles directories and manifests are not authorities, and
  the private inner target cannot run directly. Direct stage-one Python
  execution disables `site` initialization until the rules_python bootstrap
  selects the standalone root; stage-two site initialization occurs only after
  that selection. The legacy replay target remains Corpus v1-only behind the
  same launcher boundary.
- Add the domain-separated
  `phase4_confirmatory_exact_small_oracle_v1` publisher. Its CLI reads bounded
  regular files in fail-closed order: fully validate Same-Run Raw and enforce
  `(10100,4)` before opening telemetry; completely join telemetry before opening
  the Wire-2 report; completely join the report before opening the snapshot.
  Only after all four authorities join may it invoke the fixed Corpus v2 replay
  helper and enumerate the complete bounded Cartesian product.
- Bind the confirmatory campaign, exact cell configuration, Corpus 2 identity,
  and complete Raw, telemetry, report, and snapshot artifact/source associations
  in the output. The standalone artifact validator pins every field of the
  frozen `(10100,4)` configuration rather than accepting a self-derived cell-plan
  alias. Use fresh artifact and source domains
  `APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-ARTIFACT-V1` and
  `APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-SOURCE-V1`.
- Preserve a structurally valid
  `exact_rejection_guardrail_passed=false` value as publishable authentic
  negative evidence when every oracle completion condition otherwise passes.
  The oracle proves only that the production objective is exact-optimal within
  the admitted frozen pools; it must not reinterpret the telemetry guardrail,
  claim route completeness, publish timing, or decide Phase 4.
- Fail without output when the production and exhaustive objectives differ.
  Preserve the real `(10100,4)` result as an expected process-level rejection:
  no `exact_small_oracle_complete=true` artifact may be constructed from it,
  and missing or failed oracle authority leaves the confirmatory campaign
  incomplete rather than recording an allocator loss.
- Preserve Confirmatory Decision Protocol v1 byte-for-byte. The operational
  publication remains a sibling exact-cell requirement rather than an oracle
  input.

## Consequences

One exact development cell now exercises the complete fail-closed confirmatory
snapshot, join, replay, and exhaustive-enumeration path without observing a
heldout outcome or widening a legacy Corpus v1 publisher. The current
production result does not mint the required oracle authority, so confirmatory
evidence remains incomplete.

Any production remediation is a separate versioned allocator/configuration
slice. Changing price steps, terminal schedules, session semantics, or another
checksum-bound search rule requires a new canonical budget roster and
confirmatory protocol authority before heldout execution; it must not weaken
this independent oracle or silently rewrite the frozen v1 protocol.

Exact cases 10101 and 10102, every heldout and imported case, complete
aggregation, campaign execution, and the Phase 4 decision remain closed. The
fixed-pool result does not prove candidate-pool route completeness, combined
board legality, or global optimality outside the captured pools.
