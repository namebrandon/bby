#include "search.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

#include "debug.h"

namespace bby {
namespace {

constexpr std::size_t kDefaultTTMegabytes = 16;
constexpr Score kEvalInfinity = 30000;
constexpr Score kMateValue = kEvalInfinity - 512;
constexpr Score mate_score(int ply) { return kMateValue - ply; }
constexpr Score mated_score(int ply) { return -kMateValue + ply; }
constexpr int kQuietHistoryBonus = 128;
constexpr int kNullMoveReduction = 2;

bool is_passed_pawn(const Position& pos, Color side, Square sq) {
  const Bitboard opponent_pawns = pos.pieces(flip(side), PieceType::Pawn);
  const int file = static_cast<int>(file_of(sq));
  const int rank = static_cast<int>(rank_of(sq));
  if (side == Color::White) {
    for (int r = rank + 1; r <= static_cast<int>(Rank::R8); ++r) {
      for (int df = -1; df <= 1; ++df) {
        const int f = file + df;
        if (f < 0 || f > 7) {
          continue;
        }
        const Square target = static_cast<Square>(r * 8 + f);
        if (opponent_pawns & bit(target)) {
          return false;
        }
      }
    }
    return true;
  }
  for (int r = rank - 1; r >= static_cast<int>(Rank::R1); --r) {
    for (int df = -1; df <= 1; ++df) {
      const int f = file + df;
      if (f < 0 || f > 7) {
        continue;
      }
      const Square target = static_cast<Square>(r * 8 + f);
      if (opponent_pawns & bit(target)) {
        return false;
      }
    }
  }
  return true;
}

bool has_connected_passers(const Position& pos, Color side) {
  Bitboard pawns = pos.pieces(side, PieceType::Pawn);
  std::vector<Square> passed;
  while (pawns) {
    const int sq_idx = static_cast<int>(std::countr_zero(pawns));
    pawns &= pawns - 1;
    const Square sq = static_cast<Square>(sq_idx);
    if (is_passed_pawn(pos, side, sq)) {
      passed.push_back(sq);
    }
  }
  const std::size_t n = passed.size();
  if (n < 2) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    const int file_i = static_cast<int>(file_of(passed[i]));
    const int rank_i = static_cast<int>(rank_of(passed[i]));
    for (std::size_t j = i + 1; j < n; ++j) {
      const int file_j = static_cast<int>(file_of(passed[j]));
      const int rank_j = static_cast<int>(rank_of(passed[j]));
      if (std::abs(file_i - file_j) == 1 && std::abs(rank_i - rank_j) <= 1) {
        return true;
      }
    }
  }
  return false;
}

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
  SeeCache see_cache;
  std::int64_t nodes{0};
  std::int64_t node_cap{-1};
  bool aborted{false};
};

std::atomic<int> g_singular_margin{50};

int count_non_pawn_material(const Position& pos, Color color) {
  int total = 0;
  for (PieceType type : {PieceType::Knight, PieceType::Bishop,
                         PieceType::Rook, PieceType::Queen}) {
    total += std::popcount(pos.pieces(color, type));
  }
  return total;
}

bool has_sufficient_material_for_null(const Position& pos) {
  const Color side = pos.side_to_move();
  const Color them = flip(side);
  const int own = count_non_pawn_material(pos, side);
  const int opp = count_non_pawn_material(pos, them);
  if (own == 0) {
    return false;
  }
  if (own + opp <= 1) {
    return false;
  }
  return true;
}

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
              SearchState& state, int ply, PVLine& pv, bool previous_null);

