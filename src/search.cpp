#include "search.h"

namespace bby {

SearchResult search(Position& root, const Limits& limits) {
  (void)limits;
  SearchResult result;
  MoveList moves;
  root.generate_moves(moves, GenStage::All);
  if (moves.size() > 0) {
    result.best = moves[0];
    result.pv.line.push_back(result.best);
  }
  result.depth = 1;
  result.nodes = static_cast<std::int64_t>(moves.size());
  return result;
}

}  // namespace bby
