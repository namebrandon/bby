#include "bench_cli.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string_view>

#include "bench.h"
#include "bbinit.h"
#include "search.h"
#include "searchparams.h"

namespace bby {

namespace {

struct BenchConfig {
  int depth{8};
  int positions{static_cast<int>(kBenchFens.size())};
  std::int64_t nodes_limit{0};
  int lmr_min_depth{kLmrMinDepthDefault};
  int lmr_min_move{kLmrMinMoveDefault};
};

bool parse_int(std::string_view token, long long& out) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const std::string copy(token);
  const long long value = std::strtoll(copy.c_str(), &end, 10);
  if (end == copy.c_str() || (end && *end != '\0')) {
    return false;
  }
  out = value;
  return true;
}

BenchConfig parse_cli_arguments(int argc, const char* argv[]) {
  BenchConfig cfg;
  for (int idx = 0; idx < argc; ++idx) {
    std::string_view arg(argv[idx]);
    auto consume_next = [&](long long& out) -> bool {
      if (idx + 1 >= argc) {
        return false;
      }
      ++idx;
      return parse_int(argv[idx], out);
    };

  if (arg == "--depth") {
      long long value = 0;
      if (consume_next(value)) {
        cfg.depth = static_cast<int>(std::max<long long>(1, value));
      }
    } else if (arg == "--positions") {
      long long value = 0;
      if (consume_next(value)) {
        cfg.positions = static_cast<int>(std::clamp<long long>(
            value, 1, static_cast<long long>(kBenchFens.size())));
      }
    } else if (arg == "--nodes") {
      long long value = 0;
      if (consume_next(value)) {
        cfg.nodes_limit = std::max<long long>(0, value);
      }
    } else if (arg == "--lmr-depth") {
      long long value = 0;
      if (consume_next(value)) {
        cfg.lmr_min_depth = static_cast<int>(std::max<long long>(1, value));
      }
    } else if (arg == "--lmr-move") {
      long long value = 0;
      if (consume_next(value)) {
        cfg.lmr_min_move = static_cast<int>(std::max<long long>(1, value));
      }
    } else if (arg == "--help" || arg == "-h") {
      std::printf("Usage: bby bench [--depth N] [--positions N] [--nodes LIMIT]\n");
      std::printf("                [--lmr-depth N] [--lmr-move N]\n");
      std::fflush(stdout);
      std::exit(0);
    } else {
      long long value = 0;
      if (parse_int(arg, value)) {
        cfg.depth = static_cast<int>(std::max<long long>(1, value));
      }
    }
  }
  return cfg;
}

}  // namespace

int bench_cli_main(int argc, const char* argv[]) {
  (void)initialize();
  const BenchConfig cfg = parse_cli_arguments(argc, argv);

  const int total_positions =
      std::min<int>(cfg.positions, static_cast<int>(kBenchFens.size()));
  std::uint64_t total_nodes = 0;
  std::uint64_t total_ms = 0;

  for (int idx = 0; idx < total_positions; ++idx) {
    Position pos = Position::from_fen(kBenchFens[static_cast<std::size_t>(idx)], false);
    Limits limits;
    limits.depth = static_cast<std::int16_t>(cfg.depth);
    if (cfg.nodes_limit > 0) {
      limits.nodes = cfg.nodes_limit;
    }
    limits.lmr_min_depth = cfg.lmr_min_depth;
    limits.lmr_min_move = cfg.lmr_min_move;

    const auto start = std::chrono::steady_clock::now();
    const SearchResult result = search(pos, limits);
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    total_nodes += static_cast<std::uint64_t>(result.nodes);
    total_ms += static_cast<std::uint64_t>(elapsed_ms);
  }

  const std::uint64_t denom = std::max<std::uint64_t>(1, total_ms);
  const std::uint64_t nps = (total_nodes * 1000ULL) / denom;
  std::printf("%llu nodes %llu nps\n",
              static_cast<unsigned long long>(total_nodes),
              static_cast<unsigned long long>(nps));
  std::fflush(stdout);
  return 0;
}

}  // namespace bby
