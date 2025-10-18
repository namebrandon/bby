#include "timeman.h"

namespace bby {

TimeBudget compute_time_budget(const Limits& limits, Color) {
  TimeBudget budget;
  const auto base = limits.movetime_ms > 0 ? limits.movetime_ms : 1000;
  budget.soft_ms = base;
  budget.hard_ms = base + 100;
  return budget;
}

}  // namespace bby
