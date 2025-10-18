#include "board.h"
#include "perft.h"

#include <catch2/catch_test_macros.hpp>
#include <vector>

namespace bby::test {

TEST_CASE("Make/unmake preserves position state", "[property][undo]") {
  const std::vector<std::string> fens = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 3 4",
      "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1"};

  for (const auto& fen : fens) {
    Position pos = Position::from_fen(fen, true);
    const std::string baseline_fen = pos.to_fen();
    const std::uint64_t baseline_key = pos.zobrist();
    Position fresh = Position::from_fen(baseline_fen, true);
    REQUIRE(fresh.zobrist() == baseline_key);

    MoveList moves;
    pos.generate_moves(moves, GenStage::All);
    for (std::size_t idx = 0; idx < moves.size(); ++idx) {
      const Move move = moves[idx];
      Undo undo;
      pos.make(move, undo);
      pos.unmake(move, undo);
      REQUIRE(pos.to_fen() == baseline_fen);
      REQUIRE(pos.zobrist() == baseline_key);
    }
  }
}

TEST_CASE("Perft node counts are monotonic", "[property][perft]") {
  const std::vector<std::pair<std::string, int>> cases = {
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 3},
      {"r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - 0 1", 3}};

  for (const auto& [fen, max_depth] : cases) {
    Position base = Position::from_fen(fen, true);
    std::uint64_t previous = 0;
    for (int depth = 0; depth <= max_depth; ++depth) {
      Position copy = base;
      const std::uint64_t nodes = perft(copy, depth);
      REQUIRE(nodes >= previous);
      previous = nodes;
    }
  }
}

}  // namespace bby::test
