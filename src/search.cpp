#include "search.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <vector>

#include "debug.h"
#include "qsearch_probe.h"
#include "search_stack.h"

namespace bby {
namespace {

constexpr std::size_t kDefaultTTMegabytes = 16;
constexpr Score kEvalInfinity = 30000;
constexpr Score kMateValue = kEvalInfinity - 512;
constexpr Score mate_score(int ply) { return kMateValue - ply; }
constexpr Score mated_score(int ply) { return -kMateValue + ply; }
constexpr int kQuietHistoryBonus = 128;
constexpr Score kAspirationBase = 64;
constexpr Score kAspirationScale = 16;
constexpr Score kStaticFutilitySlack = 128;
constexpr Score kRazoringSlack = 512;
constexpr int kMaxLmrDepth = 64;
constexpr int kMaxLmrMoves = 64;
constexpr int kHistoryReductionScale = 8192;
using LmrPlane = std::array<std::array<int, kMaxLmrMoves>, kMaxLmrDepth>;

const std::array<LmrPlane, 2>& lmr_tables() {
  static const std::array<LmrPlane, 2> tables = []() {
    std::array<LmrPlane, 2> tbl{};
    for (int pv = 0; pv < 2; ++pv) {
      const double divisor = pv ? 2.25 : 1.6;
      const double offset = pv ? 0.15 : 0.35;
      auto& plane = tbl[static_cast<std::size_t>(pv)];
      for (int depth = 0; depth < kMaxLmrDepth; ++depth) {
        for (int moves = 0; moves < kMaxLmrMoves; ++moves) {
          int value = 0;
          if (depth >= 2 && moves >= 2) {
            const double d = static_cast<double>(depth);
            const double m = static_cast<double>(moves);
            const double reduction = std::log(d) * std::log(m) / divisor + offset;
            if (reduction > 0.0) {
              value = static_cast<int>(std::lround(reduction));
            }
          }
          plane[static_cast<std::size_t>(depth)][static_cast<std::size_t>(moves)] =
              std::max(0, value);
        }
      }
    }
    return tbl;
  }();
  return tables;
}
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
  std::unique_ptr<CounterHistory> counter_history;
  std::unique_ptr<ContinuationHistory> continuation_history;
  double history_weight{1.0};
  double counter_history_weight{0.5};
  double continuation_history_weight{0.5};
  SearchStack stack;
  bool enable_null_move{true};
  int null_min_depth{2};
  int null_base_reduction{2};
  int null_depth_scale{4};
  int null_eval_margin{120};
  int null_verification_depth{1};
  int null_prunes{0};
  int null_attempts{0};
  int null_verifications{0};
  int lmr_reductions{0};
  bool enable_recapture_extension{true};
  bool enable_check_extension{true};
  int recapture_extension_depth{4};
  int check_extension_depth{3};
  int recapture_extensions{0};
  int check_extensions{0};
  int quiet_penalties{0};
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
  bool enable_multi_cut{true};
  int multi_cut_min_depth{4};
  int multi_cut_reduction{2};
  int multi_cut_candidates{8};
  int multi_cut_threshold{3};
  int multi_cut_prunes{0};
  std::atomic<bool>* stop_flag{nullptr};
  std::chrono::steady_clock::time_point start_time{};
  std::int64_t soft_time_ms{0};
  std::int64_t hard_time_ms{0};
  bool use_time{false};
  const SearchProgressFn* progress{nullptr};
  const CurrmoveFn* currmove{nullptr};
  int seldepth{0};
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

bool is_capture_move(Move move) {
  const MoveFlag flag = move_flag(move);
  return flag == MoveFlag::Capture || flag == MoveFlag::EnPassant ||
         flag == MoveFlag::PromotionCapture;
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

void update_counter_history(SearchState& state, Move parent_move, Move move, int bonus) {
  if (parent_move.is_null() || move.is_null()) {
    return;
  }
  if (!state.counter_history) {
    return;
  }
  const std::size_t prev_idx = CounterHistory::index(parent_move);
  state.counter_history->add(prev_idx, move, bonus);
}

void update_continuation_history(SearchState& state, Piece parent_piece, Move move, int bonus) {
  if (move.is_null() || parent_piece == Piece::None || !state.continuation_history) {
    return;
  }
  state.continuation_history->add(parent_piece, move, bonus);
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
  int margin = g_singular_margin.load(std::memory_order_relaxed);
  if (margin <= 0) {
    return false;
  }
  const SearchStack::Frame& frame = state.stack.frame(ply);
  if (frame.captured != PieceType::None) {
    margin = (margin * 3) / 4;
  }
  if (!state.stack.is_improving(ply)) {
    margin = (margin * 3) / 4;
  }
  margin = std::max(16, margin);
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
    const PieceType captured_type =
        (undo.captured != Piece::None) ? type_of(undo.captured) : PieceType::None;
    state.stack.prepare_child(ply, ply + 1, move, captured_type);
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

bool should_abort(SearchState& state) {
  if (state.stop_flag != nullptr && state.stop_flag->load(std::memory_order_acquire)) {
    state.aborted = true;
    return true;
  }
  if (state.use_time && state.hard_time_ms > 0) {
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - state.start_time).count();
    if (state.soft_time_ms > 0 && elapsed_ms >= state.soft_time_ms &&
        state.stop_flag != nullptr && !state.aborted) {
      state.stop_flag->store(true, std::memory_order_release);
    }
    if (elapsed_ms >= state.hard_time_ms) {
      state.aborted = true;
      return true;
    }
  }
  return false;
}

Score negamax(Position& pos, int depth, Score alpha, Score beta, SearchTables& tables,
              SearchState& state, int ply, PvTable* pv_table, bool previous_null) {
  state.nodes++;
  if (state.node_cap >= 0 && state.nodes > state.node_cap) {
    state.aborted = true;
    return alpha;
  }
  state.seldepth = std::max(state.seldepth, ply + 1);
  if (should_abort(state)) {
    return alpha;
  }
  const bool in_pv = (pv_table != nullptr);
  const bool trace_search = trace_enabled(TraceTopic::Search);
  BBY_ASSERT(ply >= 0 && ply < kMaxPly);
  Score static_eval = 0;
  bool have_static_eval = false;
  SearchStack::Frame& stack_frame = state.stack.frame(ply);
  auto ensure_static_eval = [&]() {
    if (!have_static_eval) {
      static_eval = evaluate(pos);
      have_static_eval = true;
    }
    if (!stack_frame.has_static_eval) {
      state.stack.set_static_eval(ply, static_eval);
    } else {
      static_eval = stack_frame.static_eval;
    }
  };
  auto improving_now = [&]() -> bool {
    ensure_static_eval();
    return state.stack.is_improving(ply);
  };
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
    const bool improving = improving_now();
    if (!improving) {
      const Score margin =
          static_cast<Score>(state.static_futility_margin * std::max(depth, 1));
      Score futility_value = static_eval + margin - kStaticFutilitySlack;
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
    } else if (trace_search) {
      std::ostringstream oss;
      oss << "trace search static futility skip-improving"
          << " ply=" << ply
          << " depth=" << depth;
      trace_emit(TraceTopic::Search, oss.str());
    }
  }

  if (!in_check && state.enable_razoring && state.razor_depth > 0 && ply > 0 &&
      !in_pv && !previous_null && depth <= state.razor_depth) {
    const bool improving = improving_now();
    if (!improving) {
      const Score margin =
          static_cast<Score>(state.razor_margin * std::max(depth, 1));
      const Score threshold =
          std::clamp(static_eval + margin - kRazoringSlack, static_cast<Score>(-kEvalInfinity),
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
        if (razor_score <= alpha + kRazoringSlack) {
          ++state.razor_prunes;
          return razor_score;
        }
      }
    } else if (trace_search) {
      std::ostringstream oss;
      oss << "trace search razoring skip-improving"
          << " ply=" << ply
          << " depth=" << depth;
      trace_emit(TraceTopic::Search, oss.str());
    }
  }

  if (state.enable_null_move && !in_check && !previous_null && depth >= state.null_min_depth &&
      has_sufficient_material_for_null(pos)) {
    ensure_static_eval();
    const Score eval_margin = static_eval - beta;
    int reduction = state.null_base_reduction;
    if (depth > state.null_min_depth) {
      reduction += (depth - state.null_min_depth) / std::max(1, state.null_depth_scale);
    }
    if (eval_margin > static_cast<Score>(state.null_eval_margin)) {
      reduction += 1;
    }
    reduction = std::clamp(reduction, state.null_base_reduction, depth - 1);
    const int null_depth = depth - 1 - reduction;
    if (null_depth >= 0) {
      ++state.null_attempts;
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search null attempt"
            << " ply=" << ply
            << " depth=" << depth
            << " reduction=" << reduction
            << " null_depth=" << null_depth
            << " margin=" << eval_margin;
        trace_emit(TraceTopic::Search, oss.str());
      }
      Undo null_undo;
      state.stack.prepare_child(ply, ply + 1, Move{}, PieceType::None);
      pos.make_null(null_undo);
      const Score null_score = -negamax(pos, null_depth, -beta, -beta + 1, tables, state, ply + 1,
                                        nullptr, true);
      pos.unmake_null(null_undo);
      if (state.aborted) {
        return beta;
      }
      if (null_score >= beta) {
        bool verified = false;
        const bool allow_verification = !in_pv && state.null_verification_depth > 0 &&
                                        null_depth >= state.null_verification_depth;
        if (allow_verification) {
          ++state.null_verifications;
          if (trace_search) {
            std::ostringstream oss;
            oss << "trace search null verify"
                << " ply=" << ply
                << " depth=" << depth
                << " null_depth=" << null_depth
                << " beta=" << beta;
            trace_emit(TraceTopic::Search, oss.str());
          }
          const Score verify_score =
              negamax(pos, null_depth, beta - 1, beta, tables, state, ply, nullptr, true);
          if (state.aborted) {
            return beta;
          }
          if (verify_score >= beta) {
            verified = true;
          } else if (trace_search) {
            std::ostringstream oss;
            oss << "trace search null verify-fail"
                << " ply=" << ply
                << " depth=" << depth
                << " score=" << verify_score
                << " beta=" << beta;
            trace_emit(TraceTopic::Search, oss.str());
          }
        } else {
          verified = true;
        }
        if (verified) {
          ++state.null_prunes;
          if (trace_search) {
            std::ostringstream oss;
            oss << "trace search null prune"
                << " ply=" << ply
                << " depth=" << depth
                << " reduction=" << reduction
                << " null_depth=" << null_depth
                << " beta=" << beta
                << " score=" << null_score
                << " verified=" << (verified ? 1 : 0);
            trace_emit(TraceTopic::Search, oss.str());
          }
          return null_score;
        }
      }
    }
  }

