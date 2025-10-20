#include "board.h"
#include "attacks.h"
#include "debug.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#if defined(__SSE2__)
#include <immintrin.h>
#endif

namespace bby {
namespace {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = 0x8080808080808080ULL;
constexpr Bitboard kRank1 = 0x00000000000000FFULL;
constexpr Bitboard kRank8 = 0xFF00000000000000ULL;
constexpr Bitboard kRank2 = 0x000000000000FF00ULL;
constexpr Bitboard kRank3 = 0x0000000000FF0000ULL;
constexpr Bitboard kRank4 = 0x00000000FF000000ULL;
constexpr Bitboard kRank5 = 0x000000FF00000000ULL;
constexpr Bitboard kRank6 = 0x0000FF0000000000ULL;
constexpr Bitboard kRank7 = 0x00FF000000000000ULL;

constexpr std::array<int, 8> kCastlingRookFiles = {7, 0, 7, 0};
constexpr std::array<int, 8> kCastlingRookRanks = {0, 0, 7, 7};
constexpr std::array<int, 8> kCastlingKingTargets = {6, 2, 6, 2};
constexpr std::array<int, 8> kCastlingRookTargets = {5, 3, 5, 3};

constexpr std::array<CastlingRights, 4> kCastlingRightsMask = {
    CastleWK, CastleWQ, CastleBK, CastleBQ};

constexpr std::array<std::uint8_t, 64> kCastlingClear = [] {
  std::array<std::uint8_t, 64> mask{};
  mask[static_cast<int>(Square::A1)] = CastleWQ;
  mask[static_cast<int>(Square::H1)] = CastleWK;
  mask[static_cast<int>(Square::A8)] = CastleBQ;
  mask[static_cast<int>(Square::H8)] = CastleBK;
  return mask;
}();

constexpr std::array<int, 8> kCastleRightKingFile = {4, 4, 4, 4, 4, 4, 4, 4};

constexpr std::array<std::uint8_t, 64> kSquareToFile = [] {
  std::array<std::uint8_t, 64> arr{};
  for (int sq = 0; sq < 64; ++sq) {
    arr[sq] = static_cast<std::uint8_t>(sq & 7);
  }
  return arr;
}();

constexpr std::array<std::uint8_t, 64> kSquareToRank = [] {
  std::array<std::uint8_t, 64> arr{};
  for (int sq = 0; sq < 64; ++sq) {
    arr[sq] = static_cast<std::uint8_t>(sq >> 3);
  }
  return arr;
}();

constexpr Bitboard north(Bitboard bb) { return bb << 8; }
constexpr Bitboard south(Bitboard bb) { return bb >> 8; }

constexpr Bitboard east(Bitboard bb) { return (bb << 1) & ~kFileA; }
constexpr Bitboard west(Bitboard bb) { return (bb >> 1) & ~kFileH; }

constexpr Bitboard north_east(Bitboard bb) { return (bb << 9) & ~kFileA; }
constexpr Bitboard north_west(Bitboard bb) { return (bb << 7) & ~kFileH; }
constexpr Bitboard south_east(Bitboard bb) { return (bb >> 7) & ~kFileA; }
constexpr Bitboard south_west(Bitboard bb) { return (bb >> 9) & ~kFileH; }

constexpr bool on_board(int file, int rank) {
  return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

#if defined(__SSE2__)
inline __m128i load_bb(Bitboard bb) {
  return _mm_cvtsi64_si128(static_cast<long long>(bb));
}

inline Bitboard store_bb(__m128i vec) {
  return static_cast<Bitboard>(_mm_cvtsi128_si64(vec));
}
#endif

inline Bitboard pawn_single_pushes(Color side, Bitboard pawns, Bitboard empty) {
#if defined(__SSE2__)
  __m128i pawn_vec = load_bb(pawns);
  __m128i empty_vec = load_bb(empty);
  __m128i shifted =
      side == Color::White ? _mm_slli_epi64(pawn_vec, 8) : _mm_srli_epi64(pawn_vec, 8);
  __m128i res = _mm_and_si128(shifted, empty_vec);
  return store_bb(res);
#else
  return side == Color::White ? (north(pawns) & empty) : (south(pawns) & empty);
#endif
}

inline Bitboard pawn_double_pushes(Color side, Bitboard pawns, Bitboard empty) {
  if (pawns == 0ULL) {
    return 0ULL;
  }
  const Bitboard first = pawn_single_pushes(side, pawns, empty);
  if (first == 0ULL) {
    return 0ULL;
  }
  return pawn_single_pushes(side, first, empty);
}

Bitboard between_squares(Square a, Square b) {
  Bitboard mask = 0ULL;
  const int file_a = static_cast<int>(file_of(a));
  const int rank_a = static_cast<int>(rank_of(a));
  const int file_b = static_cast<int>(file_of(b));
  const int rank_b = static_cast<int>(rank_of(b));

  int df = file_b - file_a;
  int dr = rank_b - rank_a;

  int step_file = (df > 0) - (df < 0);
  int step_rank = (dr > 0) - (dr < 0);

  if (df == 0 && dr == 0) {
    return 0ULL;
  }

  if (df == 0) {
    step_file = 0;
    step_rank = (dr > 0) ? 1 : -1;
  } else if (dr == 0) {
    step_rank = 0;
    step_file = (df > 0) ? 1 : -1;
  } else if (std::abs(df) == std::abs(dr)) {
    step_file = (df > 0) ? 1 : -1;
    step_rank = (dr > 0) ? 1 : -1;
  } else {
    return 0ULL;
  }

  int file = file_a + step_file;
  int rank = rank_a + step_rank;
  while (file != file_b || rank != rank_b) {
    mask |= bit(static_cast<Square>(rank * 8 + file));
    file += step_file;
    rank += step_rank;
  }

  mask &= ~bit(a);
  mask &= ~bit(b);
  return mask;
}

inline bool slider_attacks_square(Bitboard occ, Square target, Bitboard bishop_sliders,
                                  Bitboard rook_sliders) {
  const int file = static_cast<int>(file_of(target));
  const int rank = static_cast<int>(rank_of(target));

  const auto scan = [&](int df, int dr, Bitboard sliders) -> bool {
    int f = file + df;
    int r = rank + dr;
    while (on_board(f, r)) {
      const Square sq = static_cast<Square>(r * 8 + f);
      const Bitboard sq_bit = bit(sq);
      if (occ & sq_bit) {
        if (sliders & sq_bit) {
          return true;
        }
        break;
      }
      f += df;
      r += dr;
    }
    return false;
  };

  if (scan(1, 1, bishop_sliders) || scan(1, -1, bishop_sliders) ||
      scan(-1, 1, bishop_sliders) || scan(-1, -1, bishop_sliders)) {
    return true;
  }
  if (scan(1, 0, rook_sliders) || scan(-1, 0, rook_sliders) ||
      scan(0, 1, rook_sliders) || scan(0, -1, rook_sliders)) {
    return true;
  }
  return false;
}

struct ZobristTables {
  std::array<std::array<std::array<std::uint64_t, 64>, 6>, 2> piece{};
  std::array<std::uint64_t, 16> castling{};
  std::array<std::uint64_t, 8> ep{};
  std::uint64_t side{0};
};

constexpr std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

const ZobristTables& zobrist_tables() {
  static const ZobristTables tables = [] {
    ZobristTables t{};
    std::uint64_t seed = 0xBADC0FFEE0DDF00DULL;
    for (int color = 0; color < 2; ++color) {
      for (int type = 0; type < 6; ++type) {
        for (int sq = 0; sq < 64; ++sq) {
          t.piece[color][type][sq] = splitmix64(seed);
        }
      }
    }
    for (auto& castling_key : t.castling) {
      castling_key = splitmix64(seed);
    }
    for (auto& ep_key : t.ep) {
      ep_key = splitmix64(seed);
    }
    t.side = splitmix64(seed);
    return t;
  }();
  return tables;
}

bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

void ensure_attacks_ready() {
  static std::once_flag flag;
  std::call_once(flag, [] { init_attacks(false); });
}

}  // namespace

Position::Position() {
  ensure_attacks_ready();
  clear();
}

void Position::clear() {
  squares_.fill(Piece::None);
  for (auto& bb : pieces_) {
    bb.fill(0ULL);
  }
  occupied_.fill(0ULL);
  occupied_all_ = 0ULL;
  kings_.fill(Square::None);
  side_ = Color::White;
  castling_ = CastleNone;
  ep_square_ = Square::None;
  halfmove_clock_ = 0;
  fullmove_number_ = 1;
  zobrist_ = 0ULL;
}

Bitboard Position::pieces(Color color, PieceType type) const {
  return pieces_[color_index(color)][static_cast<int>(type)];
}

bool Position::is_sane(std::string* reason) const {
  const auto fail = [&](std::string_view msg) {
    if (reason) {
      reason->assign(msg);
    }
    return false;
  };

  bool white_king = false;
  bool black_king = false;
  Bitboard derived_occ[2] = {0ULL, 0ULL};
  Bitboard derived_all = 0ULL;

  for (int idx = 0; idx < 64; ++idx) {
    const Square sq = static_cast<Square>(idx);
    const Piece pc = squares_[idx];
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
    return fail(!white_king ? "white king missing" : "black king missing");
  }

  if (derived_occ[0] != occupied_[0]) {
    return fail("occupancy mismatch for white");
  }
  if (derived_occ[1] != occupied_[1]) {
    return fail("occupancy mismatch for black");
  }
  if (derived_all != occupied_all_) {
    return fail("aggregate occupancy mismatch");
  }

  const Square white_king_sq = kings_[color_index(Color::White)];
  const Square black_king_sq = kings_[color_index(Color::Black)];
  if (white_king_sq == Square::None ||
      squares_[static_cast<int>(white_king_sq)] != Piece::WKing) {
    return fail("white king square mismatch");
  }
  if (black_king_sq == Square::None ||
      squares_[static_cast<int>(black_king_sq)] != Piece::BKing) {
    return fail("black king square mismatch");
  }

  if (compute_zobrist() != zobrist_) {
    return fail("zobrist mismatch");
  }

  const Square ep = ep_square_;
  if (ep != Square::None) {
    const Rank ep_rank = rank_of(ep);
    if (ep_rank != Rank::R3 && ep_rank != Rank::R6) {
      return fail("invalid en passant rank");
    }
    const Color mover = (ep_rank == Rank::R3) ? Color::White : Color::Black;
    const int dir = mover == Color::White ? 8 : -8;
    const int pawn_idx = static_cast<int>(ep) + dir;
    if (pawn_idx < 0 || pawn_idx >= 64 ||
        squares_[pawn_idx] != make_piece(mover, PieceType::Pawn)) {
      return fail("en passant pawn missing");
    }
  }

  return true;
}

Position Position::from_fen(std::string_view fen, bool strict) {
  Position pos;
  pos.clear();

  std::array<std::string_view, 6> fields{};
  std::size_t field_idx = 0;
  std::size_t start = 0;
  for (std::size_t i = 0; i <= fen.size(); ++i) {
    if (i == fen.size() || fen[i] == ' ') {
      if (field_idx < fields.size()) {
        fields[field_idx++] = fen.substr(start, i - start);
      }
      start = i + 1;
    }
  }

  if (field_idx < 4) {
    throw std::runtime_error("FEN requires at least 4 fields");
  }

  // Piece placement.
  const auto placement = fields[0];
  int sq_index = 56;  // start at a8
  for (std::size_t i = 0; i < placement.size(); ++i) {
    const char c = placement[i];
    if (c == '/') {
      sq_index -= 16;
      continue;
    }
    if (is_digit(c)) {
      sq_index += (c - '0');
      continue;
    }
    const Piece pc = piece_from_char(c);
    if (pc == Piece::None) {
      throw std::runtime_error("Invalid piece in FEN");
    }
    const auto sq = static_cast<Square>(sq_index);
    pos.put_piece(pc, sq);
    ++sq_index;
  }

  // Side to move.
  pos.side_ = (fields[1] == "b") ? Color::Black : Color::White;

  // Castling rights.
  const auto castling_field = fields[2];
  if (castling_field != "-") {
    for (char c : castling_field) {
      switch (c) {
        case 'K':
          pos.castling_ |= CastleWK;
          break;
        case 'Q':
          pos.castling_ |= CastleWQ;
          break;
        case 'k':
          pos.castling_ |= CastleBK;
          break;
        case 'q':
          pos.castling_ |= CastleBQ;
          break;
        default:
          if (strict) {
            throw std::runtime_error("Invalid castling rights");
          }
      }
    }
  }

  // En passant.
  const auto ep_field = fields[3];
  if (ep_field != "-") {
    const Square ep_sq = square_from_string(ep_field);
    if (ep_sq != Square::None) {
      pos.ep_square_ = ep_sq;
    } else if (strict) {
      throw std::runtime_error("Invalid en passant square");
    }
  }

  pos.halfmove_clock_ = 0;
  pos.fullmove_number_ = 1;

  if (field_idx >= 5) {
    std::from_chars(fields[4].data(),
                    fields[4].data() + fields[4].size(),
                    pos.halfmove_clock_);
  }
  if (field_idx >= 6) {
    std::from_chars(fields[5].data(),
                    fields[5].data() + fields[5].size(),
                    pos.fullmove_number_);
  }

  pos.recompute_occupancy();
  pos.recompute_zobrist();
  if (pos.side_ == Color::Black) {
    pos.zobrist_ ^= zobrist_tables().side;
  }

  return pos;
}

std::string Position::to_fen() const {
  std::ostringstream oss;
  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const auto sq = static_cast<Square>(rank * 8 + file);
      const Piece pc = squares_[static_cast<int>(sq)];
      if (pc == Piece::None) {
        ++empty;
      } else {
        if (empty > 0) {
          oss << empty;
          empty = 0;
        }
        oss << piece_to_char(pc);
      }
    }
    if (empty > 0) {
      oss << empty;
    }
    if (rank > 0) {
      oss << '/';
    }
  }
  oss << ' ';
  oss << (side_ == Color::White ? 'w' : 'b');
  oss << ' ';
  if (castling_ == CastleNone) {
    oss << '-';
  } else {
    if (castling_ & CastleWK) oss << 'K';
    if (castling_ & CastleWQ) oss << 'Q';
    if (castling_ & CastleBK) oss << 'k';
    if (castling_ & CastleBQ) oss << 'q';
  }
  oss << ' ';
  oss << (ep_square_ == Square::None ? "-" : square_to_string(ep_square_));
  oss << ' ';
  oss << static_cast<int>(halfmove_clock_);
  oss << ' ';
  oss << static_cast<int>(fullmove_number_);
  return oss.str();
}

