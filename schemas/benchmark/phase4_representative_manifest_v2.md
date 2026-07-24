# Phase 4 Representative Manifest v2

This manifest freezes the 40 paired-case rows and 102 canonical algorithm
budgets for Representative Corpus v2. It is additive and does not replace or
reinterpret Representative Manifest v1.

The root is compact canonical UTF-8 JSON with one terminal LF and fields in
this order:

1. `schema_version=2`;
2. `corpus_version=2`;
3. the frozen v2 `corpus_checksum`;
4. `manifest_checksum`;
5. 40 increasing `cases`; and
6. 40 increasing `canonical_algorithm_budgets`.

Cases `12000` and `12001` remain descriptor-only fixed-query controls because
their requested pools are outside the canonical paired execution range. The
remaining 40 descriptors have one manifest row. Cases `13001` and `13002`
retain typed `compiled_work_bound` witnesses; the other 38 rows carry complete
Board, workload, capacity, compiled-work, active-region, and Board-entity
identities. The budget roster includes the two work-bound cases and expands to
102 unique `(case_id,pool)` entries.

`manifest_checksum` uses Board IR v1 FNV-1a domain
`APGAR-PHASE4-REPRESENTATIVE-MANIFEST-V2` over the compact canonical root with
the `manifest_checksum` field omitted. Generation may build descriptors,
Board/workload/capacity identities, and canonical configuration preimages. It
must not execute an allocator arm or observe a heldout allocation outcome.

The checked-in generator emits the representative manifest and its workload
roster in one sequential pass so no large case is retained alongside another.
Ordinary validation authenticates the frozen bytes and uses cheap budget
preimage reconstruction; it does not rematerialize the full corpus.
