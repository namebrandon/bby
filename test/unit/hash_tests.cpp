#include "hash.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("TT stores and probes entries", "[hash]") {
  TT table(1);
  TTEntry entry{};
  entry.score = 42;
  table.store(1234ULL, entry);

  TTEntry out{};
  REQUIRE(table.probe(1234ULL, out));
  REQUIRE(out.score == 42);
}

}  // namespace bby::test
