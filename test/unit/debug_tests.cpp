#include "debug.h"

#include <catch2/catch_test_macros.hpp>

namespace bby::test {

TEST_CASE("Trace topics toggle correctly", "[debug]") {
  auto topic = trace_topic_from_string("search");
  REQUIRE(topic.has_value());
  set_trace_topic(*topic, true);
  REQUIRE(trace_enabled(*topic));
  set_trace_topic(*topic, false);
  REQUIRE_FALSE(trace_enabled(*topic));
}

TEST_CASE("Position validation detects missing king", "[debug]") {
  Position pos = Position::from_fen("8/8/8/8/8/8/8/8 w - - 0 1", false);
  InvariantStatus status = validate_position(pos);
  REQUIRE_FALSE(status.ok);
}

}  // namespace bby::test
