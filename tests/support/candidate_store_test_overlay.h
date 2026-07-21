#ifndef APGAR_TESTS_SUPPORT_CANDIDATE_STORE_TEST_OVERLAY_H_
#define APGAR_TESTS_SUPPORT_CANDIDATE_STORE_TEST_OVERLAY_H_

// Include every CandidateStore dependency normally before changing access, so
// the private overlay cannot expose constructors or evidence seams belonging
// to Board IR or RouteCandidate. Only the CandidateStore definition below is
// altered, and only inside the private Bazel testonly variant.
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "apgar/board_ir/board.h"
#include "apgar/candidates/route_candidate.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif
#define private public
#include "apgar/candidates/candidate_store.h"
#undef private
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace apgar::candidates::internal {

// Testonly atomic-publication probe. The production CandidateStore library and
// installed public header contain neither this declaration nor its symbol.
[[nodiscard]] std::vector<CandidateStoreAdmissionResult> PublishWithPinnedRollbackForTesting(
    CandidateStore& store, std::vector<RouteCandidate> candidates, board_ir::EntityRef pinned_net);

}  // namespace apgar::candidates::internal

#endif  // APGAR_TESTS_SUPPORT_CANDIDATE_STORE_TEST_OVERLAY_H_
