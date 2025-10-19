#include "debug.h"
#include "search.h"
#include "searchparams.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {

std::vector<std::string>* g_trace_sink = nullptr;

void capture_trace(bby::TraceTopic, std::string_view payload) {
  if (g_trace_sink) {
    g_trace_sink->emplace_back(payload);
  }
}

}  // namespace

namespace bby::test {

TEST_CASE("Search trace toggles respect topic flag", "[search][trace]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  Position base = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 1;

  set_trace_topic(TraceTopic::Search, false);
  {
    Position pos = base;
    search(pos, limits);
  }
  REQUIRE(payloads.empty());

  set_trace_topic(TraceTopic::Search, true);
  {
    Position pos = base;
    search(pos, limits);
  }
  REQUIRE(payloads.size() >= 2);
  REQUIRE(payloads.front().find("trace search start") != std::string::npos);

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

}  // namespace bby::test
