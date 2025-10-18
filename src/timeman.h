#pragma once
// timeman.h -- Time allocation policies for search iteration budgeting.
// Pure helpers translating Limits into soft/hard cutoffs.

#include "common.h"
#include "searchparams.h"

namespace bby {

struct TimeBudget {
  std::int64_t soft_ms{0};
  std::int64_t hard_ms{0};
};

TimeBudget compute_time_budget(const Limits& limits, Color stm);

}  // namespace bby
