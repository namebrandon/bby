#include "epd.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace bby::test {

TEST_CASE("parse_epd_line extracts operations and ignores counters", "[epd]") {
  const std::string line =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 bm e2e4; id \"start\";";
  EpdRecord record;
  std::string error;
  REQUIRE(parse_epd_line(line, record, error));
  REQUIRE(record.operations.at("bm") == "e2e4");
  REQUIRE(record.operations.at("id") == "\"start\"");
  MoveList moves;
  record.position.generate_moves(moves, GenStage::All);
  REQUIRE(moves.size() == 20);
}

TEST_CASE("parse_epd_line handles quoted semicolons", "[epd]") {
  const std::string line =
      "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - "
      "pv e2e4 e7e5; c0 \"foo;bar\";";
  EpdRecord record;
  std::string error;
  REQUIRE(parse_epd_line(line, record, error));
  REQUIRE(record.operations.at("pv") == "e2e4 e7e5");
  REQUIRE(record.operations.at("c0") == "\"foo;bar\"");
}

TEST_CASE("parse_epd_line rejects malformed records", "[epd]") {
  const std::string line = "invalid epd line";
  EpdRecord record;
  std::string error;
  REQUIRE_FALSE(parse_epd_line(line, record, error));
  REQUIRE_FALSE(error.empty());
}

TEST_CASE("load_epd_file aggregates parse errors", "[epd]") {
  const auto temp_path = std::filesystem::temp_directory_path() / "bby-epd-tests.epd";
  std::error_code ec;
  std::filesystem::remove(temp_path, ec);
  {
    std::ofstream out(temp_path);
    REQUIRE(out.good());
    out << "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - id \"a\";\n";
    out << "bad line missing fields\n";
    out << "r3k2r/p1ppqpb1/bn2pnp1/2P5/1p2P3/2N2Q1p/PPPB1PPP/R3K2R w KQkq - "
           "am g2xh3;\n";
  }

  const auto result = load_epd_file(temp_path.string());
  REQUIRE(result.records.size() == 2);
  REQUIRE(result.errors.size() == 1);
  REQUIRE(result.errors.front().line == 2);
  REQUIRE_FALSE(result.errors.front().message.empty());

  std::filesystem::remove(temp_path, ec);
}

TEST_CASE("WAC EPD exposes best-move operations", "[epd][wac]") {
  const auto base = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  const auto path = base / "tests/positions/wacnew.epd";
  const auto result = load_epd_file(path.string());
  REQUIRE(result.ok());
  REQUIRE(result.errors.empty());
  REQUIRE(result.records.size() == 20);
  for (const auto& record : result.records) {
    INFO(record.position.to_fen());
    REQUIRE(record.operations.contains("bm"));
    REQUIRE_FALSE(record.operations.at("bm").empty());
  }
}

}  // namespace bby::test
