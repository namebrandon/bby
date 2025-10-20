#include "bench.h"
#include "board.h"
#include "uci.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string>* g_output_sink = nullptr;

void capture_output(std::string_view line) {
  if (g_output_sink) {
    g_output_sink->emplace_back(line);
  }
}

}  // namespace

namespace bby {
void uci_fuzz_feed(std::string_view payload);
}

namespace bby::test {

TEST_CASE("Bench suite exposes 12 valid FENs", "[uci][bench]") {
  REQUIRE(kBenchFens.size() == 12);
  for (const auto fen : kBenchFens) {
    REQUIRE_NOTHROW(Position::from_fen(fen, true));
  }
}

TEST_CASE("repropack emits deterministic metadata", "[uci][repro]") {
  std::vector<std::string> lines;
  g_output_sink = &lines;
  set_uci_writer(&capture_output);

  const std::string script = "uci\nisready\nposition startpos\nrepropack\n";
  uci_fuzz_feed(script);

  set_uci_writer(nullptr);
  g_output_sink = nullptr;

  const auto repro = std::find_if(lines.begin(), lines.end(), [](const std::string& line) {
    return line.rfind("info string repro ", 0) == 0;
  });

  REQUIRE(repro != lines.end());
  REQUIRE(repro->find("options=Threads:1,Hash:128") != std::string::npos);
  REQUIRE(repro->find("rng_seed=0x") != std::string::npos);
  REQUIRE(repro->find("fen=") != std::string::npos);
}

}  // namespace bby::test
