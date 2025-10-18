#include "timeman.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("compute_time_budget derives soft and hard limits", "[timeman]") {
  Limits limits;
  limits.movetime_ms = 500;
  const auto budget = compute_time_budget(limits, Color::White);
  REQUIRE(budget.soft_ms > 0);
  REQUIRE(budget.hard_ms >= budget.soft_ms);
}

}  // namespace bby::test
