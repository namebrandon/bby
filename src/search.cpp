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
#include "qsearch_probe.h"

namespace bby {
namespace {

constexpr std::size_t kDefaultTTMegabytes = 16;
constexpr Score kEvalInfinity = 30000;
constexpr Score kMateValue = kEvalInfinity - 512;
constexpr Score mate_score(int ply) { return kMateValue - ply; }
constexpr Score mated_score(int ply) { return -kMateValue + ply; }
constexpr int kQuietHistoryBonus = 128;
constexpr int kNullMoveReduction = 2;
constexpr Score kAspirationBase = 64;
constexpr Score kAspirationScale = 16;
struct SearchTables {
  SearchTables() : tt(kDefaultTTMegabytes) {}

  TT tt;
  std::uint8_t generation{0};
};

struct PvTable {
  std::array<std::array<Move, kMaxPly>, kMaxPly> moves{};
  std::array<int, kMaxPly> length{};

  void clear() {
    length.fill(0);
  }

  void reset_row(int ply) {
    if (ply >= 0 && ply < kMaxPly) {
      length[ply] = 0;
    }
  }

  void set(int ply, Move move) {
    BBY_ASSERT(ply >= 0 && ply < kMaxPly);
    moves[ply][ply] = move;
    const int child_ply = ply + 1;
    const int child_length = (child_ply < kMaxPly) ? length[child_ply] : 0;
    for (int idx = 0; idx < child_length; ++idx) {
      moves[ply][ply + 1 + idx] = moves[child_ply][child_ply + idx];
    }
    length[ply] = child_length + 1;
  }

  void extract(int ply, std::vector<Move>& out) const {
    if (ply < 0 || ply >= kMaxPly) {
      out.clear();
      return;
    }
    const int count = std::clamp(length[ply], 0, kMaxPly - ply);
    out.assign(moves[ply].begin() + ply, moves[ply].begin() + ply + count);
  }
};

struct SearchState {
  HistoryTable history;
  std::array<std::array<Move, 2>, kMaxPly> killers{};
  SeeCache see_cache;
  std::int64_t nodes{0};
  std::int64_t node_cap{-1};
  bool aborted{false};
  std::array<Move, kMaxMoves> root_excludes{};
  int root_exclude_count{0};
  int lmr_min_depth{kLmrMinDepthDefault};
  int lmr_min_move{kLmrMinMoveDefault};
  bool enable_static_futility{true};
  int static_futility_margin{128};
  int static_futility_depth{1};
  int static_futility_prunes{0};
  bool enable_razoring{true};
  int razor_margin{256};
  int razor_depth{1};
  int razor_prunes{0};
};

std::atomic<int> g_singular_margin{50};

bool is_root_excluded(const SearchState& state, Move move, int ply) {
  if (ply != 0 || state.root_exclude_count <= 0) {
    return false;
  }
  for (int idx = 0; idx < state.root_exclude_count; ++idx) {
    if (state.root_excludes[static_cast<std::size_t>(idx)] == move) {
      return true;
    }
  }
  return false;
}

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

Score qsearch(Position& pos, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply);
Score negamax(Position& pos, int depth, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply, PvTable* pv_table, bool previous_null);

Score aspiration_margin(int depth) {
  const Score margin = kAspirationBase + static_cast<Score>(kAspirationScale * std::max(depth - 1, 0));
  return std::clamp<Score>(margin, 32, kEvalInfinity);
}

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

