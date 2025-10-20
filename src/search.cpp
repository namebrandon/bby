#include "search.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>

#include "debug.h"

namespace bby {
namespace {

constexpr std::size_t kDefaultTTMegabytes = 16;
constexpr Score kEvalInfinity = 30000;
constexpr Score kMateValue = kEvalInfinity - 512;
constexpr Score mate_score(int ply) { return kMateValue - ply; }
constexpr Score mated_score(int ply) { return -kMateValue + ply; }
constexpr int kQuietHistoryBonus = 128;
constexpr Score kSingularMargin = 50;

struct PVLine {
  std::array<Move, kMaxPly> moves{};
  int length{0};
};

struct SearchTables {
  SearchTables() : tt(kDefaultTTMegabytes) {}

  TT tt;
  std::uint8_t generation{0};
};

struct SearchState {
  HistoryTable history;
  std::array<std::array<Move, 2>, kMaxPly> killers{};
  std::int64_t nodes{0};
};

bool is_quiet_move(Move move) {
  const MoveFlag flag = move_flag(move);
  return flag == MoveFlag::Quiet || flag == MoveFlag::DoublePush;
}

void update_killers(SearchState& state, int ply, Move move) {
  if (move.is_null() || ply < 0 || ply >= kMaxPly) {
    return;
  }
  auto& slots = state.killers[static_cast<std::size_t>(ply)];
  if (slots[0] == move) {
    return;
  }
  slots[1] = slots[0];
  slots[0] = move;
}

void update_history(SearchState& state, Color side, Move move, int bonus) {
  if (move.is_null()) {
    return;
  }
  state.history.add(side, move, bonus);
}

void emit_search_trace_start(const Position& root, const Limits& limits) {
  if (!trace_enabled(TraceTopic::Search)) {
    return;
  }
  std::ostringstream oss;
  oss << "start stm=" << (root.side_to_move() == Color::White ? "white" : "black");
  if (limits.depth >= 0) {
    oss << " depth_limit=" << limits.depth;
  }
  if (limits.nodes >= 0) {
    oss << " node_limit=" << limits.nodes;
  }
  if (limits.movetime_ms >= 0) {
    oss << " movetime_ms=" << limits.movetime_ms;
  }
  oss << " zobrist=0x" << std::hex << root.zobrist() << std::dec;
  trace_emit(TraceTopic::Search, oss.str());
}

void emit_search_trace_finish(const SearchResult& result) {
  if (!trace_enabled(TraceTopic::Search)) {
    return;
  }
  std::ostringstream oss;
  oss << "finish depth=" << result.depth
      << " nodes=" << result.nodes
      << " eval=" << result.eval;
  if (!result.best.is_null()) {
    oss << " best=" << move_to_uci(result.best);
  } else {
    oss << " best=0000";
  }
  if (!result.pv.line.empty()) {
    oss << " pv=";
    for (std::size_t idx = 0; idx < result.pv.line.size(); ++idx) {
      if (idx > 0) {
        oss << ',';
      }
      oss << move_to_uci(result.pv.line[idx]);
    }
  }
  trace_emit(TraceTopic::Search, oss.str());
}

void set_pv(PVLine& dst, Move move, const PVLine& child) {
  dst.moves[0] = move;
  std::copy(child.moves.begin(), child.moves.begin() + child.length, dst.moves.begin() + 1);
  dst.length = child.length + 1;
}

Score qsearch(Position& pos, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply);
Score negamax(Position& pos, int depth, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply, PVLine& pv);

bool should_extend_singular(Position& pos, const MoveList& moves, Move tt_move,
                            int depth, const TTEntry& tt_entry,
                            SearchTables& tables, SearchState& state, int ply) {
  if (tt_move.is_null()) {
    return false;
  }
  if (depth < 2) {
    return false;
  }
  if (tt_entry.bound != BoundType::Lower) {
    return false;
  }
  const Score singular_beta = tt_entry.score - kSingularMargin;
  const Score singular_alpha = singular_beta - 1;
  if (singular_beta <= -kEvalInfinity) {
    return false;
  }

  const int reduced_depth = std::max(0, depth - 2);
  const auto history_snapshot = state.history;
  const auto killers_snapshot = state.killers;

  PVLine dummy{};
  for (std::size_t idx = 0; idx < moves.size(); ++idx) {
    const Move move = moves[idx];
    if (move == tt_move) {
      continue;
    }
    Undo undo;
    pos.make(move, undo);
    const Score score =
        -negamax(pos, reduced_depth, -singular_beta, -singular_alpha, tables, state, ply + 1, dummy);
    pos.unmake(move, undo);
    if (score >= singular_beta) {
      state.history = history_snapshot;
      state.killers = killers_snapshot;
      return false;
    }
  }
  state.history = history_snapshot;
  state.killers = killers_snapshot;
  return true;
}

Score negamax(Position& pos, int depth, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply, PVLine& pv) {
  state.nodes++;
  pv.length = 0;

  if (ply >= kMaxPly - 1) {
    return evaluate(pos);
  }

  const Score alpha_orig = alpha;
  TTEntry tt_entry{};
  const bool tt_hit = tables.tt.probe(pos.zobrist(), tt_entry);
  if (tt_hit && tt_entry.depth >= depth) {
    const Score tt_score = tt_entry.score;
    if (tt_entry.bound == BoundType::Exact) {
      return tt_score;
    }
    if (tt_entry.bound == BoundType::Lower && tt_score >= beta) {
      return tt_score;
    }
    if (tt_entry.bound == BoundType::Upper && tt_score <= alpha) {
      return tt_score;
    }
  }

  if (depth <= 0) {
    return qsearch(pos, alpha, beta, tables, state, ply);
  }

  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  if (moves.size() == 0) {
    if (pos.in_check(pos.side_to_move())) {
      return mated_score(ply);
    }
    return 0;
  }

  OrderingContext ordering{};
  ordering.pos = &pos;
  ordering.ply = ply;
  ordering.history = &state.history;
  if (static_cast<std::size_t>(ply) < state.killers.size()) {
    ordering.killers = state.killers[static_cast<std::size_t>(ply)];
  }
  if (tt_hit) {
    ordering.tt = &tt_entry;
  }
  score_moves(moves, ordering);

  const bool singular_extension = tt_hit && should_extend_singular(pos, moves, tt_entry.best_move,
                                                                   depth, tt_entry, tables,
                                                                   state, ply);

  Move best_move{};
  Score best_score = -kEvalInfinity;
  PVLine child_pv{};

  for (const Move move : moves) {
    Undo undo;
    pos.make(move, undo);
    const int extension = (singular_extension && move == tt_entry.best_move) ? 1 : 0;
    const int next_depth = depth - 1 + extension;
    const Score score = -negamax(pos, next_depth, -beta, -alpha, tables, state, ply + 1, child_pv);
    pos.unmake(move, undo);

    if (score > best_score) {
      best_score = score;
      best_move = move;
      set_pv(pv, move, child_pv);
    }

    if (score > alpha) {
      alpha = score;
      if (is_quiet_move(move)) {
        const int bonus = kQuietHistoryBonus * depth * depth;
        update_history(state, pos.side_to_move(), move, bonus);
      }
    }

    if (alpha >= beta) {
      if (is_quiet_move(move)) {
        update_killers(state, ply, move);
        const int bonus = kQuietHistoryBonus * depth * depth;
        update_history(state, pos.side_to_move(), move, bonus);
      }
      break;
    }
  }

  if (best_score == -kEvalInfinity) {
    best_score = evaluate(pos);
  }

  BoundType bound = BoundType::Exact;
  if (best_score <= alpha_orig) {
    bound = BoundType::Upper;
  } else if (best_score >= beta) {
    bound = BoundType::Lower;
  }

  TTEntry store{};
  store.best_move = best_move;
  store.score = best_score;
  store.static_eval = best_score;
  store.depth = static_cast<std::uint8_t>(std::clamp(depth, 0, 255));
  store.bound = bound;
  tables.tt.store(pos.zobrist(), store);

  return best_score;
}

Score qsearch(Position& pos, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply) {
  state.nodes++;
  const bool in_check = pos.in_check(pos.side_to_move());
  if (in_check) {
    MoveList evasions;
    pos.generate_moves(evasions, GenStage::All);
    if (evasions.size() == 0) {
      return mated_score(ply);
    }
    Score best = -kEvalInfinity;
    for (const Move move : evasions) {
      Undo undo;
      pos.make(move, undo);
      const Score score = -qsearch(pos, -beta, -alpha, tables, state, ply + 1);
      pos.unmake(move, undo);
      if (score > best) {
        best = score;
      }
      if (score > alpha) {
        alpha = score;
      }
      if (alpha >= beta) {
        break;
      }
    }
    return best;
  }

  const Score stand_pat = evaluate(pos);
  if (stand_pat >= beta) {
    return stand_pat;
  }
  Score best = stand_pat;
  if (stand_pat > alpha) {
    alpha = stand_pat;
  }

  MoveList moves;
  pos.generate_moves(moves, GenStage::Captures);
  if (moves.size() == 0) {
    return stand_pat;
  }

  OrderingContext ordering{};
  ordering.pos = &pos;
  ordering.ply = ply;
  ordering.history = &state.history;
  if (static_cast<std::size_t>(ply) < state.killers.size()) {
    ordering.killers = state.killers[static_cast<std::size_t>(ply)];
  }
  score_moves(moves, ordering);

  for (const Move move : moves) {
    if (see(pos, move) < 0) {
      continue;
    }
    Undo undo;
    pos.make(move, undo);
    const Score score = -qsearch(pos, -beta, -alpha, tables, state, ply + 1);
    pos.unmake(move, undo);
    if (score > best) {
      best = score;
    }
    if (score > alpha) {
      alpha = score;
    }
    if (alpha >= beta) {
      break;
    }
  }

  return best;
}

}  // namespace

SearchResult search(Position& root, const Limits& limits) {
  SearchTables tables;
  SearchState state;
  state.nodes = 0;

  emit_search_trace_start(root, limits);

  const int max_depth = limits.depth > 0 ? limits.depth : 1;
  SearchResult result;
  result.best = Move{};
  result.eval = 0;

  PVLine pv{};
  PVLine iteration_pv{};

  for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
    ++tables.generation;
    tables.tt.set_generation(tables.generation);

    result.depth = current_depth;
    iteration_pv = PVLine{};
    const Score score = negamax(root, current_depth, -kEvalInfinity, kEvalInfinity,
                                tables, state, 0, iteration_pv);
    result.eval = score;
    result.nodes = state.nodes;

    if (iteration_pv.length > 0) {
      pv = iteration_pv;
      result.best = iteration_pv.moves[0];
      result.pv.line.assign(iteration_pv.moves.begin(), iteration_pv.moves.begin() + iteration_pv.length);
    }
  }

  result.primary_killer = state.killers[0][0];
  result.history_bonus = result.best.is_null()
                             ? 0
                             : state.history.get(root.side_to_move(), result.best);
  TTEntry root_entry{};
  result.tt_hit = tables.tt.probe(root.zobrist(), root_entry);

  emit_search_trace_finish(result);
  return result;
}

}  // namespace bby
