#include "bench.h"
#include "board.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("Bench suite exposes 50 valid FENs", "[uci][bench]") {
  REQUIRE(kBenchFens.size() == 50);
  for (const auto fen : kBenchFens) {
    REQUIRE_NOTHROW(Position::from_fen(fen, true));
  }
}

}  // namespace bby::test