  for (std::size_t idx = 0; idx < moves.size(); ++idx) {
    const Move move = moves[idx];
    if (move == tt_move) {
      continue;
    }
    Undo undo;
    pos.make(move, undo);
    const Score score =
        -negamax(pos, reduced_depth, -singular_beta, -singular_alpha, tables, state,
                 ply + 1, nullptr, previous_null);
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
              SearchState& state, int ply, PvTable* pv_table, bool previous_null) {
  state.nodes++;
  if (state.node_cap >= 0 && state.nodes > state.node_cap) {
    state.aborted = true;
    return alpha;
  }
  const bool in_pv = (pv_table != nullptr);
  const bool trace_search = trace_enabled(TraceTopic::Search);
  Score static_eval = 0;
  bool have_static_eval = false;
  if (in_pv) {
    pv_table->reset_row(ply);
  }

  if (ply >= kMaxPly - 1) {
    return evaluate(pos);
  }

  const Score alpha_orig = alpha;
  TTEntry tt_entry{};
  const bool tt_hit = tables.tt.probe(pos.zobrist(), tt_entry);
  const bool root_with_exclusions = (ply == 0 && state.root_exclude_count > 0);
  if (tt_hit && tt_entry.depth >= depth && !root_with_exclusions) {
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

  if (!in_check && state.enable_static_futility && state.static_futility_depth > 0 &&
      ply > 0 && !in_pv && !previous_null && depth <= state.static_futility_depth) {
    static_eval = evaluate(pos);
    have_static_eval = true;
    const Score margin =
        static_cast<Score>(state.static_futility_margin * std::max(depth, 1));
    Score futility_value = static_eval + margin;
    futility_value = std::clamp(futility_value, static_cast<Score>(-kEvalInfinity),
                                static_cast<Score>(kEvalInfinity));
    if (futility_value <= alpha) {
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search static futility"
            << " ply=" << ply
            << " depth=" << depth
            << " alpha=" << alpha
            << " static=" << static_eval
            << " margin=" << margin
            << " value=" << futility_value;
        trace_emit(TraceTopic::Search, oss.str());
      }
      ++state.static_futility_prunes;
      return futility_value;
    }
  }

  if (!in_check && state.enable_razoring && state.razor_depth > 0 && ply > 0 &&
      !in_pv && !previous_null && depth <= state.razor_depth) {
    if (!have_static_eval) {
      static_eval = evaluate(pos);
      have_static_eval = true;
    }
    const Score margin =
        static_cast<Score>(state.razor_margin * std::max(depth, 1));
    const Score threshold =
        std::clamp(static_eval + margin, static_cast<Score>(-kEvalInfinity),
                   static_cast<Score>(kEvalInfinity));
    if (threshold <= alpha) {
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search razoring"
            << " ply=" << ply
            << " depth=" << depth
            << " alpha=" << alpha
            << " static=" << static_eval
            << " margin=" << margin;
        trace_emit(TraceTopic::Search, oss.str());
      }
      const Score razor_score = qsearch(pos, alpha, beta, tables, state, ply);
      if (state.aborted) {
        return razor_score;
      }
      if (razor_score <= alpha) {
        ++state.razor_prunes;
        return razor_score;
      }
    }
  }

