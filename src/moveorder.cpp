#include "moveorder.h"

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

#include "common.h"
#include "attacks.h"

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

constexpr std::array<PieceType, 6> kSeeOrder = {
    PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
    PieceType::Rook, PieceType::Queen, PieceType::King};

inline int piece_index(PieceType type) {
  return static_cast<int>(type);
}

inline Bitboard bit_mask(Square sq) {
  return bit(sq);
}

inline Square pop_lsb(Bitboard& bb) {
  BBY_ASSERT(bb != 0ULL);
  const int idx = static_cast<int>(std::countr_zero(bb));
  bb &= bb - 1;
  return static_cast<Square>(idx);
}

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

using PieceTable = std::array<std::array<Bitboard, 6>, 2>;
struct SeeState {
  PieceTable pieces{};
  std::array<Bitboard, 2> occ_by_color{};
  Bitboard occ{0ULL};

  void remove_piece(Color color, PieceType type, Square sq) {
    const Bitboard mask = bit_mask(sq);
    const int idx = color_index(color);
    occ_by_color[idx] &= ~mask;
    occ &= ~mask;
    if (type != PieceType::None) {
      pieces[idx][piece_index(type)] &= ~mask;
    }
  }

  void place_piece(Color color, PieceType type, Square sq) {
    const Bitboard mask = bit_mask(sq);
    const int idx = color_index(color);
    occ_by_color[idx] |= mask;
    occ |= mask;
    if (type != PieceType::None) {
      pieces[idx][piece_index(type)] |= mask;
    }
  }
};

}  // namespace

void SeeCache::clear() {
  for (auto& entry : entries_) {
    entry.valid = false;
  }
}

bool SeeCache::probe(std::uint64_t key, Move move, int& out) const {
  const std::size_t idx = index(key, move);
  const Entry& entry = entries_[idx];
  if (entry.valid && entry.key == key && entry.move == move) {
    out = entry.value;
    return true;
  }
  return false;
}

void SeeCache::store(std::uint64_t key, Move move, int value) {
  const std::size_t idx = index(key, move);
  entries_[idx] = Entry{key, move, value, true};
}

std::size_t SeeCache::index(std::uint64_t key, Move move) {
  const std::uint64_t mixed =
      key ^ (key >> 17) ^ (key << 13) ^ (static_cast<std::uint64_t>(move.value) << 1);
  return static_cast<std::size_t>(mixed) & (kSize - 1);
}

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

