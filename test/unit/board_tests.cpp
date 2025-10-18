#include "board.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace bby::test {

constexpr std::string_view kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

TEST_CASE("Start position generates 20 legal moves", "[board]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() == 20);
  REQUIRE_FALSE(pos.in_check(Color::White));
  REQUIRE_FALSE(pos.in_check(Color::Black));
}

TEST_CASE("Make/unmake restores original state", "[board]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() > 0);
  const Move first = moves[0];
  const std::string fen_before = pos.to_fen();
  Undo undo;
  pos.make(first, undo);
  pos.unmake(first, undo);
  REQUIRE(pos.to_fen() == fen_before);
}

TEST_CASE("FEN round-trip maintains state", "[board]") {
  constexpr std::string_view custom_fen =
      "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 3 4";
  Position pos = Position::from_fen(custom_fen, true);
  REQUIRE(pos.to_fen() == custom_fen);
}

TEST_CASE("move_to_uci emits coordinate moves", "[board]") {
  const Move move = make_move(Square::E7, Square::E8, MoveFlag::Promotion, PieceType::Queen);
  REQUIRE(move_to_uci(move) == "e7e8q");
  const Move quiet = make_move(Square::B1, Square::C3);
  REQUIRE(move_to_uci(quiet) == "b1c3");
}

}  // namespace bby::test
