#pragma once
// search.h -- Principal variation search driver and shared search result struct.
// Provides the public entry point used by the UCI front-end and tools.

#include <vector>

#include "board.h"
#include "eval.h"
#include "hash.h"
#include "moveorder.h"
#include "searchparams.h"
#include "timeman.h"

namespace bby {

struct PV {
  std::vector<Move> line;
};

struct SearchResult {
  Move best;
  PV pv;
  int depth{0};
  std::int64_t nodes{0};
  Score eval{0};
  bool tt_hit{false};
  Move primary_killer{};
  int history_bonus{0};
  bool aborted{false};
};

SearchResult search(Position& root, const Limits& limits);
void set_singular_margin(int margin);
int singular_margin();

}  // namespace bby
