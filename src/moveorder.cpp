#include "moveorder.h"

#include <array>

namespace bby {

namespace {

constexpr std::array<int, 6> kPieceValues = {100, 320, 330, 500, 900, 10000};

int material(Piece pc) {
  const PieceType type = type_of(pc);
  if (type == PieceType::None) {
    return 0;
  }
  return kPieceValues[static_cast<int>(type)];
}

}  // namespace

void score_moves(MoveList& ml, const OrderingContext&) {
  (void)ml;
}

int see(const Position& pos, Move m) {
  if (m.is_null()) {
    return 0;
  }
  const Square from = from_square(m);
  const Square to = to_square(m);
  const Piece attacker = pos.piece_on(from);
  Piece victim = pos.piece_on(to);

  if (move_flag(m) == MoveFlag::EnPassant) {
    const int dir = pos.side_to_move() == Color::White ? -8 : 8;
    const Square capture_sq = static_cast<Square>(static_cast<int>(to) + dir);
    victim = pos.piece_on(capture_sq);
  }

  const int gain = material(victim) - material(attacker);
  return gain;
}

}  // namespace bby