  if (!stack_frame.has_static_eval) {
    ensure_static_eval();
  }
  const bool improving_lmr = state.stack.is_improving(ply);

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
  ordering.counter_history = state.counter_history.get();
  ordering.continuation_history = state.continuation_history.get();
  ordering.history_weight = state.history_weight;
  ordering.counter_history_weight = state.counter_history_weight;
  ordering.continuation_history_weight = state.continuation_history_weight;
  ordering.see_cache = &state.see_cache;
  if (static_cast<std::size_t>(ply) < state.killers.size()) {
    ordering.killers = state.killers[static_cast<std::size_t>(ply)];
  }
  ordering.parent_move = stack_frame.parent_move;
  if (tt_hit) {
    ordering.tt = &tt_entry;
  }
  std::array<int, kMaxMoves> move_scores{};
  score_moves(moves, ordering, move_scores);

  if (!in_check && state.enable_multi_cut && state.multi_cut_threshold > 0 &&
      state.multi_cut_candidates > 0 && state.multi_cut_min_depth > 0 &&
      !in_pv && !previous_null && ply > 0 && depth >= state.multi_cut_min_depth) {
    const int reduced_depth = depth - 1 - state.multi_cut_reduction;
    if (reduced_depth >= 0) {
      const std::size_t move_count = moves.size();
      const int candidates = std::min<int>(state.multi_cut_candidates,
                                           static_cast<int>(move_count));
      if (candidates > 0) {
        std::vector<std::size_t> order(move_count);
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::partial_sort(order.begin(), order.begin() + candidates, order.end(),
                          [&](std::size_t lhs, std::size_t rhs) {
                            return move_scores[lhs] > move_scores[rhs];
                          });
        const auto history_snapshot = state.history;
        const auto killers_snapshot = state.killers;
        int cut_count = 0;
        for (int idx = 0; idx < candidates; ++idx) {
          if (should_abort(state)) {
            state.history = history_snapshot;
            state.killers = killers_snapshot;
            return beta;
          }
          const Move move = moves[order[static_cast<std::size_t>(idx)]];
          if (is_root_excluded(state, move, ply)) {
            continue;
          }
          Undo undo;
          pos.make(move, undo);
          const PieceType captured_type =
              (undo.captured != Piece::None) ? type_of(undo.captured) : PieceType::None;
          state.stack.prepare_child(ply, ply + 1, move, captured_type);
          const Score cut_score = -negamax(pos, reduced_depth, -beta, -beta + 1, tables, state,
                                           ply + 1, nullptr, false);
          pos.unmake(move, undo);
          if (state.aborted) {
            state.history = history_snapshot;
            state.killers = killers_snapshot;
            return beta;
          }
          if (cut_score >= beta) {
            ++cut_count;
            if (cut_count >= state.multi_cut_threshold) {
              state.history = history_snapshot;
              state.killers = killers_snapshot;
              if (trace_search) {
                std::ostringstream oss;
                oss << "trace search multi-cut"
                    << " ply=" << ply
                    << " depth=" << depth
                    << " beta=" << beta
                    << " reduced_depth=" << reduced_depth
                    << " cuts=" << cut_count;
                trace_emit(TraceTopic::Search, oss.str());
              }
              ++state.multi_cut_prunes;
              return beta;
            }
          }
        }
        state.history = history_snapshot;
        state.killers = killers_snapshot;
      }
    }
  }