bool Position::in_check(Color color) const {
  const Square king_sq = kings_[color_index(color)];
  if (king_sq == Square::None) {
    return false;
  }
  return is_square_attacked(king_sq, flip(color));
}

void Position::generate_moves(MoveList& out, GenStage stage) const {
  const bool trace_moves = trace_enabled(TraceTopic::Moves);
  std::array<Move, 8> samples{};
  std::size_t sample_count = 0;

  MoveList pseudo;
  generate_pseudo_legal(pseudo);
  out.clear();

  const Color us = side_;
  const Color them = flip(us);
  std::array<Bitboard, 64> pin_masks{};
  const Bitboard pinned = pinned_mask(us, pin_masks);
  Bitboard checkers;
  const bool double_check = in_double_check(us, checkers);
  const bool in_check_now = checkers != 0ULL;
  const Square king_sq = kings_[color_index(us)];

  Bitboard enemy_pawn_attacks = 0ULL;
  Bitboard pawns = pieces_[color_index(them)][static_cast<int>(PieceType::Pawn)];
  while (pawns) {
    const int sq_idx = __builtin_ctzll(pawns);
    pawns &= pawns - 1;
    const Square sq = static_cast<Square>(sq_idx);
    enemy_pawn_attacks |= pawn_attacks(them, sq);
  }

  Bitboard enemy_knight_attacks = 0ULL;
  Bitboard knights = pieces_[color_index(them)][static_cast<int>(PieceType::Knight)];
  while (knights) {
    const int sq_idx = __builtin_ctzll(knights);
    knights &= knights - 1;
    enemy_knight_attacks |= knight_attacks(static_cast<Square>(sq_idx));
  }

  Bitboard enemy_king_attacks = 0ULL;
  const Square enemy_king_sq = kings_[color_index(them)];
  if (enemy_king_sq != Square::None) {
    enemy_king_attacks = king_attacks(enemy_king_sq);
  }

  const Bitboard enemy_bishop_sliders =
      pieces_[color_index(them)][static_cast<int>(PieceType::Bishop)] |
      pieces_[color_index(them)][static_cast<int>(PieceType::Queen)];
  const Bitboard enemy_rook_sliders =
      pieces_[color_index(them)][static_cast<int>(PieceType::Rook)] |
      pieces_[color_index(them)][static_cast<int>(PieceType::Queen)];

  Bitboard capture_block = ~0ULL;
  if (in_check_now) {
    if (double_check) {
      capture_block = 0ULL;
    } else {
      const Square checker_sq = static_cast<Square>(__builtin_ctzll(checkers));
      capture_block = bit(checker_sq);
      const Piece checker_piece = piece_on(checker_sq);
      const PieceType checker_type = type_of(checker_piece);
      if (checker_type == PieceType::Bishop || checker_type == PieceType::Rook ||
          checker_type == PieceType::Queen) {
        capture_block |= between_squares(king_sq, checker_sq);
      }
    }
  }

  for (const Move move : pseudo) {
    const MoveFlag flag = move_flag(move);
    const bool is_capture = flag == MoveFlag::Capture ||
                            flag == MoveFlag::EnPassant ||
                            flag == MoveFlag::PromotionCapture;
    const bool is_quiet = flag == MoveFlag::Quiet ||
                          flag == MoveFlag::DoublePush ||
                          flag == MoveFlag::KingCastle ||
                          flag == MoveFlag::QueenCastle ||
                          flag == MoveFlag::Promotion;
    if (stage == GenStage::Captures && !is_capture) {
      continue;
    }
    if (stage == GenStage::Quiets && !is_quiet) {
      continue;
    }

    const Square from = from_square(move);
    const Square to = to_square(move);
    const Piece moving_piece = piece_on(from);
    const PieceType moving_type = type_of(moving_piece);
    const Bitboard from_mask = bit(from);
    const Bitboard to_mask = bit(to);

    bool needs_validation = false;

    if (moving_type == PieceType::King) {
      if (flag == MoveFlag::KingCastle) {
        needs_validation = true;
        const Square mid = static_cast<Square>(static_cast<int>(king_sq) + 1);
        const Square dest = static_cast<Square>(static_cast<int>(king_sq) + 2);
        const Bitboard check_mask = bit(mid) | bit(dest);
        if ((enemy_pawn_attacks | enemy_knight_attacks | enemy_king_attacks) & check_mask) {
          continue;
        }
      } else if (flag == MoveFlag::QueenCastle) {
        needs_validation = true;
        const Square mid = static_cast<Square>(static_cast<int>(king_sq) - 1);
        const Square dest = static_cast<Square>(static_cast<int>(king_sq) - 2);
        const Bitboard check_mask = bit(mid) | bit(dest);
        if ((enemy_pawn_attacks | enemy_knight_attacks | enemy_king_attacks) & check_mask) {
          continue;
        }
      } else {
        if (enemy_pawn_attacks & to_mask) {
          continue;
        }
        if (enemy_knight_attacks & to_mask) {
          continue;
        }
        if (enemy_king_attacks & to_mask) {
          continue;
        }

        Bitboard occ = occupied_all_;
        occ ^= from_mask;
        Bitboard bishop_sliders = enemy_bishop_sliders;
        Bitboard rook_sliders = enemy_rook_sliders;
        if (is_capture) {
          Piece captured_piece = piece_on(to);
          if (captured_piece != Piece::None) {
            const PieceType captured_type = type_of(captured_piece);
            if (captured_type == PieceType::Bishop || captured_type == PieceType::Queen) {
              bishop_sliders &= ~to_mask;
            }
            if (captured_type == PieceType::Rook || captured_type == PieceType::Queen) {
              rook_sliders &= ~to_mask;
            }
            occ ^= to_mask;
          }
        }
        occ |= to_mask;
        if (slider_attacks_square(occ, to, bishop_sliders, rook_sliders)) {
          continue;
        }
        needs_validation = false;
      }
    } else {
      if (double_check) {
        continue;
      }
      if (in_check_now) {
        if (flag == MoveFlag::EnPassant) {
          needs_validation = true;
        } else if (!(capture_block & to_mask)) {
          continue;
        }
      }
      if (pinned & from_mask) {
        if (!(pin_masks[static_cast<int>(from)] & to_mask)) {
          continue;
        }
      }
    }

    if (flag == MoveFlag::EnPassant) {
      needs_validation = true;
    }

    if (needs_validation) {
      auto& mutable_self = const_cast<Position&>(*this);
      Undo u;
      mutable_self.make(move, u);
      const bool still_in_check = mutable_self.in_check(us);
      mutable_self.unmake(move, u);
      if (still_in_check) {
        continue;
      }
    }

    out.push_back(move);
    if (trace_moves && sample_count < samples.size()) {
      samples[sample_count++] = move;
    }
  }

  if (trace_moves) {
    const char* stage_name = "all";
    switch (stage) {
      case GenStage::Captures:
        stage_name = "captures";
        break;
      case GenStage::Quiets:
        stage_name = "quiets";
        break;
      case GenStage::All:
        stage_name = "all";
        break;
    }
    std::ostringstream oss;
    oss << "stage=" << stage_name
        << " stm=" << (side_ == Color::White ? "white" : "black")
        << " pseudo=" << pseudo.size()
        << " legal=" << out.size();
    if (sample_count > 0) {
      oss << " moves=";
      for (std::size_t idx = 0; idx < sample_count; ++idx) {
        if (idx > 0) {
          oss << ',';
        }
        oss << move_to_uci(samples[idx]);
      }
    }
    trace_emit(TraceTopic::Moves, oss.str());
  }
}


