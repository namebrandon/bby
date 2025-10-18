#include "eval.h"

#include <catch2/catch_test_macros.hpp>
#include <string_view>

namespace bby::test {

TEST_CASE("Evaluate stub populates trace", "[eval]") {
  constexpr std::string_view start_fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto pos = Position::from_fen(start_fen, false);
  EvalTrace trace;
  const auto score = evaluate(pos, &trace);
  REQUIRE(score == trace.midgame);
  REQUIRE(trace.midgame == trace.endgame);
}

}  // namespace bby::test
