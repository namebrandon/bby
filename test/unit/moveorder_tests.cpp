#include "moveorder.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace bby::test {

constexpr std::string_view kStartFen =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

OrderingContext make_ctx(Position& pos) {
  OrderingContext ctx{};
  ctx.pos = &pos;
  return ctx;
}

TEST_CASE("score_moves leaves legal move list intact", "[moveorder]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() == 20);
  auto ctx = make_ctx(pos);
  score_moves(moves, ctx);
  REQUIRE(moves.size() == 20);
}

TEST_CASE("score_moves prioritizes TT move", "[moveorder]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() == 20);
  const Move tt_move = make_move(Square::G1, Square::F3);
  REQUIRE([&] {
    for (const Move m : moves) {
      if (m == tt_move) {
        return true;
      }
    }
    return false;
  }());

  TTEntry tt_entry{};
  tt_entry.best_move = tt_move;
  OrderingContext ctx{};
  ctx.pos = &pos;
  ctx.tt = &tt_entry;
  score_moves(moves, ctx);
  REQUIRE(moves[0] == tt_move);
}

TEST_CASE("score_moves ranks higher value captures first", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/4n3/3p4/4Q3/8/8/4K3 w - - 0 1", true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  Move best_capture{};
  Move worse_capture{};
  for (std::size_t idx = 0; idx < moves.size(); ++idx) {
    const Move m = moves[idx];
    if (move_flag(m) == MoveFlag::Capture) {
      if (to_square(m) == Square::E6) {
        best_capture = m;  // Qxe6 capturing knight
      } else if (to_square(m) == Square::D5) {
        worse_capture = m;  // Qxd5 capturing pawn
      }
    }
  }
  REQUIRE(!best_capture.is_null());
  REQUIRE(!worse_capture.is_null());

  OrderingContext ctx{};
  ctx.pos = &pos;
  score_moves(moves, ctx);
  REQUIRE(moves[0] == best_capture);
}

TEST_CASE("score_moves boosts killer and history moves", "[moveorder]") {
  Position pos = Position::from_fen(kStartFen, true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() > 0);

  HistoryTable history{};
  const Move killer = make_move(Square::E2, Square::E4, MoveFlag::DoublePush);
  REQUIRE([&] {
    for (const Move m : moves) {
      if (m == killer) {
        return true;
      }
    }
    return false;
  }());
  const std::size_t idx = HistoryTable::kStride * color_index(Color::White) +
                          static_cast<std::size_t>(from_square(killer)) * 64 +
                          static_cast<std::size_t>(to_square(killer));
  history.values[idx] = 500;

  OrderingContext ctx{};
  ctx.pos = &pos;
  ctx.history = &history;
  ctx.killers[0] = killer;

  score_moves(moves, ctx);
  REQUIRE(moves[0] == killer);
}

TEST_CASE("SEE estimates material gain", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", true);
  Move capture = make_move(Square::E4, Square::D5, MoveFlag::Capture);
  REQUIRE(see(pos, capture) > 0);
  Move quiet = make_move(Square::E4, Square::E5, MoveFlag::Quiet);
  REQUIRE(see(pos, quiet) <= 0);
}

}  // namespace bby::test