bool Position::is_legal(Move m) const {
  Undo u;
  const_cast<Position*>(this)->make(m, u);
  const bool legal = !const_cast<Position*>(this)->in_check(flip(side_));
  const_cast<Position*>(this)->unmake(m, u);
  return legal;
}

void Position::make(Move m, Undo& undo) {
#if !defined(NDEBUG)
  static thread_local std::uint32_t s_debug_counter = 0;
  if ((++s_debug_counter & 0x3FFU) == 0U) {
    BBY_INVARIANT(is_sane());
    BBY_INVARIANT(compute_zobrist() == zobrist_);
  }
#endif
  const Square from = from_square(m);
  const Square to = to_square(m);
  Piece moving = squares_[static_cast<int>(from)];
  BBY_ASSERT(moving != Piece::None);

  const auto& tables = zobrist_tables();
  const auto apply_castling = [&](std::uint8_t new_castling) {
    if (new_castling != castling_) {
      zobrist_ ^= tables.castling[castling_];
      castling_ = new_castling;
      zobrist_ ^= tables.castling[castling_];
    }
  };
  undo.key = zobrist_;
  undo.move = m;
  undo.castling = castling_;
  undo.en_passant = ep_square_;
  undo.halfmove_clock = halfmove_clock_;
  undo.captured = Piece::None;

  const MoveFlag flag = move_flag(m);
  set_en_passant(Square::None);

  const PieceType origin_type = type_of(moving);
  const bool is_quiet_move = flag == MoveFlag::Quiet;
  const bool is_double_push =
      (flag == MoveFlag::DoublePush && origin_type == PieceType::Pawn);
  const bool quiet_like = is_quiet_move || is_double_push;
  const int from_idx = static_cast<int>(from);
  const int to_idx = static_cast<int>(to);
  const Bitboard from_mask = bit(from);
  const Bitboard to_mask = bit(to);
  const int mover_idx = color_index(side_);
  const int moving_type_idx = static_cast<int>(origin_type);
  if (quiet_like && squares_[to_idx] == Piece::None) {
    BBY_ASSERT(origin_type != PieceType::King || is_quiet_move);
    const Bitboard shift_mask = from_mask | to_mask;
    pieces_[mover_idx][moving_type_idx] ^= shift_mask;
    occupied_[mover_idx] ^= shift_mask;
    occupied_all_ ^= shift_mask;
    zobrist_ ^= tables.piece[mover_idx][moving_type_idx][from_idx] ^
                tables.piece[mover_idx][moving_type_idx][to_idx];

    squares_[from_idx] = Piece::None;
    squares_[to_idx] = moving;

    halfmove_clock_ = (origin_type == PieceType::Pawn)
                          ? 0
                          : static_cast<std::uint8_t>(halfmove_clock_ + 1);

    if (origin_type == PieceType::King) {
      kings_[mover_idx] = to;
    }

    std::uint8_t new_castling = castling_;
    new_castling &=
        static_cast<std::uint8_t>(~kCastlingClear[from_idx]);
    new_castling &=
        static_cast<std::uint8_t>(~kCastlingClear[to_idx]);
    if (origin_type == PieceType::King) {
      if (side_ == Color::White) {
        new_castling &= static_cast<std::uint8_t>(~(CastleWK | CastleWQ));
      } else {
        new_castling &= static_cast<std::uint8_t>(~(CastleBK | CastleBQ));
      }
    }
    apply_castling(new_castling);

    if (is_double_push) {
      const int dir = (side_ == Color::White) ? 8 : -8;
      const Square ep_target =
          static_cast<Square>(static_cast<int>(from) + dir);
      set_en_passant(ep_target);
    }

    if (side_ == Color::Black) {
      ++fullmove_number_;
    }

    side_ = flip(side_);
    zobrist_ ^= tables.side;

    undo.captured = Piece::None;
    return;
  }

  Square capture_sq = Square::None;
  if (flag == MoveFlag::EnPassant) {
    const int dir = (side_ == Color::White) ? -8 : 8;
    capture_sq = static_cast<Square>(static_cast<int>(to) + dir);
    undo.captured = squares_[static_cast<int>(capture_sq)];
    remove_piece(undo.captured, capture_sq);
  } else if (squares_[static_cast<int>(to)] != Piece::None) {
    capture_sq = to;
    undo.captured = squares_[static_cast<int>(to)];
    remove_piece(undo.captured, to);
  }

  remove_piece(moving, from);

  if (flag == MoveFlag::Promotion || flag == MoveFlag::PromotionCapture) {
    moving = make_piece(side_, promotion_type(m));
  }

  put_piece(moving, to);

  if (flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle) {
    const bool king_side = flag == MoveFlag::KingCastle;
    const int rook_file = king_side ? 7 : 0;
    const int rook_target = king_side ? 5 : 3;
    const Square rook_from =
        static_cast<Square>(static_cast<int>(rank_of(to)) * 8 + rook_file);
    const Square rook_to =
        static_cast<Square>(static_cast<int>(rank_of(to)) * 8 + rook_target);
    Piece rook = squares_[static_cast<int>(rook_from)];
    remove_piece(rook, rook_from);
    put_piece(rook, rook_to);
  }

  if (flag == MoveFlag::DoublePush) {
    const int dir = (side_ == Color::White) ? 8 : -8;
    const Square ep_target =
        static_cast<Square>(static_cast<int>(from) + dir);
    set_en_passant(ep_target);
  }

  if (origin_type == PieceType::King) {
    kings_[mover_idx] = to;
  }

  std::uint8_t new_castling = castling_;
  new_castling &= static_cast<std::uint8_t>(~kCastlingClear[from_idx]);
  new_castling &= static_cast<std::uint8_t>(~kCastlingClear[to_idx]);
  if (origin_type == PieceType::King) {
    if (side_ == Color::White) {
      new_castling &= static_cast<std::uint8_t>(~(CastleWK | CastleWQ));
    } else {
      new_castling &= static_cast<std::uint8_t>(~(CastleBK | CastleBQ));
    }
  }

  if (undo.captured != Piece::None && type_of(undo.captured) == PieceType::Rook) {
    const Square affected =
        capture_sq == Square::None ? to : capture_sq;
    new_castling &=
        static_cast<std::uint8_t>(
            ~kCastlingClear[static_cast<int>(affected)]);
  }
  apply_castling(new_castling);

  halfmove_clock_ = (origin_type == PieceType::Pawn || undo.captured != Piece::None)
                        ? 0
                        : static_cast<std::uint8_t>(halfmove_clock_ + 1);

  if (side_ == Color::Black) {
    ++fullmove_number_;
  }

  side_ = flip(side_);
  zobrist_ ^= tables.side;

  // En passant key update handled in set_en_passant.
}

