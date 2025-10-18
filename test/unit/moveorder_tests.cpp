#include "moveorder.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace bby::test {

constexpr std::string_view kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

TEST_CASE("score_moves leaves legal move list intact", "[moveorder]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() == 20);
  OrderingContext ctx{&pos, nullptr, 0};
  score_moves(moves, ctx);
  REQUIRE(moves.size() == 20);
}

TEST_CASE("SEE estimates material gain", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", true);
  Move capture = make_move(Square::E4, Square::D5, MoveFlag::Capture);
  REQUIRE(see(pos, capture) > 0);
  Move quiet = make_move(Square::E4, Square::E5, MoveFlag::Quiet);
  REQUIRE(see(pos, quiet) <= 0);
}

}  // namespace bby::test
