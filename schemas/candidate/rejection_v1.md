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

Lifecycle stages are generated, normalized, exact-validated,
resource-accounted, signed/deduplicated, and stored. Stable rejection classes
include invalid input, unsupported, exact validation, association mismatch,
resource mismatch, metric mismatch, signature mismatch, duplicate identity,
duplicate geometry, duplicate resources, budget exhausted, memory-accounting
overflow/mismatch, internal invariant, cancelled, and backend failure.

Details are diagnostic only; consumers branch on the stable code and invariant
identifier. Records are ordered by `(net, candidate ID, stage, code,
invariant ID, payload checksum)` and have their own checked canonical-byte
accounting. A retained diagnostic may omit a corrupt bulk payload, but it must
retain enough identity, associations, hashes, and witnesses to reproduce or
explain the failure.