void Position::unmake(Move m, const Undo& undo) {
  const Square from = from_square(m);
  const Square to = to_square(m);
  Piece moving = squares_[static_cast<int>(to)];
  const MoveFlag flag = move_flag(m);

  const int from_idx = static_cast<int>(from);
  const int to_idx = static_cast<int>(to);
  const Bitboard from_mask = bit(from);
  const Bitboard to_mask = bit(to);

  side_ = flip(side_);
  const auto& tables = zobrist_tables();
  zobrist_ ^= tables.side;

  const int mover_idx = color_index(side_);
  const PieceType moving_type = type_of(moving);
  const int moving_type_idx = static_cast<int>(moving_type);
  const bool quiet_like =
      (flag == MoveFlag::Quiet || flag == MoveFlag::DoublePush);

  const bool fast_path =
      quiet_like && undo.captured == Piece::None &&
      moving_type != PieceType::King;

  if (fast_path) {
    const Bitboard shift_mask = from_mask | to_mask;
    pieces_[mover_idx][moving_type_idx] ^= shift_mask;
    occupied_[mover_idx] ^= shift_mask;
    occupied_all_ ^= shift_mask;
    zobrist_ ^= tables.piece[mover_idx][moving_type_idx][to_idx] ^
                tables.piece[mover_idx][moving_type_idx][from_idx];
    squares_[to_idx] = Piece::None;
    squares_[from_idx] = moving;
  } else {
    if (flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle) {
      const bool king_side = flag == MoveFlag::KingCastle;
      const int rook_target = king_side ? 5 : 3;
      const int rook_file = king_side ? 7 : 0;
      const Square rook_from =
          static_cast<Square>(static_cast<int>(rank_of(to)) * 8 + rook_target);
      const Square rook_to =
          static_cast<Square>(static_cast<int>(rank_of(to)) * 8 + rook_file);
      Piece rook = squares_[static_cast<int>(rook_from)];
      remove_piece(rook, rook_from);
      put_piece(rook, rook_to);
    }

    remove_piece(moving, to);
    if (flag == MoveFlag::Promotion || flag == MoveFlag::PromotionCapture) {
      moving = make_piece(side_, PieceType::Pawn);
    }
    put_piece(moving, from);

    if (flag == MoveFlag::EnPassant) {
      const int dir = (side_ == Color::White) ? -8 : 8;
      const Square capture_sq = static_cast<Square>(static_cast<int>(to) + dir);
      put_piece(undo.captured, capture_sq);
    } else if (undo.captured != Piece::None) {
      put_piece(undo.captured, to);
    }

    if (type_of(moving) == PieceType::King) {
      kings_[color_index(side_)] = from;
    }
  }

  castling_ = undo.castling;
  set_en_passant(undo.en_passant);
  halfmove_clock_ = undo.halfmove_clock;
  if (side_ == Color::Black) {
    --fullmove_number_;
  }

  zobrist_ = undo.key;
}

