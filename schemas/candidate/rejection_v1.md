# Candidate Rejection Record Contract v1

Candidate rejection v1 is an immutable structured diagnostic retained by the
candidate store whenever generated input cannot become a stored candidate.
Rejection is evidence; it is never converted into a permissive candidate.

Each record contains schema version `1`, candidate and net identity when
decodable, lifecycle stage, stable rejection code, invariant identifier,
Board/profile/compiler/routing/rule associations, generator and policy
provenance, primitive/resource witness indices, expected and actual integer
values when relevant, optional obstacle reference/provenance, candidate payload
checksum when computable, and a bounded UTF-8 detail string.

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
eight provenance fields in RouteCandidate order; presence/value pairs for
primitive witness, resource witness, expected integer, and actual integer;
obstacle presence and entity reference; candidate-payload-checksum presence
and value; detail length/string; then declared logical bytes. Presence flags
are one byte and optional values are emitted only when present.

Details are diagnostic only; consumers branch on the stable code and invariant
identifier. Retention ordering compares every canonical field with candidate
and net presence made explicit, so two unequal records never rely on arrival
order. Records have their own checked canonical-byte accounting. A retained
diagnostic may omit a corrupt bulk payload, but it must retain enough identity,
associations, claimed policy identity, hashes, and witnesses to reproduce or
explain the failure.
