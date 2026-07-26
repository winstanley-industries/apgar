# ADR-057: Phase 4 Confirmatory H=4096 Exact-Small Oracle

**Status:** Accepted for the fifth post-Protocol-v2 confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Development-only H=4096 exact-cell fixed-pool proof before
heldout acquisition

## Context

ADR-053 opens H=4096 Same-Run Raw/Wire 2 execution and its mandatory Decision
Telemetry companion for exact development cell `(10100,4)`. ADR-055 opens the
independent per-net diagnostic join, and ADR-056 opens the sibling operational
measurement authority. None of those authorities opens the final-pool snapshot,
independent candidate-admission replay, or exact-small Oracle.

Confirmatory Decision Protocol v2 reserves
`phase4_confirmatory_exact_small_oracle_v2` for the H=4096 configuration.
The reserved suffix versions the configuration-specific publication authority;
it does not revise Same-Run Raw, telemetry, report, snapshot, or Oracle output
payload schemas. The existing confirmatory exact-small path remains the
Corpus-v2/H=2250 Protocol-v1 authority and must not be widened.

The reviewed H=4096 development bundle reports production objective
`(selected_net_count=6,total_overuse_units=1,total_intrinsic_base_cost=159000)`.
That value matches the previously observed fixed-pool optimum and motivates
this slice, but it is not a proof under the new authority. The Oracle
implementation and all of its external inputs must be reacquired from one
clean commit, and the independent replay and exhaustive enumeration must
freshly establish objective equality before publication.

## Decision

- Add separately named H=4096 production and test-only snapshot runners and a
  separately named H=4096 Oracle publication validator. Their compiled
  authorities, never caller options, select
  `phase4_confirmatory_corpus_v2_h4096` out of band and positively authenticate
  Confirmatory Decision Protocol v2 and canonical algorithm-budget roster v3.
- Fix every production entry point to explicit Corpus 2, exact cell
  `(10100,4)`, four workers, 20 repetitions, Same-Run Raw Evidence schema 2
  over Raw Wire 2, Same-Run Decision Telemetry schema 1 over Telemetry Wire 2,
  equal-arm `present_step_per_overuse_unit=1` and
  `history_step_per_overuse_unit=4096`, and canonical algorithm-budget checksum
  `8829615204625848656`. Independently require the Raw/Snapshot paired semantic
  `budget_checksum=5851813264366095594`; neither checksum may substitute for
  the other. Another configuration, case, pool, carrier, role, or rechecksummed
  alias must fail before evidence-producing work.
- Preserve Exact-Small Snapshot v1's payload schema, frozen component and
  output bounds, canonical JSON key order, artifact checksum domain, and source
  envelope checksum domain. Its complete
  `budget_checksum=5851813264366095594`, candidate-arm semantics, Raw/report
  associations, pools, capacities, and production selection must bind the
  H=4096 cell even though the payload schema remains one.
- Make
  `phase4_confirmatory_h4096_exact_small_snapshot_runner` fixtureless. Case
  10100 is generated; the target accepts no fixture path and declares no board
  fixture or fixture runfile capability. Scope, source, complete nonzero Raw
  references, Raw/report associations, equal-arm price fields, and budget
  identities must be checked before representative-case construction, preparer
  creation, or candidate execution.
- Make
  `phase4_confirmatory_h4096_exact_small_snapshot_test_runner` permanently
  preflight-only. It may exercise parsing and pre-access firewall rejection but
  cannot construct a representative case, create a preparer, execute a
  candidate arm, serialize a snapshot, or use any source-state or testing
  escape to publish evidence.
- Add the fixed separately compiled
  `phase4_confirmatory_h4096_exact_small_candidate_admission_replay`. Its
  private bounded replay wire v3 explicitly binds Corpus version 2, case 10100,
  pool four, equal-arm `present=1,history=4096`, and canonical
  algorithm-budget checksum `8829615204625848656` plus paired semantic budget
  checksum `5851813264366095594` before corpus construction or candidate
  traversal. The helper independently pins both identities and must not derive
  or accept one from the other. It rebuilds only that Corpus-v2 case, replays
  every source-private non-authenticating exact candidate-admission check,
  requires exact EOF, emits no stdout, and exposes no caller-selected corpus,
  configuration, case, helper, or executable path.