  const bool singular_extension = tt_hit && should_extend_singular(pos, moves, tt_entry.best_move,
                                                                   depth, tt_entry, tables,
                                                                   state, ply, previous_null);

  Move best_move{};
  Score best_score = -kEvalInfinity;
  std::array<Move, kMaxMoves> failed_quiets{};
  int failed_quiet_count = 0;

  const std::size_t move_count = moves.size();
  std::size_t processed_moves = 0;
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    if (should_abort(state)) {
      break;
    }
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    if (is_root_excluded(state, move, ply)) {
      continue;
    }
    if (ply == 0 && state.currmove != nullptr) {
      const int move_number = static_cast<int>(processed_moves) + 1;
      (*state.currmove)(move, move_number);
    }
    const bool is_primary_move = (processed_moves == 0);
    const Color moving_side = pos.side_to_move();
    const bool quiet = is_quiet_move(move);
    const Score alpha_before_move = alpha;
    const bool cut_node = alpha > alpha_orig;
    const Move parent_move = stack_frame.parent_move;
    const Piece parent_piece = parent_move.is_null() ? Piece::None : pos.piece_on(to_square(parent_move));
    const PieceType parent_capture = stack_frame.captured;
    const bool singular_hit = singular_extension && move == tt_entry.best_move;
    Undo undo;
    pos.make(move, undo);
    const PieceType captured_type =
        (undo.captured != Piece::None) ? type_of(undo.captured) : PieceType::None;
    const bool gives_check = pos.in_check(pos.side_to_move());
    bool recapture_extension = false;
    bool check_extension = false;
    int extension = 0;
    if (singular_hit) {
      extension = std::max(extension, 1);
    }
    if (state.enable_recapture_extension && depth <= state.recapture_extension_depth &&
        !parent_move.is_null() && parent_capture != PieceType::None && is_capture_move(move) &&
        to_square(move) == to_square(parent_move)) {
      recapture_extension = true;
      extension = std::max(extension, 1);
    }
    if (state.enable_check_extension && gives_check && depth <= state.check_extension_depth) {
      check_extension = true;
      extension = std::max(extension, 1);
    }
    extension = std::min(extension, 2);
    const int next_depth = depth - 1 + extension;
    int reduction = 0;
    const bool root_node = (ply == 0);
    const bool allow_lmr = !is_primary_move && !in_check && extension == 0 &&
                           (!in_pv || root_node);
    const bool allow_reduction = allow_lmr && !root_node && quiet;
    if (allow_reduction && next_depth > 1 && depth >= state.lmr_min_depth &&
        static_cast<int>(processed_moves) + 1 >= state.lmr_min_move) {
      const auto& lmr_table = lmr_tables();
      const int depth_idx = std::min(depth, kMaxLmrDepth - 1);
      const int move_order = std::min(static_cast<int>(processed_moves) + 1, kMaxLmrMoves - 1);
      const int history_score = state.history.get(moving_side, move);
      int base = lmr_table[in_pv ? 1 : 0][static_cast<std::size_t>(depth_idx)]
                                       [static_cast<std::size_t>(move_order)];
      if (!improving_lmr && base > 0) {
        base += 1;
      }
      if (cut_node) {
        base += 1;
      }
      if (history_score > 0) {
        base -= history_score / kHistoryReductionScale;
      } else if (history_score < 0) {
        base += (-history_score) / kHistoryReductionScale;
      }
      base = std::clamp(base, 0, next_depth - 1);
      reduction = base;
    } else if (allow_lmr && root_node && trace_search) {
      std::ostringstream oss;
      oss << "trace search lmr skip-root"
          << " move=" << move_to_uci(move)
          << " depth=" << depth;
      trace_emit(TraceTopic::Search, oss.str());
    }

