#include "debug.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace bby {
namespace {

std::array<bool, static_cast<std::size_t>(TraceTopic::Count)> &trace_flags() {
  static std::array<bool, static_cast<std::size_t>(TraceTopic::Count)> flags{};
  return flags;
}

std::string lowercase(std::string_view sv) {
  std::string out(sv.begin(), sv.end());
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

}  // namespace

void set_trace_topic(TraceTopic topic, bool enabled) {
  trace_flags()[static_cast<std::size_t>(topic)] = enabled;
}

bool trace_enabled(TraceTopic topic) {
  return trace_flags()[static_cast<std::size_t>(topic)];
}

std::optional<TraceTopic> trace_topic_from_string(std::string_view token) {
  const std::string norm = lowercase(token);
  if (norm == "search") {
    return TraceTopic::Search;
  }
  if (norm == "qsearch") {
    return TraceTopic::QSearch;
  }
  if (norm == "tt") {
    return TraceTopic::TT;
  }
  if (norm == "eval") {
    return TraceTopic::Eval;
  }
  if (norm == "moves") {
    return TraceTopic::Moves;
  }
  return std::nullopt;
}

std::string_view trace_topic_name(TraceTopic topic) {
  switch (topic) {
    case TraceTopic::Search:
      return "search";
    case TraceTopic::QSearch:
      return "qsearch";
    case TraceTopic::TT:
      return "tt";
    case TraceTopic::Eval:
      return "eval";
    case TraceTopic::Moves:
      return "moves";
    case TraceTopic::Count:
      break;
  }
  return "unknown";
}

InvariantStatus validate_position(const Position& pos) {
  InvariantStatus status;

  bool white_king = false;
  bool black_king = false;
  Bitboard derived_occ[2] = {0ULL, 0ULL};
  Bitboard derived_all = 0ULL;

  for (int idx = 0; idx < 64; ++idx) {
    const Square sq = static_cast<Square>(idx);
    const Piece pc = pos.piece_on(sq);
    if (pc == Piece::None) {
      continue;
    }
    const Color c = color_of(pc);
    const Bitboard mask = bit(sq);
    derived_occ[color_index(c)] |= mask;
    derived_all |= mask;
    if (pc == Piece::WKing) {
      white_king = true;
    } else if (pc == Piece::BKing) {
      black_king = true;
    }
  }

  if (!white_king || !black_king) {
    status.ok = false;
    status.message = !white_king ? "white king missing" : "black king missing";
    return status;
  }

  for (int ci = 0; ci < 2; ++ci) {
    const Color c = (ci == 0) ? Color::White : Color::Black;
    if (derived_occ[ci] != pos.occupancy(c)) {
      status.ok = false;
      status.message = ci == 0 ? "occupancy mismatch for white"
                                : "occupancy mismatch for black";
      return status;
    }
  }

  if (derived_all != pos.occupancy()) {
    status.ok = false;
    status.message = "aggregate occupancy mismatch";
    return status;
  }

  const Square ep = pos.en_passant_square();
  if (ep != Square::None) {
    const Rank ep_rank = rank_of(ep);
    const bool valid_rank = (ep_rank == Rank::R3) || (ep_rank == Rank::R6);
    if (!valid_rank) {
      status.ok = false;
      status.message = "invalid en passant square rank";
      return status;
    }
  }

  status.message = "position ok";
  return status;
}

}  // namespace bby