void Position::put_piece(Piece pc, Square sq) {
  const int idx = static_cast<int>(sq);
  if (squares_[idx] != Piece::None) {
    remove_piece(squares_[idx], sq);
  }
  squares_[idx] = pc;
  if (pc == Piece::None) {
    return;
  }
  const Color c = color_of(pc);
  const auto type = type_of(pc);
  const Bitboard mask = bit(sq);
  pieces_[color_index(c)][static_cast<int>(type)] |= mask;
  occupied_[color_index(c)] |= mask;
  occupied_all_ |= mask;
  if (type == PieceType::King) {
    kings_[color_index(c)] = sq;
  }
  zobrist_ ^= zobrist_tables().piece[color_index(c)][static_cast<int>(type)][idx];
}

void Position::remove_piece(Piece pc, Square sq) {
  if (pc == Piece::None) {
    return;
  }
  const int idx = static_cast<int>(sq);
  squares_[idx] = Piece::None;
  const Color c = color_of(pc);
  const auto type = type_of(pc);
  const Bitboard mask = bit(sq);
  pieces_[color_index(c)][static_cast<int>(type)] &= ~mask;
  occupied_[color_index(c)] &= ~mask;
  occupied_all_ &= ~mask;
  zobrist_ ^= zobrist_tables().piece[color_index(c)][static_cast<int>(type)][idx];
  if (type == PieceType::King) {
    kings_[color_index(c)] = Square::None;
  }
}

