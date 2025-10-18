#pragma once
// eval.h -- Lightweight evaluation terms (PeSTO baseline) and tracing support.
// Provides a stable API for the search to request tapered scores.

#include "board.h"

namespace bby {

struct EvalTrace {
  Score midgame{0};
  Score endgame{0};
};

Score evaluate(const Position& pos, EvalTrace* trace = nullptr);

}  // namespace bby
