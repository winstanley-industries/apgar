# Candidate Rejection Record Contract v1

Candidate rejection v1 is an immutable structured diagnostic retained by the
candidate store whenever generated input cannot become a stored candidate.
Rejection is evidence; it is never converted into a permissive candidate.

Each record contains schema version `1`, candidate and net identity when
decodable, lifecycle stage, stable rejection code, invariant identifier,
Board/profile/compiler/routing/rule associations, generator and policy
provenance, primitive/resource witness indices, expected and actual integer
values when relevant, an optional conflicting Board IR entity reference
(including an obstacle or unintended terminal), candidate payload checksum when
computable, and a bounded UTF-8 detail string.

The invariant identifier, provenance `supported_device_class`, and detail are
each valid UTF-8 and at most 1,024 encoded bytes. The device-class string may
be empty when no backend evidence exists for a candidate-less transaction
diagnostic. The 1,024-byte limit is part of schema-v1 compatibility.

Candidate construction is part of this lifecycle, not a lossy pre-admission
exception path. Normalization, exact-geometry, resource reconstruction, metric,
and memory-accounting failures returned by a typed CPU/GPU builder are full
Candidate Rejection v1 records. They retain the lifecycle stage, derived
candidate/net identity, associations, bound provenance, primitive/resource
witnesses, conflicting-entity reference, expected/actual values, and a checksum
of the available draft payload. Candidate stores expose bounded canonical ingestion
for these records so callers such as the benchmark retain them instead of
counting and discarding them.

Canonical ingestion accepts schema version `1` and only the known lifecycle,
rejection-code, generator, and backend enum tags. It independently validates
all three bounded text fields and recomputes `logical_bytes`; a caller-supplied
logical byte count is never trusted. The canonicalizer receives a non-owning
reference and checks each text byte count in O(1) before copying or scanning
that field; only an at-most-1,024-byte field receives UTF-8 validation. An
incompatible tag, invalid UTF-8, overlong field, or accounting overflow is not
retained under the claimed invariant. It is replaced by one bounded schema-v1
`InvalidInput` record with invariant `candidate.rejection.ingestion.v1`. That
replacement preserves only the fixed-width candidate ID, net reference,
association tuple, and candidate payload checksum when present; unvalidated
provenance, witnesses, conflicting entity, and text are discarded.

Lifecycle stages use the stable zero-based byte tags `generated=0`,
`normalized=1`, `exact_validated=2`, `resource_accounted=3`,
`signed_and_deduplicated=4`, and `stored=5`.

Rejection classes use these stable byte tags:

| Code | Tag | Code | Tag |
| --- | ---: | --- | ---: |
| invalid input | 0 | unsupported | 1 |
| exact validation | 2 | association mismatch | 3 |
| resource mismatch | 4 | metric mismatch | 5 |
| signature mismatch | 6 | duplicate identity | 7 |
| duplicate geometry | 8 | duplicate resources | 9 |
| budget exhausted | 10 | memory-accounting overflow | 11 |
| memory-accounting mismatch | 12 | internal invariant | 13 |
| cancelled | 14 | backend failure | 15 |

Unknown stage or code tags are incompatible.

The canonical record order is schema version; candidate-ID presence and value;
net presence and value; stage; code; invariant-ID length/string; the five
candidate associations in RouteCandidate order; claimed policy identity; the
eight provenance fields in RouteCandidate order, including supported-device-
class length/string; presence/value pairs for primitive witness, resource
witness, expected integer, and actual integer; conflicting-entity presence and
entity reference; candidate-payload-checksum presence and value; detail length/string;
then declared logical bytes. Every string comparison is unsigned encoded-byte
length first and bytewise value second; ordinary lexical string ordering is not
the schema order. Presence flags are one byte and optional values are emitted
only when present.

When an untrusted candidate policy fails its O(1) schema-v1 resource-count
preflight, rejection construction must not copy, iterate, or hash that bulk
payload. The record uses invariant `candidate.policy.resource_entry_count.v1`,
records the 1,000,000-entry bound and the actual aggregate count when it fits in
unsigned 64 bits, and omits `candidate_payload_checksum` even when the input
claims a nonzero checksum.

For every bounded draft payload, rejection construction independently
recomputes `candidate_payload_checksum` from the actual available canonical
fields. It never adopts the draft's nonzero claimed checksum. A later payload-
checksum mismatch records expected=recomputed and actual=supplied while the
rejection checksum remains the recomputed payload value. Every failed O(1)
shape preflight omits the rejection checksum because that bulk payload is not
walked.

Details are diagnostic only; consumers branch on the stable code and invariant
identifier. Retention ordering compares every canonical field with candidate
and net presence made explicit, so two unequal records never rely on arrival
order. Records have their own checked canonical-byte accounting. A retained
diagnostic may omit a corrupt bulk payload, but it must retain enough identity,
associations, claimed policy identity, hashes, and witnesses to reproduce or
explain the failure.