void Position::set_castling(std::uint8_t rights) {
  const auto& tables = zobrist_tables();
  zobrist_ ^= tables.castling[castling_];
  castling_ = rights;
  zobrist_ ^= tables.castling[castling_];
}

void Position::set_en_passant(Square sq) {
  const auto& tables = zobrist_tables();
  if (ep_square_ != Square::None) {
    const auto file = static_cast<int>(file_of(ep_square_));
    zobrist_ ^= tables.ep[file];
  }
  ep_square_ = sq;
  if (ep_square_ != Square::None) {
    const auto file = static_cast<int>(file_of(ep_square_));
    zobrist_ ^= tables.ep[file];
  }
}

void Position::recompute_occupancy() {
  occupied_.fill(0ULL);
  occupied_all_ = 0ULL;
  for (int color = 0; color < 2; ++color) {
    for (int type = 0; type < 6; ++type) {
      occupied_[color] |= pieces_[color][type];
    }
  }
  occupied_all_ = occupied_[0] | occupied_[1];
}

void Position::recompute_zobrist() {
  zobrist_ = compute_zobrist();
}

std::uint64_t Position::compute_zobrist() const {
  std::uint64_t value = 0ULL;
  const auto& tables = zobrist_tables();
  for (int sq = 0; sq < 64; ++sq) {
    const Piece pc = squares_[sq];
    if (pc == Piece::None) {
      continue;
    }
    const Color c = color_of(pc);
    const auto type = type_of(pc);
    value ^= tables.piece[color_index(c)][static_cast<int>(type)][sq];
  }
  value ^= tables.castling[castling_];
  if (ep_square_ != Square::None) {
    value ^= tables.ep[static_cast<int>(file_of(ep_square_))];
  }
  if (side_ == Color::Black) {
    value ^= tables.side;
  }
  return value;
}

bool Position::is_square_attacked(Square sq, Color by) const {
  const int attacker = color_index(by);

  const Bitboard pawns = pieces_[attacker][static_cast<int>(PieceType::Pawn)];
  if (pawn_attacks(flip(by), sq) & pawns) {
    return true;
  }

  if (knight_attacks(sq) &
      pieces_[attacker][static_cast<int>(PieceType::Knight)]) {
    return true;
  }

  Bitboard bishops =
      pieces_[attacker][static_cast<int>(PieceType::Bishop)] |
      pieces_[attacker][static_cast<int>(PieceType::Queen)];
  if (bishop_attacks(sq, occupied_all_) & bishops) {
    return true;
  }

  Bitboard rooks =
      pieces_[attacker][static_cast<int>(PieceType::Rook)] |
      pieces_[attacker][static_cast<int>(PieceType::Queen)];
  if (rook_attacks(sq, occupied_all_) & rooks) {
    return true;
  }

  if (king_attacks(sq) &
      pieces_[attacker][static_cast<int>(PieceType::King)]) {
    return true;
  }

  return false;
}

