#pragma once
// moveorder.h -- Move ordering heuristics and static exchange evaluation.
// Keeps pure helpers that score moves prior to search traversal.

#include "board.h"
#include "hash.h"

namespace bby {

struct OrderingContext {
  const Position* pos{nullptr};
  const TTEntry* tt{nullptr};
  int ply{0};
};

void score_moves(MoveList& ml, const OrderingContext& ctx);
int see(const Position& pos, Move m);

}  // namespace bby
