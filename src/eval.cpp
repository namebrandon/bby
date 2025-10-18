#include "eval.h"

namespace bby {

Score evaluate(const Position& pos, EvalTrace* trace) {
  const Score base = pos.in_check(Color::White) ? 0 : 10;
  if (trace) {
    trace->midgame = base;
    trace->endgame = base;
  }
  return base;
}

}  // namespace bby
