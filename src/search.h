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
};

SearchResult search(Position& root, const Limits& limits);

}  // namespace bby