bool should_extend_singular(Position& pos, const MoveList& moves, Move tt_move,
                            int depth, const TTEntry& tt_entry,
                            SearchTables& tables, SearchState& state, int ply,
                            bool previous_null) {
  if (previous_null) {
    return false;
  }
  if (tt_move.is_null()) {
    return false;
  }
  if (depth < 3) {
    return false;
  }
  if (moves.size() <= 1) {
    return false;
  }
  constexpr std::size_t kMaxSingularWidth = 24;
  if (moves.size() > kMaxSingularWidth) {
    return false;
  }
  const MoveFlag tt_flag = move_flag(tt_move);
  if ((tt_flag == MoveFlag::Quiet || tt_flag == MoveFlag::DoublePush) && depth < 5) {
    return false;
  }
  if (tt_entry.bound != BoundType::Lower) {
    return false;
  }
  const int margin = g_singular_margin.load(std::memory_order_relaxed);
  if (margin <= 0) {
    return false;
  }
  if (margin <= 0) {
    return false;
  }
  const Score singular_beta = tt_entry.score - static_cast<Score>(margin);
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
        -negamax(pos, reduced_depth, -singular_beta, -singular_alpha, tables, state, ply + 1, dummy, previous_null);
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
              SearchState& state, int ply, PVLine& pv, bool previous_null) {
  state.nodes++;
  if (state.node_cap >= 0 && state.nodes > state.node_cap) {
    state.aborted = true;
    return alpha;
  }
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

  const bool in_check = pos.in_check(pos.side_to_move());

  if (!in_check && !previous_null && depth >= kNullMoveReduction + 1 &&
      has_sufficient_material_for_null(pos)) {
    Undo null_undo;
    pos.make_null(null_undo);
    PVLine null_pv{};
    const Score null_score = -negamax(pos, depth - 1 - kNullMoveReduction,
                                      -beta, -beta + 1, tables, state, ply + 1,
                                      null_pv, true);
    pos.unmake_null(null_undo);
    if (state.aborted) {
      return beta;
    }
    if (null_score >= beta) {
      return null_score;
    }
  }

  MoveList moves;
  pos.generate_moves(moves, GenStage::All);
  if (moves.size() == 0) {
    if (in_check) {
      return mated_score(ply);
    }
    return 0;
  }

  OrderingContext ordering{};
  ordering.pos = &pos;
  ordering.ply = ply;
  ordering.history = &state.history;
  ordering.see_cache = &state.see_cache;
  if (static_cast<std::size_t>(ply) < state.killers.size()) {
    ordering.killers = state.killers[static_cast<std::size_t>(ply)];
  }
  if (tt_hit) {
    ordering.tt = &tt_entry;
  }
  std::array<int, kMaxMoves> move_scores{};
  score_moves(moves, ordering, move_scores);

  const bool singular_extension = tt_hit && should_extend_singular(pos, moves, tt_entry.best_move,
                                                                   depth, tt_entry, tables,
                                                                   state, ply, previous_null);

  Move best_move{};
  Score best_score = -kEvalInfinity;
  PVLine child_pv{};

  const std::size_t move_count = moves.size();
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    Undo undo;
    pos.make(move, undo);
    const int extension = (singular_extension && move == tt_entry.best_move) ? 1 : 0;
    const int next_depth = depth - 1 + extension;
    const Score score = -negamax(pos, next_depth, -beta, -alpha, tables, state, ply + 1, child_pv, false);
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

  if (state.aborted) {
    return best_score;
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
  if (state.node_cap >= 0 && state.nodes > state.node_cap) {
    state.aborted = true;
    return alpha;
  }
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
      if (state.aborted) {
        break;
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
  ordering.see_cache = &state.see_cache;
  if (static_cast<std::size_t>(ply) < state.killers.size()) {
    ordering.killers = state.killers[static_cast<std::size_t>(ply)];
  }
  std::array<int, kMaxMoves> move_scores{};
  score_moves(moves, ordering, move_scores);

  const std::size_t move_count = moves.size();
  constexpr int kDeltaMargin = 150;
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    const int margin = capture_margin(pos, move);
    if (stand_pat + margin + kDeltaMargin < alpha) {
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
    if (state.aborted) {
      break;
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
  state.see_cache.clear();
  state.nodes = 0;
  state.node_cap = limits.nodes;
  state.aborted = false;

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
                                tables, state, 0, iteration_pv, false);
    result.eval = score;
    result.nodes = state.nodes;
    if (state.aborted) {
      break;
    }

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
  result.aborted = state.aborted;

  emit_search_trace_finish(result);
  return result;
}

void set_singular_margin(int margin) {
  const int clamped = std::clamp(margin, 0, 10000);
  g_singular_margin.store(clamped, std::memory_order_relaxed);
}

int singular_margin() {
  return g_singular_margin.load(std::memory_order_relaxed);
}

}  // namespace bby
