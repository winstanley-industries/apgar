# Phase 4 Confirmatory Canonical Algorithm-Budget Roster v3

This acquisition-free authority freezes the inactive
`phase4_confirmatory_corpus_v2_h4096` canonical configuration. It does not
replace or reinterpret Representative Manifest v2, Workload-Net Roster
Manifest v2, Confirmatory Decision Protocol v1, or any evidence acquired under
their H=2250 configuration.

The authority retains Representative Corpus v2, all 40 ordered paired-case
rows, all workload identities, root seeds, query/work opportunities, external
budgets, and every non-price canonical configuration field. Its only
configuration change is applied equally to the sequential baseline and
reusable-candidate session:

- `present_step_per_overuse_unit=1`;
- superseded `history_step_per_overuse_unit=2250`; and
- effective `history_step_per_overuse_unit=4096`.

The 102 ordered `(case_id,pool)` cells are exactly the canonical budget cells
embedded in Representative Manifest v2: three exact, 18 calibration, 72
heldout, three executable fixed-query, three stress (including the two typed
compiled-work-bound cases), and three imported cells. Descriptor-only
`(12000,1024)` and `(12001,1)` are not algorithm-budget cells.

The compact canonical UTF-8 JSON ends in exactly one LF. It binds:

1. roster schema and authority name;
2. Representative Corpus v2 and checksum;
3. Representative Manifest v2 as case authority;
4. Workload-Net Roster Manifest v2 as workload authority;
5. the superseded H=2250 roster-v2 checksum;
6. the fixed H=4096 configuration authority and equal-arm invariants;
7. the exact cell count; and
8. all 102 ordered per-cell canonical algorithm-budget checksums.

`roster_checksum` uses Board IR v1 FNV-1a with this exact typed preimage:

1. `AddString("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V3")`;
2. `AddU32(schema_version)`;
3. `AddString(authority)`;
4. `AddU32(corpus_version)`, then `AddU64(corpus_checksum)`;
5. `AddU32(representative_manifest_schema_version)`, then
   `AddU64(representative_manifest_checksum)`;
6. `AddU32(workload_roster_manifest_schema_version)`, then
   `AddU64(workload_roster_manifest_checksum)`;
7. `AddU32(supersedes.schema_version)`, then
   `AddU64(supersedes.roster_checksum)`;
8. `AddString(configuration_authority)`;
9. `AddU64(present_step_per_overuse_unit)`,
   `AddU64(superseded_history_step_per_overuse_unit)`, and
   `AddU64(history_step_per_overuse_unit)`;
10. `AddBool(baseline_and_candidate_price_configs_equal)`,
    `AddBool(all_other_canonical_algorithm_budget_fields_unchanged)`, and
    `AddBool(query_work_and_external_opportunity_unchanged)`;
11. `AddU64(cell_count)`; and
12. for every cell in increasing case/pool order, `AddU32(case_id)`,
    `AddU32(pool)`, and `AddU64(checksum)`.

Strings use the Board IR stable-hash length-prefixed byte encoding; booleans
use its single-byte encoding. Validation constrains `cell_count` to the u32
range, while its checksum encoding is deliberately u64 as listed above.
Per-cell checksums retain
`APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-V1`, which already hashes the complete
baseline, preparation, and candidate-session configuration preimage.

The fixed C++ generator is linked through dead-section-eliminated preimage
libraries and contains no fixture, representative-case, worker, or
allocator-execution symbol. Its source accepts no arguments and invokes only
the internal H=4096 configuration transformer and checksum routine. Every
ASan- or UBSan-instrumented generator build is analysis-incompatible because
sanitizer registration roots otherwise-dead shared-source sections. Sanitizer
gates run the separately instrumented H=4096 structural test and link-inspect
the exact ordinary generator through a sanitizer-reset audit transition; they
do not present a partially instrumented generator as satisfying this contract.
Every existing Corpus-v2 execution entry point positively requires equal-arm
`present=1,history=2250` before case or preparer access. This roster alone
authorizes no Raw, report, operational, exact-oracle, matrix, or heldout
execution. A separately frozen Confirmatory Decision Protocol v2 and
separately reviewed execution entry points are required before acquisition.
