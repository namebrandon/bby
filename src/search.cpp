#include "search.h"

#include <iomanip>
#include <sstream>

#include "debug.h"

namespace bby {

SearchResult search(Position& root, const Limits& limits) {
  const bool trace_search = trace_enabled(TraceTopic::Search);
  if (trace_search) {
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

  SearchResult result;
  MoveList moves;
  root.generate_moves(moves, GenStage::All);

  const Score eval_score = evaluate(root);
  if (moves.size() > 0) {
    result.best = moves[0];
    result.pv.line.push_back(result.best);
  }
  result.depth = 1;
  result.nodes = static_cast<std::int64_t>(moves.size());

  if (trace_search) {
    std::ostringstream oss;
    oss << "finish depth=" << result.depth
        << " nodes=" << result.nodes
        << " eval=" << eval_score;
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

  return result;
}

}  // namespace bby
