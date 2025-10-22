#pragma once

/**
 * @file search_stack.h
 * @brief Per-ply bookkeeping for the searcher.
 *
 * Maintains lightweight metadata for each ply of the search, including static
 * evaluation trends and capture context, enabling improving-aware pruning
 * gates. All operations run in O(1) time per ply. Frames are reused across
 * calls; callers must reset between searches.
 */

#include <array>

#include "common.h"

namespace bby {

class SearchStack {
public:
  struct Frame {
    Move parent_move{};
    PieceType captured{PieceType::None};
    Score static_eval{0};
    Score previous_static_eval{0};
    bool has_static_eval{false};
    bool has_previous_eval{false};
    bool improving{false};
  };

  SearchStack();

  void reset();

  Frame& frame(int ply);
  const Frame& frame(int ply) const;

  void prepare_root();

  void prepare_child(int parent_ply, int child_ply, Move move, PieceType captured);

  void set_static_eval(int ply, Score eval);

  bool is_improving(int ply) const;

private:
  std::array<Frame, kMaxPly> frames_;
};

}  // namespace bby