- Preserve the compiled-launcher and authenticated standalone-runfiles
  boundary. Before runfiles authentication, Python delegation, or any
  publisher input access, the H=4096 production launcher requires its generated
  source stamp to be clean and stamped and requires exactly one well-formed
  `--expected-commit` equal to the embedded build commit. Missing, duplicate,
  abbreviated, malformed, unstamped, dirty, or mismatched identity fails
  before input access. The compiled boundary rejects abbreviated
  expected-commit prefixes and the inner parser disables long-option
  abbreviation, preventing a second spelling from replacing the checked
  commit after delegation. The public publisher then authenticates its canonical executable,
  complete adjacent target-specific standalone runfiles tree, and hermetic
  interpreter before delegating through the one-use parent-bound handshake.
  Replay resolves only to the fixed H=4096 target in that authenticated tree;
  ambient or enclosing runfiles state cannot substitute another helper.
  Fixed clean, dirty, and unstamped test-only launchers make the source
  firewall deterministic in any worktree state. Their lightweight inner
  targets stop immediately after the handshake and argument parse, depend on
  neither the production validator nor the replay helper, and cannot open an
  input, execute replay or enumeration, construct an artifact, or emit an
  Oracle Artifact. Existing H=2250 and ordinary launchers remain unchanged.
- Require the H=4096 publisher to fail closed in this exact authority order:
  1. open and completely authenticate bounded regular H=4096 Same-Run Raw;
  2. only then open and completely join bounded regular H=4096 telemetry;
  3. only then open and completely join the bounded regular H=4096 Wire-2
     per-net report;
  4. only then open and completely validate the bounded regular Snapshot v1;
  5. only then invoke the fixed replay-v3 helper; and
  6. only after replay succeeds enumerate the complete bounded Cartesian
     product.
- Preserve Oracle output payload schema 1, its fixed key order, objective and
  witness semantics, and permanent non-standalone and non-timing flags. Select
  the Protocol-v2 authority through binding values and fresh final publication
  checksum domains
  `APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-ARTIFACT-V2` and
  `APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-SOURCE-V2`.
- Require exact equality between the production and exhaustive objective
  triples before constructing an Oracle Artifact. A mismatch emits no artifact
  and leaves the campaign incomplete; it is not an allocator loss. The
  validator must independently recompute overused-resource counts, but those
  counts remain diagnostic and do not rank worlds.
- Treat the current one overused resource and one overuse unit as compatible
  with fixed-pool optimality. Objective equality proves only that no world in
  the captured pools has a better lexicographic objective; it does not prove
  resource feasibility or combined-board legality.
- Preserve a structurally valid
  `exact_rejection_guardrail_passed=false` as publishable authentic negative
  evidence when every fixed-pool proof condition otherwise completes. The
  Oracle must not erase, repair, offset, or pre-judge the telemetry authority.
- After this implementation passes adversarial review and is committed,
  reacquire exact Raw, its telemetry companion, the sibling per-net report, and
  the final-pool snapshot from that exact clean commit before running replay
  and enumeration. A mutually joinable full exact-cell development bundle must
  also reacquire its sibling operational capture/publication from that commit.
  Earlier content cannot be copied, relabeled, rechecksummed, or accepted
  through a cross-commit exception.
- Preserve Confirmatory Decision Protocol v2 JSON byte-for-byte and leave every
  H=2250 runner, replay helper, validator, payload, and evidence artifact
  unchanged. Existing paths must continue to reject H=4096 semantics.
- Keep exact cases 10101 and 10102, every other development cell, fixed-query,
  stress, aggregation, matrix, decision, heldout, and imported paths closed.

## Consequences

One H=4096 exact development cell can independently prove whether the
production selection is lexicographically optimal inside its complete captured
candidate pools without widening an H=2250 authority or changing a frozen
payload schema.

Even if the expected objective `(6,1,159000)` is freshly proven and an Oracle
Artifact is emitted, the remaining overuse prevents a feasibility claim. The
artifact proves neither candidate-pool route completeness, global optimality
outside the frozen pools, operational performance, final legality, confirmatory
matrix completion, Phase 4 completion, nor M1 completion.

## Rejected alternatives

- **Promote the reviewed pre-authority objective without reacquisition.** A
  remembered value or cross-commit artifact is not an independently joined
  proof under the H=4096 Oracle authority.
- **Change Snapshot v1 or Oracle payload schema 1.** Both frozen payloads
  already carry the required semantics; only the H=4096 publication authority
  and final checksum domains change.
- **Reuse replay wire v2.** It binds Corpus 2 and case 10100 but does not bind
  the H=4096 price configuration or canonical algorithm budget.
- **Treat fixed-pool optimality as feasibility.** A lexicographic optimum may
  retain nonzero resource overuse when every captured world is infeasible.
- **Reject a false telemetry guardrail as malformed.** Authentic negative
  evidence remains publishable and belongs to later complete aggregation.
- **Open other cells or heldout acquisition with the Oracle.** Each remaining
  authority requires its own narrow adversarial review and, for heldout,
  the separately frozen campaign-acquisition authority.
