#include "search.h"

#include <array>
#include <iomanip>
#include <sstream>

#include "debug.h"

namespace bby {
namespace {

constexpr std::size_t kDefaultTTMegabytes = 16;
constexpr int kQuietHistoryBonus = 128;

struct SearchTables {
  SearchTables() : tt(kDefaultTTMegabytes) {}

  TT tt;
  std::uint8_t generation{0};
};

struct SearchState {
  HistoryTable history;
  std::array<std::array<Move, 2>, kMaxPly> killers{};
};

bool is_quiet_move(Move move) {
  const MoveFlag flag = move_flag(move);
  return flag == MoveFlag::Quiet || flag == MoveFlag::DoublePush;
}

void update_killers(SearchState& state, int ply, Move move) {
  if (ply < 0 || ply >= kMaxPly) {
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

}  // namespace

SearchResult search(Position& root, const Limits& limits) {
  SearchTables tables;
  SearchState state;

  ++tables.generation;
  tables.tt.set_generation(tables.generation);

  emit_search_trace_start(root, limits);

  OrderingContext ordering{};
  ordering.pos = &root;
  ordering.ply = 0;
  ordering.history = &state.history;
  ordering.killers = state.killers[0];

  TTEntry tt_entry{};
  const bool tt_hit = tables.tt.probe(root.zobrist(), tt_entry);
  if (tt_hit) {
    ordering.tt = &tt_entry;
  }

  MoveList moves;
  root.generate_moves(moves, GenStage::All);
  score_moves(moves, ordering);

  SearchResult result;
  result.depth = 1;
  result.nodes = static_cast<std::int64_t>(moves.size());
  result.tt_hit = tt_hit;

  const Score eval_score = tt_hit ? tt_entry.score : evaluate(root);
  result.eval = eval_score;

  if (moves.size() > 0) {
    result.best = moves[0];
    result.pv.line.push_back(result.best);
  }

  if (!result.best.is_null() && is_quiet_move(result.best)) {
    update_history(state, root.side_to_move(), result.best, kQuietHistoryBonus);
    update_killers(state, ordering.ply, result.best);
  }

  result.primary_killer = state.killers[0][0];
  result.history_bonus = result.best.is_null()
                              ? 0
                              : state.history.get(root.side_to_move(), result.best);

  if (!tt_hit) {
    TTEntry store{};
    store.best_move = result.best;
    store.score = eval_score;
    store.static_eval = eval_score;
    store.depth = 0;
    store.bound = BoundType::Exact;
    tables.tt.store(root.zobrist(), store);
  }

  emit_search_trace_finish(result);
  return result;
}

}  // namespace bby
