#ifndef APGAR_TESTS_SUPPORT_CPU_ROUTE_EVIDENCE_TEST_ACCESS_H_
#define APGAR_TESTS_SUPPORT_CPU_ROUTE_EVIDENCE_TEST_ACCESS_H_

#include "apgar/routing/cpu_astar.h"

namespace apgar::test_support {

// Deliberate test-only capability for constructing authenticated malformed CPU
// route payloads. Production targets cannot depend on the Bazel testonly
// library that implements this function.
void ResealCpuRouteEvidenceForTest(routing::CpuRoute& route);

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_CPU_ROUTE_EVIDENCE_TEST_ACCESS_H_
