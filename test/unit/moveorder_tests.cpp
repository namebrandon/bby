#include "moveorder.h"

#include <array>
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
  std::array<int, kMaxMoves> scores{};
  score_moves(moves, ctx, scores);
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
  std::array<int, kMaxMoves> scores{};
  score_moves(moves, ctx, scores);
  select_best_move(moves, scores, 0, moves.size());
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
  std::array<int, kMaxMoves> scores{};
  score_moves(moves, ctx, scores);
  select_best_move(moves, scores, 0, moves.size());
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

  std::array<int, kMaxMoves> scores{};
  score_moves(moves, ctx, scores);
  select_best_move(moves, scores, 0, moves.size());
  REQUIRE(moves[0] == killer);
}

TEST_CASE("SEE estimates material gain", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/8/3n4/4P3/8/8/4K3 w - - 0 1", true);
  Move capture = make_move(Square::E4, Square::D5, MoveFlag::Capture);
  REQUIRE(see(pos, capture) > 0);
  Move quiet = make_move(Square::E4, Square::E5, MoveFlag::Quiet);
  REQUIRE(see(pos, quiet) <= 0);
}

TEST_CASE("SEE identifies losing queen grab", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/4p3/3p4/4Q3/8/8/4K3 w - - 0 1", true);
  Move capture = make_move(Square::E4, Square::D5, MoveFlag::Capture);
  REQUIRE(see(pos, capture) < 0);
}

TEST_CASE("SEE handles en passant captures", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", true);
  Move ep = make_move(Square::E5, Square::D6, MoveFlag::EnPassant);
  REQUIRE(see(pos, ep) > 0);
}

TEST_CASE("SEE rewards promotion captures", "[moveorder]") {
  Position pos = Position::from_fen("4k2r/6P1/8/8/8/8/8/4K3 w - - 0 1", true);
 Move promo = make_move(Square::G7, Square::H8, MoveFlag::PromotionCapture, PieceType::Queen);
  REQUIRE(see(pos, promo) > 0);
}

TEST_CASE("SeeCache reuses stored results", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/4p3/3p4/4Q3/8/8/4K3 w - - 0 1", true);
  Move move = make_move(Square::E4, Square::D5, MoveFlag::Capture);
  SeeCache cache{};
  cache.clear();
  const int computed = cached_see(pos, move, &cache);
  REQUIRE(computed == see(pos, move));
  int cached_value = 0;
  REQUIRE(cache.probe(pos.zobrist(), move, cached_value));
  REQUIRE(cached_value == computed);
  const int reused = cached_see(pos, move, &cache);
  REQUIRE(reused == computed);
}

TEST_CASE("score_moves leaves SEE deferred for favorable trades", "[moveorder]") {
  Position pos = Position::from_fen("4k3/8/3r4/4P3/8/8/8/4K3 w - - 0 1", true);
  MoveList moves;
  pos.generate_moves(moves, GenStage::Captures);
  OrderingContext ctx{};
  ctx.pos = &pos;
  SeeCache cache{};
  cache.clear();
  ctx.see_cache = &cache;
  std::array<int, kMaxMoves> scores{};
  std::array<int, kMaxMoves> see_scores{};
  score_moves(moves, ctx, scores, &see_scores);
  const Move pawn_takes_rook = make_move(Square::E5, Square::D6, MoveFlag::Capture);
  bool found = false;
  int see_entry = 0;
  for (std::size_t idx = 0; idx < moves.size(); ++idx) {
    if (moves[idx] == pawn_takes_rook) {
      found = true;
      see_entry = see_scores[idx];
      break;
    }
  }
  REQUIRE(found);
  REQUIRE(see_entry > 0);
}

}  // namespace bby::test
