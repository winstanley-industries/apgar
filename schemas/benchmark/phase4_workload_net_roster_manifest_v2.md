# Phase 4 Workload-Net Roster Manifest v2

This sidecar independently freezes every complete ordered workload EntityRef
roster used by the Representative Corpus v2 confirmatory campaign. It binds
Representative Manifest v2 and does not broaden the v1 roster validator.

The root carries schema and corpus version `2`, the v2 corpus checksum, the v2
representative-manifest checksum, its own semantic checksum, 38 successful
rows, and four exclusions. The exclusions are:

- `12000` and `12001`: `descriptor_only_unsupported_pool`;
- `13001` and `13002`: `compiled_work_bound`.

Each successful row repeats the representative case identities and
authenticates the complete ordered EntityRef sequence. Per-row checksums use
Board IR v1 FNV-1a domain `APGAR-PHASE4-WORKLOAD-NET-ROSTER-V2`, followed by
schema version, corpus version/checksum, case identity, descriptor
fingerprint, case/Board/workload identities, net count, and each
`(net_id u64,generation u32)` pair.

The root `manifest_checksum` uses domain
`APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V2` over compact canonical JSON with
that field omitted. The artifact is compact canonical UTF-8 JSON with one
terminal LF.
