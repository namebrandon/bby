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

TEST_CASE("Quiescence search resolves horizon captures", "[search][qsearch]") {
  Position pos = Position::from_fen("4k3/8/8/4q3/4Q3/8/8/4K3 w - - 0 1", false);
  Limits limits;
  limits.depth = 0;

  const auto result = search(pos, limits);
  REQUIRE_FALSE(result.best.is_null());
  REQUIRE(move_to_uci(result.best) == std::string("e4e5"));
  REQUIRE(result.eval > 0);
}

TEST_CASE("Search emits PVS narrow trace for secondary moves", "[search][pvs]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  set_trace_topic(TraceTopic::Search, true);
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 2;

  const auto result = search(pos, limits);
  REQUIRE(result.depth == 2);

  const auto narrow_it = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("pvs narrow") != std::string::npos;
  });
  REQUIRE(narrow_it != payloads.end());

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

TEST_CASE("Search re-searches when a move improves alpha within the PV window", "[search][pvs]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  set_trace_topic(TraceTopic::Search, true);
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 2;

  const auto result = search(pos, limits);
  REQUIRE(result.depth == 2);

  const auto research_it = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("pvs research") != std::string::npos;
  });
  REQUIRE(research_it != payloads.end());

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

TEST_CASE("Search aspiration windows expand when bounds fail", "[search][aspiration]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  set_trace_topic(TraceTopic::Search, true);
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 4;

  const auto result = search(pos, limits);
  REQUIRE(result.depth == 4);

  const auto fail_low = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("aspiration fail-low") != std::string::npos;
  });
  const auto fail_high = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("aspiration fail-high") != std::string::npos;
  });
  REQUIRE(fail_low != payloads.end());
  REQUIRE(fail_high != payloads.end());

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

TEST_CASE("Search returns multiple PV lines when MultiPV requested", "[search][multipv]") {
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 2;
  limits.multipv = 2;

  const auto result = search(pos, limits);
  REQUIRE(result.lines.size() >= 2);
  REQUIRE_FALSE(result.lines[0].best.is_null());
  REQUIRE_FALSE(result.lines[1].best.is_null());
  REQUIRE(result.lines[0].best != result.lines[1].best);
}

TEST_CASE("Search applies late move reductions on quiet moves", "[search][lmr]") {
  std::vector<std::string> payloads;
  g_trace_sink = &payloads;
  set_trace_writer(&capture_trace);

  set_trace_topic(TraceTopic::Search, true);
  Position pos = Position::from_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", false);
  Limits limits;
  limits.depth = 3;

  const auto result = search(pos, limits);
  REQUIRE(result.depth >= 2);

  const auto lmr_hit = std::find_if(payloads.begin(), payloads.end(), [](const std::string& payload) {
    return payload.find("lmr reduce") != std::string::npos;
  });
  REQUIRE(lmr_hit != payloads.end());

  set_trace_topic(TraceTopic::Search, false);
  set_trace_writer(nullptr);
  g_trace_sink = nullptr;
}

}  // namespace bby::test
