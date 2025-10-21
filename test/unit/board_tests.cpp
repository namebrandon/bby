#include "board.h"

#include <algorithm>
#include <vector>

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

namespace {
bool is_capture_like(Move move) {
  const MoveFlag flag = move_flag(move);
  return flag == MoveFlag::Capture || flag == MoveFlag::PromotionCapture ||
         flag == MoveFlag::EnPassant;
}
}  // namespace

TEST_CASE("capture and quiet stages partition pseudo moves", "[board]") {
  constexpr std::string_view kFen =
      "rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 3";
  Position pos = Position::from_fen(kFen, true);

  MoveList all_moves;
  pos.generate_moves(all_moves, GenStage::All);
  REQUIRE(all_moves.size() > 0);

  MoveList capture_moves;
  pos.generate_moves(capture_moves, GenStage::Captures);

  MoveList quiet_moves;
  pos.generate_moves(quiet_moves, GenStage::Quiets);

  std::vector<Move> expected_captures;
  for (const Move m : all_moves) {
    if (is_capture_like(m)) {
      expected_captures.push_back(m);
    }
  }
  std::sort(expected_captures.begin(), expected_captures.end(),
            [](Move lhs, Move rhs) { return lhs.value < rhs.value; });

  std::vector<Move> actual_captures(capture_moves.begin(), capture_moves.end());
  std::sort(actual_captures.begin(), actual_captures.end(),
            [](Move lhs, Move rhs) { return lhs.value < rhs.value; });
  REQUIRE(actual_captures == expected_captures);

  for (const Move m : capture_moves) {
    REQUIRE(is_capture_like(m));
  }
  for (const Move m : quiet_moves) {
    REQUIRE_FALSE(is_capture_like(m));
  }
}

}  // namespace bby::test
