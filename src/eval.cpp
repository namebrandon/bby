#include "eval.h"

#include <sstream>

#include "debug.h"

namespace bby {

Score evaluate(const Position& pos, EvalTrace* trace) {
  const bool in_check = pos.in_check(pos.side_to_move());
  const Score midgame = in_check ? -20 : 10;
  const Score endgame = midgame;

  if (trace) {
    trace->midgame = midgame;
    trace->endgame = endgame;
  }

  if (trace_enabled(TraceTopic::Eval)) {
    std::ostringstream oss;
    oss << "stm=" << (pos.side_to_move() == Color::White ? "white" : "black")
        << " check=" << (in_check ? "yes" : "no")
        << " mid=" << midgame
        << " end=" << endgame;
    trace_emit(TraceTopic::Eval, oss.str());
  }

  return midgame;
}

}  // namespace bby