void Position::generate_pseudo_legal(MoveList& out) const {
  out.clear();

  const int us = color_index(side_);
  const int them = color_index(flip(side_));
  const Bitboard ours = occupied_[us];
  const Bitboard theirs = occupied_[them];
  const Bitboard empty = ~occupied_all_;

  auto emit = [&](Square from, Square to, MoveFlag flag,
                  PieceType promo = PieceType::None) {
    out.push_back(make_move(from, to, flag, promo));
  };

  const Bitboard pawns = pieces_[us][static_cast<int>(PieceType::Pawn)];
  if (side_ == Color::White) {
    Bitboard single = pawn_single_pushes(Color::White, pawns, empty);
    Bitboard promotions = single & kRank8;
    Bitboard quiets = single & ~kRank8;
    while (quiets) {
      const int to_idx = __builtin_ctzll(quiets);
      quiets &= quiets - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx - 8);
      emit(from, to, MoveFlag::Quiet);
    }

    Bitboard start_rank = pawns & kRank2;
    Bitboard double_push = pawn_double_pushes(Color::White, start_rank, empty);
    while (double_push) {
      const int to_idx = __builtin_ctzll(double_push);
      double_push &= double_push - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx - 16);
      emit(from, to, MoveFlag::DoublePush);
    }

    while (promotions) {
      const int to_idx = __builtin_ctzll(promotions);
      promotions &= promotions - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx - 8);
      for (PieceType promo : {PieceType::Queen, PieceType::Rook,
                              PieceType::Bishop, PieceType::Knight}) {
        emit(from, to, MoveFlag::Promotion, promo);
      }
    }

    Bitboard capture_left = north_west(pawns) & theirs;
    while (capture_left) {
      const int to_idx = __builtin_ctzll(capture_left);
      capture_left &= capture_left - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx - 7);
      const bool promo = rank_of(to) == Rank::R8;
      if (promo) {
        for (PieceType promo_type : {PieceType::Queen, PieceType::Rook,
                                     PieceType::Bishop, PieceType::Knight}) {
          emit(from, to, MoveFlag::PromotionCapture, promo_type);
        }
      } else {
        emit(from, to, MoveFlag::Capture);
      }
    }

    Bitboard capture_right = north_east(pawns) & theirs;
    while (capture_right) {
      const int to_idx = __builtin_ctzll(capture_right);
      capture_right &= capture_right - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx - 9);
      const bool promo = rank_of(to) == Rank::R8;
      if (promo) {
        for (PieceType promo_type : {PieceType::Queen, PieceType::Rook,
                                     PieceType::Bishop, PieceType::Knight}) {
          emit(from, to, MoveFlag::PromotionCapture, promo_type);
        }
      } else {
        emit(from, to, MoveFlag::Capture);
      }
    }

    if (ep_square_ != Square::None && rank_of(ep_square_) == Rank::R6) {
      const int to_idx = static_cast<int>(ep_square_);
      const int file = to_idx & 7;
      if (file > 0) {
        const int from_idx = to_idx - 9;
        if (from_idx >= 0 &&
            squares_[from_idx] == make_piece(Color::White, PieceType::Pawn)) {
          emit(static_cast<Square>(from_idx), ep_square_, MoveFlag::EnPassant);
        }
      }
      if (file < 7) {
        const int from_idx = to_idx - 7;
        if (from_idx >= 0 &&
            squares_[from_idx] == make_piece(Color::White, PieceType::Pawn)) {
          emit(static_cast<Square>(from_idx), ep_square_, MoveFlag::EnPassant);
        }
      }
    }
  } else {
    Bitboard single = pawn_single_pushes(Color::Black, pawns, empty);
    Bitboard promotions = single & kRank1;
    Bitboard quiets = single & ~kRank1;
    while (quiets) {
      const int to_idx = __builtin_ctzll(quiets);
      quiets &= quiets - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx + 8);
      emit(from, to, MoveFlag::Quiet);
    }

    Bitboard start_rank = pawns & kRank7;
    Bitboard double_push = pawn_double_pushes(Color::Black, start_rank, empty);
    while (double_push) {
      const int to_idx = __builtin_ctzll(double_push);
      double_push &= double_push - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx + 16);
      emit(from, to, MoveFlag::DoublePush);
    }

    while (promotions) {
      const int to_idx = __builtin_ctzll(promotions);
      promotions &= promotions - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx + 8);
      for (PieceType promo : {PieceType::Queen, PieceType::Rook,
                              PieceType::Bishop, PieceType::Knight}) {
        emit(from, to, MoveFlag::Promotion, promo);
      }
    }

    Bitboard capture_left = south_west(pawns) & theirs;
    while (capture_left) {
      const int to_idx = __builtin_ctzll(capture_left);
      capture_left &= capture_left - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx + 9);
      const bool promo = rank_of(to) == Rank::R1;
      if (promo) {
        for (PieceType promo_type : {PieceType::Queen, PieceType::Rook,
                                     PieceType::Bishop, PieceType::Knight}) {
          emit(from, to, MoveFlag::PromotionCapture, promo_type);
        }
      } else {
        emit(from, to, MoveFlag::Capture);
      }
    }

    Bitboard capture_right = south_east(pawns) & theirs;
    while (capture_right) {
      const int to_idx = __builtin_ctzll(capture_right);
      capture_right &= capture_right - 1;
      const Square to = static_cast<Square>(to_idx);
      const Square from = static_cast<Square>(to_idx + 7);
      const bool promo = rank_of(to) == Rank::R1;
      if (promo) {
        for (PieceType promo_type : {PieceType::Queen, PieceType::Rook,
                                     PieceType::Bishop, PieceType::Knight}) {
          emit(from, to, MoveFlag::PromotionCapture, promo_type);
        }
      } else {
        emit(from, to, MoveFlag::Capture);
      }
    }

    if (ep_square_ != Square::None && rank_of(ep_square_) == Rank::R3) {
      const int to_idx = static_cast<int>(ep_square_);
      const int file = to_idx & 7;
      if (file > 0) {
        const int from_idx = to_idx + 7;
        if (from_idx < 64 &&
            squares_[from_idx] == make_piece(Color::Black, PieceType::Pawn)) {
          emit(static_cast<Square>(from_idx), ep_square_, MoveFlag::EnPassant);
        }
      }
      if (file < 7) {
        const int from_idx = to_idx + 9;
        if (from_idx < 64 &&
            squares_[from_idx] == make_piece(Color::Black, PieceType::Pawn)) {
          emit(static_cast<Square>(from_idx), ep_square_, MoveFlag::EnPassant);
        }
      }
    }
  }

  Bitboard knights = pieces_[us][static_cast<int>(PieceType::Knight)];
  while (knights) {
    const int from_idx = __builtin_ctzll(knights);
    knights &= knights - 1;
    const Square from = static_cast<Square>(from_idx);
    Bitboard moves = knight_attacks(from) & ~ours;
    while (moves) {
      const int to_idx = __builtin_ctzll(moves);
      moves &= moves - 1;
      const Square to = static_cast<Square>(to_idx);
      const bool capture = (theirs & bit(to)) != 0;
      emit(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet);
    }
  }

  auto emit_sliders = [&](Bitboard pieces, auto attack_fn) {
    while (pieces) {
      const int from_idx = __builtin_ctzll(pieces);
      pieces &= pieces - 1;
      const Square from = static_cast<Square>(from_idx);
      Bitboard moves = attack_fn(from, occupied_all_) & ~ours;
      while (moves) {
        const int to_idx = __builtin_ctzll(moves);
        moves &= moves - 1;
        const Square to = static_cast<Square>(to_idx);
        const bool capture = (theirs & bit(to)) != 0;
        emit(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet);
      }
    }
  };

  emit_sliders(pieces_[us][static_cast<int>(PieceType::Bishop)],
               [&](Square sq, Bitboard occ) { return bishop_attacks(sq, occ); });
  emit_sliders(pieces_[us][static_cast<int>(PieceType::Rook)],
               [&](Square sq, Bitboard occ) { return rook_attacks(sq, occ); });
  emit_sliders(pieces_[us][static_cast<int>(PieceType::Queen)],
               [&](Square sq, Bitboard occ) {
                 return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
               });

  Bitboard king_bb = pieces_[us][static_cast<int>(PieceType::King)];
  if (king_bb) {
    const int from_idx = __builtin_ctzll(king_bb);
    const Square from = static_cast<Square>(from_idx);
    Bitboard moves = king_attacks(from) & ~ours;
    while (moves) {
      const int to_idx = __builtin_ctzll(moves);
      moves &= moves - 1;
      const Square to = static_cast<Square>(to_idx);
      const bool capture = (theirs & bit(to)) != 0;
      emit(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet);
    }

    const Color enemy = flip(side_);
    if (side_ == Color::White) {
      if ((castling_ & CastleWK) &&
          !(occupancy() & (bit(Square::F1) | bit(Square::G1))) &&
          !is_square_attacked(Square::E1, enemy) &&
          !is_square_attacked(Square::F1, enemy) &&
          !is_square_attacked(Square::G1, enemy)) {
        emit(Square::E1, Square::G1, MoveFlag::KingCastle);
      }
      if ((castling_ & CastleWQ) &&
          !(occupancy() &
            (bit(Square::D1) | bit(Square::C1) | bit(Square::B1))) &&
          !is_square_attacked(Square::E1, enemy) &&
          !is_square_attacked(Square::D1, enemy) &&
          !is_square_attacked(Square::C1, enemy)) {
        emit(Square::E1, Square::C1, MoveFlag::QueenCastle);
      }
    } else {
      if ((castling_ & CastleBK) &&
          !(occupancy() & (bit(Square::F8) | bit(Square::G8))) &&
          !is_square_attacked(Square::E8, enemy) &&
          !is_square_attacked(Square::F8, enemy) &&
          !is_square_attacked(Square::G8, enemy)) {
        emit(Square::E8, Square::G8, MoveFlag::KingCastle);
      }
      if ((castling_ & CastleBQ) &&
          !(occupancy() &
            (bit(Square::D8) | bit(Square::C8) | bit(Square::B8))) &&
          !is_square_attacked(Square::E8, enemy) &&
          !is_square_attacked(Square::D8, enemy) &&
          !is_square_attacked(Square::C8, enemy)) {
        emit(Square::E8, Square::C8, MoveFlag::QueenCastle);
      }
    }
  }
}