    if (recapture_extension) {
      ++state.recapture_extensions;
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search extend recapture"
            << " ply=" << ply
            << " move=" << move_to_uci(move)
            << " depth=" << depth;
        trace_emit(TraceTopic::Search, oss.str());
      }
    }
    if (check_extension) {
      ++state.check_extensions;
      if (trace_search) {
        std::ostringstream oss;
        oss << "trace search extend check"
            << " ply=" << ply
            << " move=" << move_to_uci(move)
            << " depth=" << depth;
        trace_emit(TraceTopic::Search, oss.str());
      }
    }
    state.stack.prepare_child(ply, ply + 1, move, captured_type);
    if (gives_check) {
      reduction = 0;
    }

    int search_depth = next_depth;
    const bool lmr_used = reduction > 0;
    if (lmr_used) {
      search_depth = std::max(1, next_depth - reduction);
      ++state.lmr_reductions;
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

    if (quiet && score <= alpha_before_move &&
        failed_quiet_count < static_cast<int>(failed_quiets.size())) {
      failed_quiets[static_cast<std::size_t>(failed_quiet_count++)] = move;
    }

    if (state.aborted) {
      break;
    }

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (searched_full_window && in_pv) {
        pv_table->set(ply, move);
      }
    }

    if (score > alpha) {
      alpha = score;
      if (quiet) {
        const int bonus = kQuietHistoryBonus * depth * depth;
        update_history(state, pos.side_to_move(), move, bonus);
        if (!parent_move.is_null()) {
          const int scaled_bonus = std::max(1, bonus / 2);
          update_counter_history(state, parent_move, move, scaled_bonus);
          if (parent_piece != Piece::None) {
            update_continuation_history(state, parent_piece, move, scaled_bonus);
          }
        }
      }
    }

