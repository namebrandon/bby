#pragma once
/// @file eval.h
/// @brief PeSTO-based tapered evaluation and optional tracing hooks.
/// @details Produces deterministic midgame/endgame scores blended by phase.

#include "board.h"

namespace bby {

struct EvalTrace {
  Score midgame{0};
  Score endgame{0};
  Score blended{0};
  int phase{0};
};

Score evaluate(const Position& pos, EvalTrace* trace = nullptr);

}  // namespace bby
