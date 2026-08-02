#ifndef APGAR_TESTS_SUPPORT_COMPILED_BOARD_TEST_ACCESS_H_
#define APGAR_TESTS_SUPPORT_COMPILED_BOARD_TEST_ACCESS_H_

#include <cstdint>

#include "apgar/geometry_compiler/compiled_board.h"

namespace apgar::geometry_compiler {

class CompiledBoardTestPeer {
 public:
  static bool AddLegalEdge(CompiledBoard& board, board_ir::LayerId layer, std::int64_t lattice_x,
                           std::int64_t lattice_y, Direction direction) {
    for (SparseTile& tile : board.tiles_) {
      if (tile.key.layer != layer) {
        continue;
      }
      for (CompiledNode& node : tile.nodes) {
        const LatticeIndex index = GlobalLatticeIndex(board.profile_, tile.key, node.local_index);
        if (index.x == lattice_x && index.y == lattice_y) {
          node.legal_edges |= MaskFor(direction);
          return true;
        }
      }
    }
    return false;
  }

  static void CorruptProfileFingerprint(CompiledBoard& board) {
    ++board.compiler_profile_fingerprint_;
  }

  static void CorruptRuleBucketIdentity(CompiledBoard& board) { ++board.rule_bucket_.identity; }
};

}  // namespace apgar::geometry_compiler

#endif  // APGAR_TESTS_SUPPORT_COMPILED_BOARD_TEST_ACCESS_H_
