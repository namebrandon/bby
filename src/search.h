#pragma once
// search.h -- Principal variation search driver and shared search result struct.
// Provides the public entry point used by the UCI front-end and tools.

#include <atomic>
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

struct PVLine {
  Move best{};
  PV pv;
  Score eval{0};
};

struct SearchResult {
  Move best;
  PV pv;
  std::vector<PVLine> lines;
  int depth{0};
  std::int64_t nodes{0};
  Score eval{0};
  int static_futility_prunes{0};
  int razor_prunes{0};
  int multi_cut_prunes{0};
  std::int64_t elapsed_ms{0};
  bool tt_hit{false};
  Move primary_killer{};
  int history_bonus{0};
  bool aborted{false};
};

SearchResult search(Position& root, const Limits& limits,
                    std::atomic<bool>* stop_flag = nullptr);
void set_singular_margin(int margin);
int singular_margin();

}  // namespace bby
