#include "perft.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

namespace {

struct Options {
  std::string fen;
  int depth{4};
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
    }
  }
  return opt;
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parse(argc, argv);
  bby::Position pos = bby::Position::from_fen(options.fen, false);

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