    if (alpha >= beta) {
      if (quiet) {
        update_killers(state, ply, move);
        const int bonus = kQuietHistoryBonus * depth * depth;
        update_history(state, pos.side_to_move(), move, bonus);
        if (!parent_move.is_null()) {
          const int scaled_bonus = std::max(1, bonus / 2);
          update_counter_history(state, parent_move, move, scaled_bonus);
          if (parent_piece != Piece::None) {
            update_continuation_history(state, parent_piece, move, scaled_bonus);
          }
        }
      }
      const int penalty = kQuietHistoryBonus * depth;
      for (int idx = 0; idx < failed_quiet_count; ++idx) {
        update_history(state, moving_side, failed_quiets[static_cast<std::size_t>(idx)],
                       -penalty);
        if (!parent_move.is_null()) {
          const int scaled_penalty = std::max(1, penalty / 2);
          update_counter_history(state, parent_move, failed_quiets[static_cast<std::size_t>(idx)],
                                 -scaled_penalty);
          if (parent_piece != Piece::None) {
            update_continuation_history(state, parent_piece,
                                        failed_quiets[static_cast<std::size_t>(idx)], -scaled_penalty);
          }
        }
      }
      state.quiet_penalties += failed_quiet_count;
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
  state.seldepth = std::max(state.seldepth, ply + 1);
  if (should_abort(state)) {
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
  constexpr int kDeltaMargin = 128;
  for (std::size_t move_index = 0; move_index < move_count; ++move_index) {
    select_best_move(moves, move_scores, move_index, move_count);
    const Move move = moves[move_index];
    const int margin = capture_margin(pos, move);
    const int see_gain = cached_see(pos, move, ordering.see_cache);
    bool delta_pruned = false;
    if (see_gain < 0) {
      if (stand_pat + see_gain + kDeltaMargin < alpha) {
        delta_pruned = true;
      }
    } else if (stand_pat + see_gain + kDeltaMargin < alpha) {
      delta_pruned = true;
    }
    qsearch_delta_prune_probe(pos, move, stand_pat, alpha, margin, kDeltaMargin, ply,
                              delta_pruned);
    if (trace_q) {
      std::ostringstream oss;
      oss << "trace qsearch candidate"
          << " ply=" << ply
          << " move=" << move_to_uci(move)
          << " margin=" << margin
          << " see=" << see_gain
          << " delta=" << kDeltaMargin
          << " threshold=" << (stand_pat + see_gain + kDeltaMargin)
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

SearchResult search(Position& root, const Limits& limits, std::atomic<bool>* stop_flag,
                    const SearchProgressFn* progress, const CurrmoveFn* currmove) {
  SearchTables tables;
  SearchState state;
  state.counter_history = std::make_unique<CounterHistory>();
  state.continuation_history = std::make_unique<ContinuationHistory>();
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
  state.enable_multi_cut = limits.enable_multi_cut;
  state.multi_cut_min_depth = std::clamp(limits.multi_cut_min_depth, 0, 64);
  state.multi_cut_reduction = std::clamp(limits.multi_cut_reduction, 0, 4);
  state.multi_cut_candidates = std::clamp(limits.multi_cut_candidates, 0, 32);
  state.multi_cut_threshold = std::clamp(limits.multi_cut_threshold, 0, 32);
  state.multi_cut_prunes = 0;
  state.history_weight = std::clamp(limits.history_weight_scale, 0, 400) / 100.0;
  state.counter_history_weight = std::clamp(limits.counter_history_weight_scale, 0, 400) / 100.0;
  state.continuation_history_weight =
      std::clamp(limits.continuation_history_weight_scale, 0, 400) / 100.0;
  state.enable_null_move = limits.enable_null_move;
  state.null_min_depth = std::clamp(limits.null_min_depth, 1, 64);
  state.null_base_reduction = std::max(1, limits.null_base_reduction);
  state.null_depth_scale = std::max(1, limits.null_depth_scale);
  state.null_eval_margin = std::max(0, limits.null_eval_margin);
  state.null_verification_depth = std::max(0, limits.null_verification_depth);
  state.null_prunes = 0;
  state.null_attempts = 0;
  state.null_verifications = 0;
  state.lmr_reductions = 0;
  state.enable_recapture_extension = limits.enable_recapture_extension;
  state.enable_check_extension = limits.enable_check_extension;
  state.recapture_extension_depth = std::clamp(limits.recapture_extension_depth, 0, 16);
  state.check_extension_depth = std::clamp(limits.check_extension_depth, 0, 16);
  state.recapture_extensions = 0;
  state.check_extensions = 0;
  state.quiet_penalties = 0;
  state.start_time = std::chrono::steady_clock::now();
  const TimeBudget time_budget = compute_time_budget(limits, root.side_to_move());
  state.hard_time_ms = time_budget.hard_ms;
  state.soft_time_ms = std::min<std::int64_t>(time_budget.soft_ms, time_budget.hard_ms);
  state.use_time = state.hard_time_ms > 0;
  if (!state.use_time) {
    state.soft_time_ms = 0;
  }
  state.stop_flag = stop_flag;
  state.progress = progress;
  state.currmove = currmove;

  emit_search_trace_start(root, limits);

  const int max_depth = limits.depth > 0 ? limits.depth : 1;
  const int requested_multipv = std::clamp(limits.multipv > 0 ? limits.multipv : 1, 1,
                                          static_cast<int>(kMaxMoves));

  SearchResult result;
  result.best = Move{};
  result.eval = 0;
  result.lines.clear();
  SearchResult last_completed = result;
  bool have_completed = false;

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

    if (state.stop_flag != nullptr &&
        state.stop_flag->load(std::memory_order_acquire)) {
      state.aborted = true;
      aborted_depth = true;
    }
    if (aborted_depth) {
      break;
    }

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
        if (should_abort(state)) {
          aborted_depth = true;
          break;
        }
        if (trace_search_enabled && use_aspiration) {
          std::ostringstream oss;
          oss << "aspiration attempt depth=" << current_depth
              << " multipv=" << (pv_index + 1)
              << " attempt=" << attempt
              << " alpha=" << alpha
              << " beta=" << beta;
          trace_emit(TraceTopic::Search, oss.str());
        }

        state.stack.prepare_root();
        score = negamax(root, current_depth, alpha, beta, tables, state, 0, &pv_table, false);
        if (state.aborted) {
          aborted_depth = true;
          break;
        }
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
      result.seldepth = state.seldepth;
      result.hashfull = tables.tt.hashfull();
      last_completed = result;
      have_completed = true;
      if (state.progress != nullptr) {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - state.start_time)
                            .count();
        result.elapsed_ms = now;
        (*state.progress)(result);
      }
    }

    if (state.aborted || aborted_depth) {
      break;
    }
  }

  if (state.aborted && have_completed) {
    result = last_completed;
  }

  result.nodes = state.nodes;
  result.primary_killer = state.killers[0][0];
  result.history_bonus = result.best.is_null()
                             ? 0
                             : state.history.get(root.side_to_move(), result.best);
  result.static_futility_prunes = state.static_futility_prunes;
  result.razor_prunes = state.razor_prunes;
  result.multi_cut_prunes = state.multi_cut_prunes;
  result.null_prunes = state.null_prunes;
  result.null_attempts = state.null_attempts;
  result.null_verifications = state.null_verifications;
  result.lmr_reductions = state.lmr_reductions;
  result.recapture_extensions = state.recapture_extensions;
  result.check_extensions = state.check_extensions;
  result.quiet_penalties = state.quiet_penalties;
  const auto finish_time = std::chrono::steady_clock::now();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              finish_time - state.start_time)
                              .count();
  result.elapsed_ms = elapsed_ms;
  result.seldepth = state.seldepth;
  result.hashfull = tables.tt.hashfull();

  if (result.best.is_null()) {
    MoveList fallback_moves;
    root.generate_moves(fallback_moves, GenStage::All);
    for (const Move move : fallback_moves) {
      Undo undo;
      root.make(move, undo);
      const bool legal = !root.in_check(root.side_to_move());
      root.unmake(move, undo);
      if (legal) {
        result.best = move;
        result.pv.line.clear();
        result.pv.line.push_back(move);
        if (result.lines.empty()) {
          PVLine line;
          line.best = move;
          line.pv.line = result.pv.line;
          line.eval = result.eval;
          result.lines.push_back(line);
        } else {
          result.lines.front().best = move;
          if (result.lines.front().pv.line.empty()) {
            result.lines.front().pv.line = result.pv.line;
          }
        }
        break;
      }
    }
  }

  TTEntry root_entry{};
  result.tt_hit = tables.tt.probe(root.zobrist(), root_entry);
  if (result.best.is_null() && !root_entry.best_move.is_null()) {
    result.best = root_entry.best_move;
    result.pv.line.clear();
    result.pv.line.push_back(root_entry.best_move);
    if (result.lines.empty()) {
      PVLine line;
      line.best = root_entry.best_move;
      line.pv.line = result.pv.line;
      line.eval = result.eval;
      result.lines.push_back(line);
    } else {
      result.lines.front().best = root_entry.best_move;
      if (result.lines.front().pv.line.empty()) {
        result.lines.front().pv.line = result.pv.line;
      }
    }
  }
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
