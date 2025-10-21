#include "timeman.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("compute_time_budget derives soft and hard limits", "[timeman]") {
  Limits limits;
  limits.movetime_ms = 500;
  const auto budget = compute_time_budget(limits, Color::White);
  REQUIRE(budget.soft_ms == 500);
  REQUIRE(budget.hard_ms == 550);
}

TEST_CASE("compute_time_budget uses remaining clock and increment", "[timeman]") {
  Limits limits;
  limits.wtime_ms = 60'000;
  limits.winc_ms = 1'000;
  limits.movetime_ms = -1;
  const auto budget = compute_time_budget(limits, Color::White);
  REQUIRE(budget.soft_ms == 3'500);
  REQUIRE(budget.hard_ms == 3'550);
}

TEST_CASE("compute_time_budget respects minimum move time", "[timeman]") {
  Limits limits;
  limits.btime_ms = 80;
  limits.binc_ms = 0;
  limits.movetime_ms = -1;
  const auto budget = compute_time_budget(limits, Color::Black);
  REQUIRE(budget.soft_ms >= 10);
  REQUIRE(budget.hard_ms >= budget.soft_ms);
}

}  // namespace bby::test
