#include "epd/epd.h"
#include "perft.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
  std::string fen;
  std::string epd_path;
  std::string suite_path;
  int depth{4};
  bool split{false};
};

Options parse(int argc, char** argv) {
  Options opt;
  opt.fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "-f" || arg == "--fen") && i + 1 < argc) {
      opt.fen = argv[++i];
    } else if ((arg == "-d" || arg == "--depth") && i + 1 < argc) {
      opt.depth = std::stoi(argv[++i]);
    } else if ((arg == "-e" || arg == "--epd") && i + 1 < argc) {
      opt.epd_path = argv[++i];
    } else if ((arg == "-s" || arg == "--suite") && i + 1 < argc) {
      opt.suite_path = argv[++i];
    } else if (arg == "--split") {
      opt.split = true;
    }
  }
  return opt;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parse(argc, argv);
  if (!options.suite_path.empty()) {
    std::ifstream suite(options.suite_path);
    if (!suite) {
      std::cerr << "Failed to open perft suite: " << options.suite_path << "\n";
      return 1;
    }
    bool ok = true;
    std::string line;
    std::size_t line_no = 0;
    while (std::getline(suite, line)) {
      ++line_no;
      if (line.empty() || line[0] == '#') {
        continue;
      }
      const auto first_bar = line.find('|');
      const auto second_bar =
          first_bar == std::string::npos ? std::string::npos : line.find('|', first_bar + 1);
      if (first_bar == std::string::npos || second_bar == std::string::npos) {
        std::cerr << "Malformed suite line " << line_no << ": " << line << "\n";
        ok = false;
        continue;
      }
      const std::string fen = line.substr(0, first_bar);
      const int depth = std::stoi(line.substr(first_bar + 1, second_bar - first_bar - 1));
      const std::uint64_t expected = std::stoull(line.substr(second_bar + 1));
      bby::Position pos = bby::Position::from_fen(fen, false);
      const auto start = std::chrono::steady_clock::now();
      const std::uint64_t nodes = bby::perft(pos, depth);
      const auto end = std::chrono::steady_clock::now();
      const auto elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      std::cout << "line=" << line_no << " depth=" << depth << " nodes=" << nodes
                << " expected=" << expected << " time_ms=" << elapsed_ms;
      if (nodes != expected) {
        std::cout << " mismatch";
        ok = false;
      }
      std::cout << "\n";
    }
    return ok ? 0 : 1;
  }

  if (!options.epd_path.empty()) {
    std::string error;
    const auto records = bby::load_epd_file(options.epd_path, error);
    if (!error.empty()) {
      std::cerr << "EPD error: " << error << "\n";
      return 1;
    }
    std::uint64_t total_nodes = 0;
    for (std::size_t idx = 0; idx < records.size(); ++idx) {
      bby::Position pos = records[idx].position;
      const auto start = std::chrono::steady_clock::now();
      const std::uint64_t nodes = bby::perft(pos, options.depth);
      const auto end = std::chrono::steady_clock::now();
      const auto elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
      total_nodes += nodes;
      std::cout << "line=" << (idx + 1) << " nodes=" << nodes << " time_ms=" << elapsed_ms
                << " id=";
      auto id_it = records[idx].operations.find("id");
      if (id_it != records[idx].operations.end()) {
        std::cout << id_it->second;
      }
      auto bm_it = records[idx].operations.find("bm");
      if (bm_it != records[idx].operations.end()) {
        std::cout << " bm=" << bm_it->second;
      }
      std::cout << "\n";
    }
    std::cout << "summary nodes=" << total_nodes << " entries=" << records.size() << "\n";
    return 0;
  }

  bby::Position pos = bby::Position::from_fen(options.fen, false);

  if (options.split) {
    bby::MoveList moves;
    pos.generate_moves(moves, bby::GenStage::All);
    std::uint64_t total = 0;
    for (const bby::Move move : moves) {
      bby::Undo undo;
      pos.make(move, undo);
      const std::uint64_t nodes = bby::perft(pos, options.depth - 1);
      pos.unmake(move, undo);
      total += nodes;
      std::cout << bby::move_to_uci(move) << ": " << nodes << "\n";
    }
    std::cout << "total: " << total << "\n";
    return 0;
  }

  const auto start = std::chrono::steady_clock::now();
  std::uint64_t nodes = bby::perft(pos, options.depth);
  const auto end = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  std::cout << "perft depth=" << options.depth << " nodes=" << nodes
            << " time_ms=" << elapsed_ms.count();
  if (elapsed_ms.count() > 0) {
    const double nps = (nodes * 1000.0) / static_cast<double>(elapsed_ms.count());
    std::cout << " nps=" << static_cast<std::uint64_t>(nps);
  }
  std::cout << "\n";
  return 0;
}
