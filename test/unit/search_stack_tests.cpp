#include "search_stack.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("SearchStack resets frames and records context", "[search][stack]") {
  SearchStack stack;
  stack.prepare_root();

  auto& root = stack.frame(0);
  REQUIRE_FALSE(root.has_static_eval);

  stack.set_static_eval(0, 25);
  CHECK(root.has_static_eval);
  CHECK(stack.is_improving(0) == false);

  const Move move = make_move(Square::E2, Square::E4);
  stack.prepare_child(0, 1, move, PieceType::Pawn);

  const auto& child = stack.frame(1);
  CHECK(child.parent_move == move);
  CHECK(child.captured == PieceType::Pawn);
  CHECK_FALSE(child.has_static_eval);
  CHECK_FALSE(stack.is_improving(1));
}

TEST_CASE("SearchStack marks improving trend across plies", "[search][stack]") {
  SearchStack stack;
  stack.prepare_root();
  stack.set_static_eval(0, 10);

  stack.prepare_child(0, 1, Move{}, PieceType::None);
  stack.set_static_eval(1, -5);

  stack.prepare_child(1, 2, Move{}, PieceType::None);
  stack.set_static_eval(2, 50);
  CHECK(stack.is_improving(2));

  stack.prepare_child(1, 2, Move{}, PieceType::None);
  stack.set_static_eval(2, -200);
  CHECK_FALSE(stack.is_improving(2));
}

}  // namespace bby::test

