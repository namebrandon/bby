#include "debug.h"
#include "search.h"
#include "searchparams.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
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
    const auto result = search(pos, limits);
    REQUIRE_FALSE(result.best.is_null());
    REQUIRE(result.depth == 1);
  }
  REQUIRE(payloads.size() >= 2);
  REQUIRE(payloads.front().find("trace search start") != std::string::npos);

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

TEST_CASE("Search stores TT entries and updates quiet history", "[search][tt]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 1;

  set_trace_topic(TraceTopic::TT, true);
  const auto result = search(pos, limits);
  set_trace_topic(TraceTopic::TT, false);

  REQUIRE(result.tt_hit);
  REQUIRE((result.primary_killer.is_null() || result.primary_killer == result.best));
  REQUIRE(result.history_bonus >= 0);

  const auto store_it = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("trace tt store") != std::string::npos;
  });
  REQUIRE(store_it != payloads.end());

  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

TEST_CASE("Search prefers immediate winning capture", "[search]") {
  Position pos = Position::from_fen("4k3/8/8/4q3/4Q3/8/8/4K3 w - - 0 1", false);
  Limits limits;
  limits.depth = 1;

  const auto result = search(pos, limits);
  REQUIRE_FALSE(result.best.is_null());
  REQUIRE(move_to_uci(result.best) == std::string("e4e5"));
  REQUIRE(result.eval > 0);
  REQUIRE(result.nodes > 0);
}

}  // namespace bby::test
