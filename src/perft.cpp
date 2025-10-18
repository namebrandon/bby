#include "perft.h"

namespace bby {

namespace {

std::uint64_t perft_inner(Position& pos, int depth) {
  if (depth == 0) {
    return 1ULL;
  }

  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  std::uint64_t nodes = 0;

  for (const Move move : moves) {
    Undo undo;
    pos.make(move, undo);
    nodes += perft_inner(pos, depth - 1);
    pos.unmake(move, undo);
  }

  return nodes;
}

}  // namespace

std::uint64_t perft(Position& pos, int depth) {
  return perft_inner(pos, depth);
}

}  // namespace bby
