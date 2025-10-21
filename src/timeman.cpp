#include "timeman.h"

#include <algorithm>

namespace {

constexpr std::int64_t kSafetyMarginMs = 50;
constexpr std::int64_t kMinMoveTimeMs = 10;
constexpr std::int64_t kHardSlackMs = 50;
constexpr std::int64_t kDefaultMoveTimeMs = 1000;

}  // namespace

namespace bby {

TimeBudget compute_time_budget(const Limits& limits, Color stm) {
  TimeBudget budget{};

  if (limits.infinite) {
    return budget;
  }

  if (limits.movetime_ms >= 0) {
    const std::int64_t move_time =
        std::max<std::int64_t>(limits.movetime_ms, kMinMoveTimeMs);
    budget.soft_ms = move_time;
    budget.hard_ms = move_time + kHardSlackMs;
    return budget;
  }

  const std::int64_t time_left =
      (stm == Color::White) ? limits.wtime_ms : limits.btime_ms;
  const std::int64_t increment =
      (stm == Color::White) ? limits.winc_ms : limits.binc_ms;

  const bool have_clock = time_left >= 0;
  const bool have_increment = increment > 0;

  if (!have_clock && !have_increment) {
    return budget;
  }

  if (!have_clock) {
    if (increment > 0) {
      const std::int64_t alloc =
          std::max<std::int64_t>(increment / 2, kMinMoveTimeMs);
      budget.soft_ms = alloc;
      budget.hard_ms = alloc + kHardSlackMs;
      return budget;
    }
    return budget;
  }

  const int divisor = limits.movestogo > 0 ? limits.movestogo : 20;
  const std::int64_t base_time = std::max<std::int64_t>(time_left /
                                                        std::max(divisor, 1),
                                                        0);
  const std::int64_t inc_time = std::max<std::int64_t>(increment / 2, 0);
  std::int64_t allocate = base_time + inc_time;

  const std::int64_t safety_margin =
      std::min<std::int64_t>(kSafetyMarginMs, std::max<std::int64_t>(time_left / 10, 0));
  std::int64_t max_allowed = time_left;
  if (time_left > safety_margin) {
    max_allowed = time_left - safety_margin;
  }
  allocate = std::min(allocate, max_allowed);
  if (allocate < kMinMoveTimeMs) {
    allocate = std::min<std::int64_t>(max_allowed,
                                      std::max<std::int64_t>(kMinMoveTimeMs, 0));
  }
  allocate = std::clamp<std::int64_t>(allocate, 0, time_left);

  budget.soft_ms = allocate;
  const std::int64_t hard_cap =
      std::max<std::int64_t>(allocate + kHardSlackMs, allocate);
  budget.hard_ms = std::min<std::int64_t>(time_left, hard_cap);
  if (budget.hard_ms < budget.soft_ms) {
    budget.hard_ms = budget.soft_ms;
  }

  return budget;
}

}  // namespace bby
