#include "perft.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("Perft start position small depths", "[perft]") {
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", true);
  REQUIRE(perft(pos, 1) == 20ULL);
  REQUIRE(perft(pos, 2) == 400ULL);
  REQUIRE(perft(pos, 3) == 8902ULL);
}

}  // namespace bby::test