  if (!in_check && !previous_null && depth >= kNullMoveReduction + 1 &&
      has_sufficient_material_for_null(pos)) {
    Undo null_undo;
    pos.make_null(null_undo);
    const Score null_score = -negamax(pos, depth - 1 - kNullMoveReduction,
                                      -beta, -beta + 1, tables, state, ply + 1,
                                      nullptr, true);
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

  const std::size_t move_count = moves.size();
  std::size_t processed_moves = 0;
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    if (is_root_excluded(state, move, ply)) {
      continue;
    }
    const bool is_primary_move = (processed_moves == 0);
    const Color moving_side = pos.side_to_move();
    Undo undo;
    pos.make(move, undo);
    const int extension = (singular_extension && move == tt_entry.best_move) ? 1 : 0;
    const int next_depth = depth - 1 + extension;
    int reduction = 0;
    const bool allow_lmr = !is_primary_move && !in_pv && !in_check && extension == 0;
    if (allow_lmr && next_depth > 1 && depth >= state.lmr_min_depth &&
        static_cast<int>(processed_moves) + 1 >= state.lmr_min_move && is_quiet_move(move)) {
      const int move_order = static_cast<int>(processed_moves);
      const int history_score = state.history.get(moving_side, move);
      reduction = 1;
      reduction += std::max(0, move_order - 2) / 3;
      reduction += std::max(0, depth - 4) / 3;
      if (history_score < 0) {
        reduction += 1;
      }
      reduction = std::min(reduction, next_depth - 1);
    }

    const bool gives_check = pos.in_check(pos.side_to_move());
    if (gives_check) {
      reduction = 0;
    }

    int search_depth = next_depth;
    const bool lmr_used = reduction > 0;
    if (lmr_used) {
      search_depth = std::max(1, next_depth - reduction);
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search lmr reduce"
            << " ply=" << ply
            << " move=" << move_to_uci(move)
            << " depth=" << depth
            << " reduction=" << reduction
            << " reduced_depth=" << search_depth;
        trace_emit(TraceTopic::Search, oss.str());
      }
    }
    Score score = -kEvalInfinity;
    bool searched_full_window = false;
    if (is_primary_move) {
      PvTable* child_pv = in_pv ? pv_table : nullptr;
      score = -negamax(pos, search_depth, -beta, -alpha, tables, state,
                       ply + 1, child_pv, false);
      searched_full_window = true;
    } else {
      const Score null_window_beta = std::min<Score>(alpha + 1, kEvalInfinity);
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search pvs narrow"
            << " ply=" << ply
            << " move=" << move_to_uci(move)
            << " alpha=" << alpha
            << " beta=" << beta
            << " window=[" << null_window_beta << ',' << alpha << ']';
        trace_emit(TraceTopic::Search, oss.str());
      }
      score = -negamax(pos, search_depth, -null_window_beta, -alpha, tables, state,
                       ply + 1, nullptr, false);
      if (lmr_used && !state.aborted && score > alpha) {
        score = -negamax(pos, next_depth, -null_window_beta, -alpha, tables, state,
                         ply + 1, nullptr, false);
      }
      if (!state.aborted && score > alpha && score < beta) {
        if (trace_search) {
          std::ostringstream oss;
          oss << "trace search pvs research"
              << " ply=" << ply
              << " move=" << move_to_uci(move)
              << " alpha=" << alpha
              << " beta=" << beta
              << " score=" << score;
          trace_emit(TraceTopic::Search, oss.str());
        }
        PvTable* child_pv = in_pv ? pv_table : nullptr;
        score = -negamax(pos, next_depth, -beta, -alpha, tables, state,
                         ply + 1, child_pv, false);
        searched_full_window = true;
      }
    }
    pos.unmake(move, undo);
    ++processed_moves;

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (searched_full_window && in_pv) {
        pv_table->set(ply, move);
      }
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
    if (!have_static_eval) {
      static_eval = evaluate(pos);
      have_static_eval = true;
    }
    best_score = static_eval;
    if (pv_table != nullptr) {
      pv_table->reset_row(ply);
    }
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
  store.static_eval = have_static_eval ? static_eval : best_score;
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
  const bool trace_q = trace_enabled(TraceTopic::QSearch);
  if (trace_q) {
    std::ostringstream oss;
    oss << "trace qsearch node"
        << " ply=" << ply
        << " stm=" << (pos.side_to_move() == Color::White ? 'w' : 'b')
        << " stand_pat=" << stand_pat
        << " alpha=" << alpha
        << " beta=" << beta
        << " fen=\"" << pos.to_fen() << '"';
    trace_emit(TraceTopic::QSearch, oss.str());
  }
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
  constexpr int kDeltaMargin = 210;
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    const int margin = capture_margin(pos, move);
    const bool delta_pruned =
        stand_pat + margin + kDeltaMargin < alpha;
    qsearch_delta_prune_probe(pos, move, stand_pat, alpha, margin, kDeltaMargin, ply,
                              delta_pruned);
    if (trace_q) {
      std::ostringstream oss;
      oss << "trace qsearch candidate"
          << " ply=" << ply
          << " move=" << move_to_uci(move)
          << " margin=" << margin
          << " delta=" << kDeltaMargin
          << " threshold=" << (stand_pat + margin + kDeltaMargin)
          << " alpha=" << alpha
          << " pruned=" << (delta_pruned ? 1 : 0);
      trace_emit(TraceTopic::QSearch, oss.str());
    }
    if (delta_pruned) {
      continue;
    }
    Undo undo;
    pos.make(move, undo);
    const Score score = -qsearch(pos, -beta, -alpha, tables, state, ply + 1);
    pos.unmake(move, undo);
    const Score alpha_before = alpha;
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
    if (trace_q) {
      std::ostringstream oss;
      oss << "trace qsearch result"
          << " ply=" << ply
          << " move=" << move_to_uci(move)
          << " score=" << score
          << " best=" << best
          << " alpha_before=" << alpha_before
          << " alpha_after=" << alpha
          << " beta=" << beta;
      trace_emit(TraceTopic::QSearch, oss.str());
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
  state.root_exclude_count = 0;
  state.lmr_min_depth = std::max(1, limits.lmr_min_depth);
  state.lmr_min_move = std::max(1, limits.lmr_min_move);
  state.enable_static_futility = limits.enable_static_futility;
  state.static_futility_margin = std::clamp(limits.static_futility_margin, 0, 1024);
  state.static_futility_depth = std::clamp(limits.static_futility_depth, 0, 3);
  state.static_futility_prunes = 0;
  state.enable_razoring = limits.enable_razoring;
  state.razor_margin = std::clamp(limits.razor_margin, 0, 2048);
  state.razor_depth = std::clamp(limits.razor_depth, 0, 3);
  state.razor_prunes = 0;

  emit_search_trace_start(root, limits);

  const int max_depth = limits.depth > 0 ? limits.depth : 1;
  const int requested_multipv = std::clamp(limits.multipv > 0 ? limits.multipv : 1, 1,
                                          static_cast<int>(kMaxMoves));

  SearchResult result;
  result.best = Move{};
  result.eval = 0;
  result.lines.clear();

  PvTable pv_table{};
  pv_table.clear();
  std::vector<Move> root_line;
  std::vector<PVLine> multipv_lines(static_cast<std::size_t>(requested_multipv));
  std::vector<Score> previous_scores(static_cast<std::size_t>(requested_multipv), 0);
  std::vector<bool> have_previous(static_cast<std::size_t>(requested_multipv), false);
  int active_multipv = requested_multipv;

  for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
    ++tables.generation;
    tables.tt.set_generation(tables.generation);

    result.depth = current_depth;
    const bool trace_search_enabled = trace_enabled(TraceTopic::Search);
    bool aborted_depth = false;
    int produced_lines = 0;

    for (int pv_index = 0; pv_index < active_multipv; ++pv_index) {
      state.root_exclude_count = pv_index;
      for (int idx = 0; idx < pv_index; ++idx) {
        state.root_excludes[static_cast<std::size_t>(idx)] =
            multipv_lines[static_cast<std::size_t>(idx)].best;
      }

      Score alpha = -kEvalInfinity;
      Score beta = kEvalInfinity;
      Score window = aspiration_margin(current_depth);
      Score score = 0;
      bool use_aspiration = have_previous[static_cast<std::size_t>(pv_index)];
      const Score previous_score = previous_scores[static_cast<std::size_t>(pv_index)];
      int attempt = 0;

      if (use_aspiration) {
        alpha = std::max(previous_score - window, -kEvalInfinity);
        beta = std::min(previous_score + window, kEvalInfinity);
        if (alpha >= beta) {
          alpha = -kEvalInfinity;
          beta = kEvalInfinity;
          use_aspiration = false;
        } else if (trace_search_enabled) {
          std::ostringstream oss;
          oss << "aspiration start depth=" << current_depth
              << " multipv=" << (pv_index + 1)
              << " alpha=" << alpha
              << " beta=" << beta
              << " window=" << window;
          trace_emit(TraceTopic::Search, oss.str());
        }
      }

      while (true) {
        pv_table.clear();
        ++attempt;
        if (trace_search_enabled && use_aspiration) {
          std::ostringstream oss;
          oss << "aspiration attempt depth=" << current_depth
              << " multipv=" << (pv_index + 1)
              << " attempt=" << attempt
              << " alpha=" << alpha
              << " beta=" << beta;
          trace_emit(TraceTopic::Search, oss.str());
        }

        score = negamax(root, current_depth, alpha, beta, tables, state, 0, &pv_table, false);
        if (state.aborted) {
          aborted_depth = true;
          break;
        }
        if (!use_aspiration) {
          break;
        }

        if (score <= alpha) {
          if (trace_search_enabled) {
            std::ostringstream oss;
            oss << "aspiration fail-low depth=" << current_depth
                << " multipv=" << (pv_index + 1)
                << " score=" << score
                << " alpha=" << alpha
                << " beta=" << beta;
            trace_emit(TraceTopic::Search, oss.str());
          }
          if (alpha <= -kEvalInfinity) {
            use_aspiration = false;
            alpha = -kEvalInfinity;
            beta = kEvalInfinity;
            continue;
          }
          window = std::min<Score>(static_cast<Score>(window * 2), kEvalInfinity);
          const Score center = score;
          alpha = std::max(center - window, -kEvalInfinity);
          beta = std::min(center + window, kEvalInfinity);
          if (alpha >= beta || (alpha <= -kEvalInfinity && beta >= kEvalInfinity)) {
            use_aspiration = false;
            alpha = -kEvalInfinity;
            beta = kEvalInfinity;
          }
          continue;
        }

        if (score >= beta) {
          if (trace_search_enabled) {
            std::ostringstream oss;
            oss << "aspiration fail-high depth=" << current_depth
                << " multipv=" << (pv_index + 1)
                << " score=" << score
                << " alpha=" << alpha
                << " beta=" << beta;
            trace_emit(TraceTopic::Search, oss.str());
          }
          if (beta >= kEvalInfinity) {
            use_aspiration = false;
            alpha = -kEvalInfinity;
            beta = kEvalInfinity;
            continue;
          }
          window = std::min<Score>(static_cast<Score>(window * 2), kEvalInfinity);
          const Score center = score;
          alpha = std::max(center - window, -kEvalInfinity);
          beta = std::min(center + window, kEvalInfinity);
          if (alpha >= beta || (alpha <= -kEvalInfinity && beta >= kEvalInfinity)) {
            use_aspiration = false;
            alpha = -kEvalInfinity;
            beta = kEvalInfinity;
          }
          continue;
        }

        break;
      }

      root_line.clear();
      pv_table.extract(0, root_line);
      PVLine line;
      line.pv.line = root_line;
      line.best = root_line.empty() ? Move{} : root_line.front();
      line.eval = score;
      multipv_lines[static_cast<std::size_t>(pv_index)] = line;
      previous_scores[static_cast<std::size_t>(pv_index)] = score;
      have_previous[static_cast<std::size_t>(pv_index)] = true;
      ++produced_lines;

      if (line.best.is_null()) {
        if (pv_index == 0) {
          active_multipv = 1;
        } else {
          active_multipv = pv_index;
          --produced_lines;
        }
        break;
      }
      if (aborted_depth) {
        break;
      }
    }

    state.root_exclude_count = 0;
    result.nodes = state.nodes;

    const int available = std::min(active_multipv, produced_lines);
    if (available > 0) {
      result.lines.assign(multipv_lines.begin(), multipv_lines.begin() + available);
      const PVLine& primary = result.lines.front();
      result.best = primary.best;
      result.pv = primary.pv;
      result.eval = primary.eval;
    }

    if (state.aborted || aborted_depth) {
      break;
    }
  }

  result.nodes = state.nodes;
  result.primary_killer = state.killers[0][0];
  result.history_bonus = result.best.is_null()
                             ? 0
                             : state.history.get(root.side_to_move(), result.best);
  result.static_futility_prunes = state.static_futility_prunes;
  result.razor_prunes = state.razor_prunes;
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
