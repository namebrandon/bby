#include "hash.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("TT stores and probes entries", "[hash]") {
  TT table(0);
  table.set_generation(7);

  TTEntry entry{};
  entry.score = 42;
  entry.depth = 8;
  entry.bound = BoundType::Exact;
  entry.best_move = make_move(Square::E2, Square::E4);
  table.store(1234ULL, entry);

  TTEntry out{};
  REQUIRE(table.probe(1234ULL, out));
  REQUIRE(out.score == 42);
  REQUIRE(out.depth == 8);
  REQUIRE(out.best_move == make_move(Square::E2, Square::E4));
  REQUIRE(out.bound == BoundType::Exact);
  REQUIRE(out.generation == 7);
}

TEST_CASE("TT replacement prefers oldest generation then shallow depth", "[hash]") {
  TT table(0);
  TTEntry entry{};
  for (int idx = 0; idx < static_cast<int>(TT::kBucketSize); ++idx) {
    entry.key = 0;
    entry.depth = static_cast<std::uint8_t>(10 - idx);
    entry.score = idx;
    entry.bound = BoundType::Lower;
    table.set_generation(0);
    table.store(static_cast<std::uint64_t>(idx + 1), entry);
  }

  // Entries now in generation 0; advance generation to age existing slots.
  table.set_generation(1);
  TTEntry newcomer{};
  newcomer.depth = 12;
  newcomer.score = 99;
  newcomer.bound = BoundType::Upper;
  table.store(999ULL, newcomer);

  TTEntry out{};
  REQUIRE(table.probe(999ULL, out));
  REQUIRE(out.score == 99);
  REQUIRE(out.depth == 12);
  REQUIRE(out.generation == 1);

  // Oldest entry should have been evicted; depth 10 survives, depth 7 (key 4) replaced.
  TTEntry survivor{};
  bool found_evicted = !table.probe(4ULL, survivor);
  REQUIRE(found_evicted);
}

TEST_CASE("TT updates entry in place when key matches", "[hash]") {
  TT table(0);
  TTEntry entry{};
  entry.score = 5;
  entry.depth = 6;
  table.set_generation(2);
  table.store(111ULL, entry);

  TTEntry update{};
  update.score = 99;
  update.depth = 12;
  update.bound = BoundType::Upper;
  table.set_generation(3);
  table.store(111ULL, update);

  TTEntry out{};
  REQUIRE(table.probe(111ULL, out));
  REQUIRE(out.score == 99);
  REQUIRE(out.depth == 12);
  REQUIRE(out.bound == BoundType::Upper);
  REQUIRE(out.generation == 3);
}

}  // namespace bby::test
