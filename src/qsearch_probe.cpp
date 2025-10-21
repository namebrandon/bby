#include "qsearch_probe.h"

#if BBY_ENABLE_QSEARCH_PROBE

#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "board.h"
#include "common.h"

namespace bby {

namespace {

struct ProbeConfig {
  bool enabled{false};
  const char* fen_filter{nullptr};
  const char* move_filter{nullptr};
  bool has_ply_filter{false};
  int ply_filter{0};
  bool include_unpruned{false};
  std::ostream* stream{&std::cerr};
};

const ProbeConfig& probe_config() {
  static const ProbeConfig config = [] {
    ProbeConfig cfg{};
    const char* flag = std::getenv("BBY_QSEARCH_PROBE");
    if (flag == nullptr || flag[0] == '\0') {
      return cfg;
    }
    cfg.enabled = true;
    cfg.fen_filter = std::getenv("BBY_QSEARCH_PROBE_FEN");
    cfg.move_filter = std::getenv("BBY_QSEARCH_PROBE_MOVE");
    if (const char* ply_env = std::getenv("BBY_QSEARCH_PROBE_PLY")) {
      if (*ply_env != '\0') {
        char* endptr = nullptr;
        const long parsed = std::strtol(ply_env, &endptr, 10);
        if (endptr != ply_env && *endptr == '\0') {
          cfg.has_ply_filter = true;
          cfg.ply_filter = static_cast<int>(parsed);
        }
      }
    }
    if (const char* mode = std::getenv("BBY_QSEARCH_PROBE_MODE")) {
      if (std::strcmp(mode, "all") == 0) {
        cfg.include_unpruned = true;
      }
    }
    if (const char* target = std::getenv("BBY_QSEARCH_PROBE_STREAM")) {
      if (std::strcmp(target, "stdout") == 0) {
        cfg.stream = &std::cout;
      }
    }
    return cfg;
  }();
  return config;
}

void emit_log(const ProbeConfig& cfg,
              const std::string& fen,
              std::string_view move_uci,
              Score stand_pat,
              Score alpha,
              int margin,
              int delta_margin,
              int ply,
              bool pruned) {
  std::ostringstream oss;
  const int threshold = stand_pat + margin + delta_margin;
  oss << "probe qsearch-delta-prune"
      << " fen=\"" << fen << '"'
      << " move=" << move_uci
      << " ply=" << ply
      << " stand_pat=" << stand_pat
      << " alpha=" << alpha
      << " margin=" << margin
      << " delta_margin=" << delta_margin
      << " threshold=" << threshold
      << " alpha_gap=" << (alpha - threshold)
      << " pruned=" << (pruned ? 1 : 0);
  (*cfg.stream) << oss.str() << std::endl;
}

}  // namespace

bool qsearch_delta_prune_probe(const Position& pos,
                               Move move,
                               Score stand_pat,
                               Score alpha,
                               int margin,
                               int delta_margin,
                               int ply,
                               bool pruned) {
  BBY_ASSERT(delta_margin >= 0);

  const ProbeConfig& cfg = probe_config();
  if (!cfg.enabled) {
    return false;
  }

  if (cfg.has_ply_filter && ply != cfg.ply_filter) {
    return false;
  }

  if (!pruned && !cfg.include_unpruned) {
    return false;
  }

  const std::string fen = pos.to_fen();
  if (cfg.fen_filter != nullptr && fen != cfg.fen_filter) {
    return false;
  }

  const std::string move_uci = move_to_uci(move);
  if (cfg.move_filter != nullptr && move_uci != cfg.move_filter) {
    return false;
  }

  emit_log(cfg, fen, move_uci, stand_pat, alpha, margin, delta_margin, ply, pruned);
  return true;
}

}  // namespace bby

#endif  // BBY_ENABLE_QSEARCH_PROBE
