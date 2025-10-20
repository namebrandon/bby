#pragma once
// moveorder.h -- Move ordering heuristics and static exchange evaluation.
// Keeps pure helpers that score moves prior to search traversal.

#include <array>

#include "board.h"
#include "hash.h"

namespace bby {

struct HistoryTable {
  static constexpr std::size_t kStride = 64 * 64;
  std::array<int, 2 * kStride> values{};

  [[nodiscard]] int get(Color color, Move move) const;
  void add(Color color, Move move, int delta);

private:
  [[nodiscard]] static std::size_t index(Color color, Move move);
};

struct OrderingContext {
  const Position* pos{nullptr};
  const TTEntry* tt{nullptr};
  const HistoryTable* history{nullptr};
  std::array<Move, 2> killers{};
  int ply{0};
};

void score_moves(MoveList& ml, const OrderingContext& ctx);
int see(const Position& pos, Move m);

}  // namespace bby
