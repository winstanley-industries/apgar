#ifndef APGAR_TESTS_SUPPORT_BOARD_BUILDER_H_
#define APGAR_TESTS_SUPPORT_BOARD_BUILDER_H_

#include "apgar/board_ir/board.h"

namespace apgar::test_support {

[[nodiscard]] inline board_ir::BoardData ValidM1BoardData() {
  using board_ir::AxisAlignedBox64;
  using board_ir::EntityRef;
  using board_ir::Layer;
  using board_ir::LayerType;
  using board_ir::Net;
  using board_ir::Obstacle;
  using board_ir::Point64;
  using board_ir::RoutingProfile;
  using board_ir::Terminal;

  constexpr EntityRef kTargetNet{.id = 10, .generation = 0};
  constexpr EntityRef kBlockerNet{.id = 11, .generation = 0};
  constexpr EntityRef kFirstTerminal{.id = 20, .generation = 0};
  constexpr EntityRef kSecondTerminal{.id = 21, .generation = 0};

  return board_ir::BoardData{
      .schema_version = board_ir::kBoardSchemaVersion,
      .dbu_per_millimeter = 1'000'000,
      .revision = 7,
      .adapter_name = "unit-test",
      .adapter_version = "1",
      .layers =
          {
              Layer{.ref = EntityRef{.id = 1, .generation = 0},
                    .routing_id = 0,
                    .name = "front-signal",
                    .physical_order = 0,
                    .type = LayerType::kSignal,
                    .routable = true},
              Layer{.ref = EntityRef{.id = 2, .generation = 0},
                    .routing_id = 31,
                    .name = "back-signal",
                    .physical_order = 1,
                    .type = LayerType::kSignal,
                    .routable = true},
          },
      .nets =
          {
              Net{.ref = kTargetNet,
                  .name = "TARGET",
                  .terminals = {kFirstTerminal, kSecondTerminal}},
              Net{.ref = kBlockerNet, .name = "BLOCKER", .terminals = {}},
          },
      .terminals =
          {
              Terminal{.ref = kFirstTerminal,
                       .net = kTargetNet,
                       .component = "U1",
                       .pin = "1",
                       .center = Point64{.x = 0, .y = 0},
                       .connection_region = AxisAlignedBox64{.min = Point64{.x = -10, .y = -10},
                                                             .max = Point64{.x = 10, .y = 10}},
                       .layers = {0, 31}},
              Terminal{.ref = kSecondTerminal,
                       .net = kTargetNet,
                       .component = "U2",
                       .pin = "1",
                       .center = Point64{.x = 100, .y = 0},
                       .connection_region = AxisAlignedBox64{.min = Point64{.x = 90, .y = -10},
                                                             .max = Point64{.x = 110, .y = 10}},
                       .layers = {0, 31}},
          },
      .obstacles =
          {
              Obstacle{.ref = EntityRef{.id = 30, .generation = 0},
                       .layer = 0,
                       .bounds = AxisAlignedBox64{.min = Point64{.x = 40, .y = -10},
                                                  .max = Point64{.x = 60, .y = 10}},
                       .owner_net = kBlockerNet,
                       .provenance = "U3/pad-1"},
          },
      .routing_profile =
          RoutingProfile{
              .net = kTargetNet,
              .nominal_width = 10,
              .clearance = 5,
              .allowed_layers = {0, 31},
              .allowed_headings = board_ir::kM1HeadingMask,
          },
  };
}

[[nodiscard]] inline board_ir::BoardData MultiNetM1BoardData() {
  constexpr board_ir::EntityRef kThirdTerminal{.id = 22, .generation = 0};
  constexpr board_ir::EntityRef kFourthTerminal{.id = 23, .generation = 0};
  board_ir::BoardData data = ValidM1BoardData();
  const board_ir::EntityRef second_net = data.nets[1].ref;
  data.nets[1].terminals = {kThirdTerminal, kFourthTerminal};
  data.obstacles[0].bounds =
      board_ir::AxisAlignedBox64{.min = {.x = 40, .y = 10}, .max = {.x = 60, .y = 30}};
  data.terminals.push_back(board_ir::Terminal{
      .ref = kThirdTerminal,
      .net = second_net,
      .component = "U4",
      .pin = "1",
      .center = {.x = 0, .y = 20},
      .connection_region = {.min = {.x = -10, .y = 10}, .max = {.x = 10, .y = 30}},
      .layers = {0},
  });
  data.terminals.push_back(board_ir::Terminal{
      .ref = kFourthTerminal,
      .net = second_net,
      .component = "U5",
      .pin = "1",
      .center = {.x = 100, .y = 20},
      .connection_region = {.min = {.x = 90, .y = 10}, .max = {.x = 110, .y = 30}},
      .layers = {0},
  });
  return data;
}

}  // namespace apgar::test_support

#endif  // APGAR_TESTS_SUPPORT_BOARD_BUILDER_H_
