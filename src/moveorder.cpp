#include "moveorder.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "common.h"

namespace bby {

namespace {

constexpr std::array<int, 6> kPieceValues = {100, 320, 330, 500, 900, 10000};
constexpr int kTTScore = 1'000'000;
constexpr int kCaptureBase = 100'000;
constexpr int kPromotionBase = 90'000;
constexpr int kKillerPrimary = 80'000;
constexpr int kKillerSecondary = 60'000;
constexpr int kBadCapturePenalty = 40'000;
constexpr int kHistoryScale = 2;

struct ScoredMove {
  Move move{};
  int score{0};
};

int material(Piece pc) {
  const PieceType type = type_of(pc);
  if (type == PieceType::None) {
    return 0;
  }
  return kPieceValues[static_cast<int>(type)];
}

bool is_capture_like(MoveFlag flag) {
  return flag == MoveFlag::Capture || flag == MoveFlag::PromotionCapture ||
         flag == MoveFlag::EnPassant;
}

Piece capture_victim(const Position& pos, Move m) {
  const Square to = to_square(m);
  Piece victim = pos.piece_on(to);
  if (move_flag(m) == MoveFlag::EnPassant) {
    const int dir = pos.side_to_move() == Color::White ? -8 : 8;
    const Square capture_sq = static_cast<Square>(static_cast<int>(to) + dir);
    victim = pos.piece_on(capture_sq);
  }
  return victim;
}

int promotion_bonus(Move m) {
  if (move_flag(m) != MoveFlag::Promotion &&
      move_flag(m) != MoveFlag::PromotionCapture) {
    return 0;
  }
  switch (promotion_type(m)) {
    case PieceType::Queen:
      return kPromotionBase + 8'000;
    case PieceType::Rook:
      return kPromotionBase + 5'000;
    case PieceType::Bishop:
      return kPromotionBase + 2'000;
    case PieceType::Knight:
      return kPromotionBase + 1'000;
    default:
      return kPromotionBase;
  }
}

int promotion_delta(Move m) {
  const PieceType promo = promotion_type(m);
  if (promo == PieceType::None) {
    return 0;
  }
  return kPieceValues[static_cast<int>(promo)] -
         kPieceValues[static_cast<int>(PieceType::Pawn)];
}

int history_score(const HistoryTable* history, const Position& pos, Move m) {
  if (!history) {
    return 0;
  }
  return history->get(pos.side_to_move(), m) * kHistoryScale;
}

int see_recursive(Position& pos, Square target) {
  MoveList moves;
  pos.generate_moves(moves, GenStage::Captures);
  int best = std::numeric_limits<int>::min();
  for (std::size_t idx = 0; idx < moves.size(); ++idx) {
    const Move move = moves[idx];
    if (to_square(move) != target) {
      continue;
    }
    const Piece victim = capture_victim(pos, move);
    const int gain = material(victim) + promotion_delta(move);
    Undo undo;
    pos.make(move, undo);
    const int reply = see_recursive(pos, target);
    pos.unmake(move, undo);
    const int net = gain - reply;
    if (net > best) {
      best = net;
    }
  }
  if (best == std::numeric_limits<int>::min()) {
    return 0;
  }
  return best;
}

}  // namespace

int HistoryTable::get(Color color, Move move) const {
  const std::size_t idx = index(color, move);
  return values[idx];
}

void HistoryTable::add(Color color, Move move, int delta) {
  const std::size_t idx = index(color, move);
  int value = values[idx] + delta;
  constexpr int kMaxHistory = 32'000;
  value = std::clamp(value, -kMaxHistory, kMaxHistory);
  values[idx] = value;
}

std::size_t HistoryTable::index(Color color, Move move) {
  const int from = static_cast<int>(from_square(move));
  const int to = static_cast<int>(to_square(move));
  const std::size_t base = static_cast<std::size_t>(color_index(color)) * kStride;
  const std::size_t idx = base + static_cast<std::size_t>(from) * 64 +
                          static_cast<std::size_t>(to);
  BBY_ASSERT(idx < 2 * kStride);
  return idx;
}

void score_moves(MoveList& ml, const OrderingContext& ctx) {
  BBY_ASSERT(ctx.pos != nullptr);
  const Position& pos = *ctx.pos;
  const std::size_t count = ml.size();

  std::array<ScoredMove, kMaxMoves> scored{};
  for (std::size_t idx = 0; idx < count; ++idx) {
    const Move move = ml[idx];
    int score = 0;

    if (ctx.tt && ctx.tt->best_move == move) {
      score += kTTScore;
    }

    const MoveFlag flag = move_flag(move);
    if (is_capture_like(flag)) {
      const Piece victim = capture_victim(pos, move);
      const Piece attacker = pos.piece_on(from_square(move));
      const int mvv_lva =
          material(victim) * 16 - material(attacker);  // MVV-LVA emphasis
      score += kCaptureBase + mvv_lva;
      if (see(pos, move) < 0) {
        score -= kBadCapturePenalty;
      }
    }

    score += promotion_bonus(move);

    if (move == ctx.killers[0]) {
      score += kKillerPrimary;
    } else if (move == ctx.killers[1]) {
      score += kKillerSecondary;
    } else if (!is_capture_like(flag)) {
      score += history_score(ctx.history, pos, move);
    }

    scored[idx] = {move, score};
  }

  std::sort(scored.begin(), scored.begin() + static_cast<long>(count),
            [](const ScoredMove& lhs, const ScoredMove& rhs) {
              if (lhs.score == rhs.score) {
                return lhs.move.value < rhs.move.value;
              }
              return lhs.score > rhs.score;
            });

  for (std::size_t idx = 0; idx < count; ++idx) {
    ml[idx] = scored[idx].move;
  }
}

int see(const Position& pos, Move m) {
  if (m.is_null()) {
    return 0;
  }

  const MoveFlag flag = move_flag(m);
  if (!is_capture_like(flag) && promotion_type(m) == PieceType::None) {
    return 0;
  }

  const Piece victim = capture_victim(pos, m);
  const int initial_gain = material(victim) + promotion_delta(m);

  Position temp = pos;
  Undo undo;
  temp.make(m, undo);
  const int reply = see_recursive(temp, to_square(m));
  temp.unmake(m, undo);
  return initial_gain - reply;
}

}  // namespace bby
