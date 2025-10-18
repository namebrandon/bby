#include "pgn.h"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

namespace bby::test {

TEST_CASE("PGN reader parses tagged game", "[pgn]") {
  const char* kPgn = R"(
[Event "Test Match"]
[Site "Internet"]

1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 {Ruy Lopez} 4. Ba4 Nf6 1-0
)";

  std::istringstream stream(kPgn);
  PgnReader reader(stream);
  PgnGame game;
  std::string error;
  REQUIRE(reader.read_next(game, error));
  REQUIRE(error.empty());
  REQUIRE(game.tags.at("Event") == "Test Match");
  REQUIRE(game.tags.at("Site") == "Internet");
  REQUIRE(game.moves.size() == 8);
  REQUIRE(game.moves[4].comment == "Ruy Lopez");
  REQUIRE(game.result == "1-0");
  REQUIRE_FALSE(reader.read_next(game, error));
}

TEST_CASE("PGN reader ignores variations and annotations", "[pgn]") {
  const char* kPgn = R"(

1. d4 d5 (1... Nf6) 2. c4 c6 $1 3. Nc3 Nf6 1/2-1/2
)";
  std::istringstream stream(kPgn);
  PgnReader reader(stream);
  PgnGame game;
  std::string error;
  REQUIRE(reader.read_next(game, error));
  REQUIRE(game.moves.size() == 6);
  REQUIRE(game.moves.front().san == "d4");
  REQUIRE(game.moves.back().san == "Nf6");
  REQUIRE(game.result == "1/2-1/2");
}

}  // namespace bby::test