Bitboard Position::pinned_mask(Color us, std::array<Bitboard, 64>& pin_masks) const {
  pin_masks.fill(0ULL);
  Bitboard pinned = 0ULL;
  const Color them = flip(us);
  const Square king_sq = kings_[color_index(us)];
  const int king_file = static_cast<int>(file_of(king_sq));
  const int king_rank = static_cast<int>(rank_of(king_sq));

  const Bitboard enemy_rooks = pieces_[color_index(them)][static_cast<int>(PieceType::Rook)] |
                               pieces_[color_index(them)][static_cast<int>(PieceType::Queen)];
  const Bitboard enemy_bishops = pieces_[color_index(them)][static_cast<int>(PieceType::Bishop)] |
                                 pieces_[color_index(them)][static_cast<int>(PieceType::Queen)];

  auto trace_direction = [&](int df, int dr, bool diagonal) {
    int file = king_file + df;
    int rank = king_rank + dr;
    Square candidate = Square::None;
    bool found_friend = false;
    while (on_board(file, rank)) {
      const Square sq = static_cast<Square>(rank * 8 + file);
      const Piece pc = squares_[rank * 8 + file];
      if (pc == Piece::None) {
        file += df;
        rank += dr;
        continue;
      }
      if (color_of(pc) == us) {
        if (found_friend) {
          break;
        }
        found_friend = true;
        candidate = sq;
        file += df;
        rank += dr;
        continue;
      }

      const bool attacks = diagonal
                               ? (enemy_bishops & bit(sq)) != 0ULL
                               : (enemy_rooks & bit(sq)) != 0ULL;
      if (attacks && found_friend) {
        pinned |= bit(candidate);
        pin_masks[static_cast<int>(candidate)] =
            between_squares(king_sq, sq) | bit(sq) | bit(candidate);
      }
      break;
    }
  };

  trace_direction(1, 0, false);
  trace_direction(-1, 0, false);
  trace_direction(0, 1, false);
  trace_direction(0, -1, false);
  trace_direction(1, 1, true);
  trace_direction(1, -1, true);
  trace_direction(-1, 1, true);
  trace_direction(-1, -1, true);

  return pinned;
}

Bitboard Position::attacked_squares(Color by) const {
  Bitboard attacks = 0ULL;
  const int idx = color_index(by);
  const Bitboard occ = occupied_all_;

  Bitboard pawns = pieces_[idx][static_cast<int>(PieceType::Pawn)];
  while (pawns) {
    const int sq_idx = __builtin_ctzll(pawns);
    pawns &= pawns - 1;
    const Square sq = static_cast<Square>(sq_idx);
    attacks |= pawn_attacks(by, sq);
  }

  Bitboard knights = pieces_[idx][static_cast<int>(PieceType::Knight)];
  while (knights) {
    const int sq_idx = __builtin_ctzll(knights);
    knights &= knights - 1;
    const Square sq = static_cast<Square>(sq_idx);
    attacks |= knight_attacks(sq);
  }

  Bitboard bishops = pieces_[idx][static_cast<int>(PieceType::Bishop)] |
                     pieces_[idx][static_cast<int>(PieceType::Queen)];
  while (bishops) {
    const int sq_idx = __builtin_ctzll(bishops);
    bishops &= bishops - 1;
    const Square sq = static_cast<Square>(sq_idx);
    attacks |= bishop_attacks(sq, occ);
  }

  Bitboard rooks = pieces_[idx][static_cast<int>(PieceType::Rook)] |
                   pieces_[idx][static_cast<int>(PieceType::Queen)];
  while (rooks) {
    const int sq_idx = __builtin_ctzll(rooks);
    rooks &= rooks - 1;
    const Square sq = static_cast<Square>(sq_idx);
    attacks |= rook_attacks(sq, occ);
  }

  const Square king_sq = kings_[idx];
  if (king_sq != Square::None) {
    attacks |= king_attacks(king_sq);
  }

  return attacks;
}

bool Position::in_double_check(Color side, Bitboard& checkers) const {
  checkers = 0ULL;
  const Color them = flip(side);
  const Square king_sq = kings_[color_index(side)];
  const Bitboard king_mask = bit(king_sq);

  Bitboard pawn_sources = (them == Color::White)
                              ? (south_west(king_mask) | south_east(king_mask))
                              : (north_west(king_mask) | north_east(king_mask));
  checkers |= pawn_sources & pieces_[color_index(them)][static_cast<int>(PieceType::Pawn)];

  Bitboard knight_checks = knight_attacks(king_sq) &
                           pieces_[color_index(them)][static_cast<int>(PieceType::Knight)];
  checkers |= knight_checks;

  Bitboard bishop_checks = bishop_attacks(king_sq, occupied_all_) &
                           (pieces_[color_index(them)][static_cast<int>(PieceType::Bishop)] |
                            pieces_[color_index(them)][static_cast<int>(PieceType::Queen)]);
  checkers |= bishop_checks;

  Bitboard rook_checks = rook_attacks(king_sq, occupied_all_) &
                         (pieces_[color_index(them)][static_cast<int>(PieceType::Rook)] |
                          pieces_[color_index(them)][static_cast<int>(PieceType::Queen)]);
  checkers |= rook_checks;

  const Square enemy_king = kings_[color_index(them)];
  if (enemy_king != Square::None && (king_attacks(king_sq) & bit(enemy_king))) {
    checkers |= bit(enemy_king);
  }

  return (checkers & (checkers - 1ULL)) != 0ULL;
}

std::string move_to_uci(Move move) {
  if (move.is_null()) {
    return "0000";
  }
  std::string result;
  result += square_to_string(from_square(move));
  result += square_to_string(to_square(move));
  const PieceType promo = promotion_type(move);
  if (promo != PieceType::None) {
    char suffix = 'q';
    switch (promo) {
      case PieceType::Queen:
        suffix = 'q';
        break;
      case PieceType::Rook:
        suffix = 'r';
        break;
      case PieceType::Bishop:
        suffix = 'b';
        break;
      case PieceType::Knight:
        suffix = 'n';
        break;
      default:
        suffix = 'q';
        break;
    }
    result.push_back(suffix);
  }
  return result;
}

}  // namespace bby
