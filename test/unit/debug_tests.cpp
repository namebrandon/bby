#include "debug.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

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

namespace {

std::vector<std::string>* g_trace_sink = nullptr;

void test_trace_writer(TraceTopic, std::string_view payload) {
  if (g_trace_sink) {
    g_trace_sink->emplace_back(payload);
  }
}

}  // namespace

TEST_CASE("Custom trace writer captures payloads", "[debug]") {
  set_trace_topic(TraceTopic::Search, true);
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&test_trace_writer);

  trace_emit(TraceTopic::Search, "hello");
  trace_emit(TraceTopic::Eval, "ignored");

  set_trace_topic(TraceTopic::Search, false);
  trace_emit(TraceTopic::Search, "suppressed");

  set_trace_writer(nullptr);
  g_trace_sink = nullptr;

  REQUIRE(payloads.size() == 1);
  REQUIRE(payloads.front() == "trace search hello");
}

}  // namespace bby::test