void score_moves(MoveList& ml, const OrderingContext& ctx, std::array<int, kMaxMoves>& scores,
                 std::array<int, kMaxMoves>* see_results, bool force_see) {
  BBY_ASSERT(ctx.pos != nullptr);
  const Position& pos = *ctx.pos;
  const std::size_t count = ml.size();

  for (std::size_t idx = 0; idx < count; ++idx) {
    const Move move = ml[idx];
    int score = 0;

    if (ctx.tt && ctx.tt->best_move == move) {
      score += kTTScore;
    }

    const MoveFlag flag = move_flag(move);
    if (see_results != nullptr) {
      (*see_results)[idx] = is_capture_like(flag) ? kSeeUnknown : 0;
    }

    if (is_capture_like(flag)) {
      const Piece victim = capture_victim(pos, move);
      const Piece attacker = pos.piece_on(from_square(move));
      const int victim_value = material(victim);
      const int attacker_value = material(attacker);
      const int mvv_lva =
          victim_value * 16 - attacker_value;  // MVV-LVA emphasis
      score += kCaptureBase + mvv_lva;
      const bool needs_see =
          promotion_type(move) != PieceType::None || flag == MoveFlag::EnPassant ||
          attacker_value >= victim_value;
      int see_value = 0;
      const int margin = material(victim) - attacker_value;
      const bool guaranteed_win = margin >= 300;
      bool have_see = false;
      if (guaranteed_win) {
        see_value = margin;
        have_see = true;
      } else if (force_see || needs_see) {
        see_value = cached_see(pos, move, ctx.see_cache);
        have_see = true;
      }
      if (!guaranteed_win && needs_see && have_see && see_value < 0) {
        score -= kBadCapturePenalty;
      }
      if (see_results != nullptr) {
        (*see_results)[idx] = have_see ? see_value : kSeeUnknown;
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

    scores[idx] = score;
  }
}

void select_best_move(MoveList& ml, std::array<int, kMaxMoves>& scores, std::size_t start, std::size_t end) {
  std::size_t best = start;
  for (std::size_t idx = start + 1; idx < end; ++idx) {
    if (scores[idx] > scores[best] ||
        (scores[idx] == scores[best] && ml[idx].value < ml[best].value)) {
      best = idx;
    }
  }
  if (best != start) {
    std::swap(scores[start], scores[best]);
    std::swap(ml[start], ml[best]);
  }
}

int capture_margin(const Position& pos, Move m) {
  const Piece victim = capture_victim(pos, m);
  return material(victim) + promotion_delta(m);
}

int cached_see(const Position& pos, Move move, SeeCache* cache) {
  if (cache != nullptr) {
    int cached_value = 0;
    const std::uint64_t key = pos.zobrist();
    if (cache->probe(key, move, cached_value)) {
      return cached_value;
    }
    const int value = see(pos, move);
    cache->store(key, move, value);
    return value;
  }
  return see(pos, move);
}

int see(const Position& pos, Move m) {
  if (m.is_null()) {
    return 0;
  }

  const MoveFlag flag = move_flag(m);
  if (!is_capture_like(flag) && promotion_type(m) == PieceType::None) {
    return 0;
  }

  const Square from = from_square(m);
  const Square to = to_square(m);
  const Piece moving_piece = pos.piece_on(from);
  const Color us = color_of(moving_piece);
  const Color them = flip(us);
  const PieceType moving_type = type_of(moving_piece);
  const PieceType promotion = promotion_type(m);
  const bool promoting = promotion != PieceType::None;

  const Piece victim_piece =
      (flag == MoveFlag::EnPassant)
          ? make_piece(them, PieceType::Pawn)
          : pos.piece_on(to);

  const int initial_gain = material(victim_piece) + promotion_delta(m);
  int depth = 0;
  std::array<int, 32> gains{};
  gains[depth] = initial_gain;

  SeeState state{};
  for (int color_idx = 0; color_idx < 2; ++color_idx) {
    const Color color = color_idx == 0 ? Color::White : Color::Black;
    state.occ_by_color[color_idx] = pos.occupancy(color);
    for (int type_idx = 0; type_idx < 6; ++type_idx) {
      state.pieces[color_idx][type_idx] =
          pos.pieces(color, static_cast<PieceType>(type_idx));
    }
  }
  state.occ = state.occ_by_color[0] | state.occ_by_color[1];

  state.remove_piece(us, moving_type, from);
  if (flag == MoveFlag::EnPassant) {
    const int ep_dir = (us == Color::White) ? -8 : 8;
    const auto ep_sq =
        static_cast<Square>(static_cast<int>(to) + ep_dir);
    state.remove_piece(them, PieceType::Pawn, ep_sq);
  } else if (victim_piece != Piece::None) {
    state.remove_piece(them, type_of(victim_piece), to);
  }

  PieceType current_type = promoting ? promotion : moving_type;
  Color current_color = us;
  state.place_piece(current_color, current_type, to);

  auto slider_attacks = [&](Bitboard occ) {
    const Bitboard bishops = bishop_attacks(to, occ);
    const Bitboard rooks = rook_attacks(to, occ);
    return std::pair{bishops, rooks};
  };

  auto compute_non_sliders = [&](Color side) {
    const int idx = color_index(side);
    Bitboard attackers_bb = pawn_attacks(flip(side), to) &
                            state.pieces[idx][piece_index(PieceType::Pawn)];
    attackers_bb |= knight_attacks(to) &
                    state.pieces[idx][piece_index(PieceType::Knight)];
    attackers_bb |= king_attacks(to) &
                    state.pieces[idx][piece_index(PieceType::King)];
    return attackers_bb;
  };

  std::array<Bitboard, 2> non_sliders{
      compute_non_sliders(Color::White),
      compute_non_sliders(Color::Black),
  };

  const Bitboard bishop_rays = bishop_attacks(to, 0ULL);
  const Bitboard rook_rays = rook_attacks(to, 0ULL);

  auto compute_attackers = [&](Color side, Bitboard bishop_mask, Bitboard rook_mask) {
    const int idx = color_index(side);
    Bitboard attackers_bb = non_sliders[idx];
    const Bitboard bishop_like =
        (state.pieces[idx][piece_index(PieceType::Bishop)] |
         state.pieces[idx][piece_index(PieceType::Queen)]);
    const Bitboard rook_like =
        (state.pieces[idx][piece_index(PieceType::Rook)] |
         state.pieces[idx][piece_index(PieceType::Queen)]);
    attackers_bb |= bishop_mask & bishop_like;
    attackers_bb |= rook_mask & rook_like;
    return attackers_bb;
  };

  Bitboard bishop_mask{};
  Bitboard rook_mask{};
  {
    const auto masks = slider_attacks(state.occ);
    bishop_mask = masks.first;
    rook_mask = masks.second;
  }

  std::array<Bitboard, 2> attackers{};
  std::array<bool, 2> dirty{true, true};

  Color side = them;

  while (true) {
    const int side_idx = color_index(side);
    if (dirty[side_idx]) {
      attackers[side_idx] = compute_attackers(side, bishop_mask, rook_mask);
      dirty[side_idx] = false;
    }
    Bitboard side_attackers = attackers[side_idx];
    if (side_attackers == 0ULL) {
      break;
    }

    PieceType attacker_type = PieceType::None;
    Square attacker_sq = Square::None;

    for (const PieceType candidate : kSeeOrder) {
      Bitboard pool = state.pieces[side_idx][piece_index(candidate)] & side_attackers;
      if (pool != 0ULL) {
        const int sq_idx = static_cast<int>(std::countr_zero(pool));
        attacker_sq = static_cast<Square>(sq_idx);
        attacker_type = candidate;
        break;
      }
    }

    if (attacker_type == PieceType::None) {
      break;
    }

    ++depth;
    gains[depth] = kPieceValues[piece_index(current_type)] - gains[depth - 1];

    state.remove_piece(current_color, current_type, to);
    const Bitboard from_mask = bit(attacker_sq);
    state.remove_piece(side, attacker_type, attacker_sq);
    if (attacker_type == PieceType::Pawn || attacker_type == PieceType::Knight ||
        attacker_type == PieceType::King) {
      non_sliders[side_idx] &= ~from_mask;
    }

    current_color = side;
    current_type = attacker_type;
    state.place_piece(current_color, current_type, to);

    side = flip(side);
    const bool touches_diag = (from_mask & bishop_rays) != 0ULL;
    const bool touches_orth = (from_mask & rook_rays) != 0ULL;
    if (touches_diag) {
      bishop_mask = bishop_attacks(to, state.occ);
    }
    if (touches_orth) {
      rook_mask = rook_attacks(to, state.occ);
    }
    if (touches_diag || touches_orth) {
      dirty = {true, true};
    }
  }

  for (int idx = depth; idx > 0; --idx) {
    gains[idx - 1] = -std::max(-gains[idx - 1], gains[idx]);
  }

  return gains[0];
}

}  // namespace bby
