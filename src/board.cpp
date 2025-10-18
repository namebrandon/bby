#include "board.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

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

constexpr std::array<Bitboard, 64> kKnightAttacks = [] {
  std::array<Bitboard, 64> arr{};
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard attacks = 0ULL;
    const int file = sq & 7;
    const int rank = sq >> 3;
    constexpr std::array<std::pair<int, int>, 8> kOffsets = {{
        {1, 2},  {2, 1},  {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2},
    }};
    for (const auto& [df, dr] : kOffsets) {
      const int nf = file + df;
      const int nr = rank + dr;
      if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
        const auto nsq = static_cast<Square>(nr * 8 + nf);
        attacks |= bit(nsq);
      }
    }
    arr[sq] = attacks;
  }
  return arr;
}();

constexpr std::array<Bitboard, 64> kKingAttacks = [] {
  std::array<Bitboard, 64> arr{};
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard attacks = 0ULL;
    const int file = sq & 7;
    const int rank = sq >> 3;
    for (int df = -1; df <= 1; ++df) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (df == 0 && dr == 0) {
          continue;
        }
        const int nf = file + df;
        const int nr = rank + dr;
        if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
          attacks |= bit(static_cast<Square>(nr * 8 + nf));
        }
      }
    }
    arr[sq] = attacks;
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

Bitboard ray_attacks(Square sq, int df, int dr, Bitboard occ) {
  Bitboard attacks = 0ULL;
  int file = static_cast<int>(file_of(sq));
  int rank = static_cast<int>(rank_of(sq));
  int nf = file + df;
  int nr = rank + dr;
  while (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
    const Square target = static_cast<Square>(nr * 8 + nf);
    const Bitboard target_bb = bit(target);
    attacks |= target_bb;
    if (occ & target_bb) {
      break;
    }
    nf += df;
    nr += dr;
  }
  return attacks;
}

Bitboard bishop_attacks(Square sq, Bitboard occ) {
  return ray_attacks(sq, 1, 1, occ) | ray_attacks(sq, 1, -1, occ) |
         ray_attacks(sq, -1, 1, occ) | ray_attacks(sq, -1, -1, occ);
}

Bitboard rook_attacks(Square sq, Bitboard occ) {
  return ray_attacks(sq, 1, 0, occ) | ray_attacks(sq, -1, 0, occ) |
         ray_attacks(sq, 0, 1, occ) | ray_attacks(sq, 0, -1, occ);
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

}  // namespace

Position::Position() {
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
  MoveList pseudo;
  generate_pseudo_legal(pseudo);
  out.clear();
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
    Undo u;
    const_cast<Position*>(this)->make(move, u);
    const bool legal = !const_cast<Position*>(this)->in_check(flip(side_));
    const_cast<Position*>(this)->unmake(move, u);
    if (legal) {
      out.push_back(move);
    }
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
  const Square from = from_square(m);
  const Square to = to_square(m);
  Piece moving = squares_[static_cast<int>(from)];
  BBY_ASSERT(moving != Piece::None);

  undo.key = zobrist_;
  undo.move = m;
  undo.castling = castling_;
  undo.en_passant = ep_square_;
  undo.halfmove_clock = halfmove_clock_;
  undo.captured = Piece::None;

  const MoveFlag flag = move_flag(m);

  set_en_passant(Square::None);

  if (flag == MoveFlag::EnPassant) {
    const int dir = (side_ == Color::White) ? -8 : 8;
    const Square capture_sq = static_cast<Square>(static_cast<int>(to) + dir);
    undo.captured = squares_[static_cast<int>(capture_sq)];
    remove_piece(undo.captured, capture_sq);
  } else if (squares_[static_cast<int>(to)] != Piece::None) {
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

  if (type_of(moving) == PieceType::King) {
    kings_[color_index(side_)] = to;
  }

  auto clear_castling_rook = [&](Square sq) {
    if (sq == Square::A1) castling_ &= ~CastleWQ;
    if (sq == Square::H1) castling_ &= ~CastleWK;
    if (sq == Square::A8) castling_ &= ~CastleBQ;
    if (sq == Square::H8) castling_ &= ~CastleBK;
  };

  clear_castling_rook(from);
  clear_castling_rook(to);
  if (type_of(moving) == PieceType::King) {
    if (side_ == Color::White) {
      castling_ &= static_cast<std::uint8_t>(~(CastleWK | CastleWQ));
    } else {
      castling_ &= static_cast<std::uint8_t>(~(CastleBK | CastleBQ));
    }
  }

  if (undo.captured != Piece::None && type_of(undo.captured) == PieceType::Rook) {
    clear_castling_rook(to);
  }

  halfmove_clock_ = (type_of(moving) == PieceType::Pawn || undo.captured != Piece::None)
                        ? 0
                        : static_cast<std::uint8_t>(halfmove_clock_ + 1);

  if (side_ == Color::Black) {
    ++fullmove_number_;
  }

  side_ = flip(side_);
  zobrist_ ^= zobrist_tables().side;

  const auto& tables = zobrist_tables();
  // Castling key update.
  if (undo.castling != castling_) {
    zobrist_ ^= tables.castling[undo.castling];
    zobrist_ ^= tables.castling[castling_];
  }
  // En passant key update handled in set_en_passant.
}

void Position::unmake(Move m, const Undo& undo) {
  const Square from = from_square(m);
  const Square to = to_square(m);
  Piece moving = squares_[static_cast<int>(to)];
  const MoveFlag flag = move_flag(m);

  side_ = flip(side_);
  zobrist_ ^= zobrist_tables().side;

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
  zobrist_ = 0ULL;
  const auto& tables = zobrist_tables();
  for (int sq = 0; sq < 64; ++sq) {
    const Piece pc = squares_[sq];
    if (pc == Piece::None) {
      continue;
    }
    const Color c = color_of(pc);
    const auto type = type_of(pc);
    zobrist_ ^= tables.piece[color_index(c)][static_cast<int>(type)][sq];
  }
  zobrist_ ^= tables.castling[castling_];
  if (ep_square_ != Square::None) {
    zobrist_ ^= tables.ep[static_cast<int>(file_of(ep_square_))];
  }
}

bool Position::is_square_attacked(Square sq, Color by) const {
  const Bitboard target = bit(sq);
  const int attacker = color_index(by);

  const Bitboard pawns = pieces_[attacker][static_cast<int>(PieceType::Pawn)];
  if (by == Color::White) {
    Bitboard attacks = north_west(pawns) | north_east(pawns);
    if (attacks & target) {
      return true;
    }
  } else {
    Bitboard attacks = south_west(pawns) | south_east(pawns);
    if (attacks & target) {
      return true;
    }
  }

  if (kKnightAttacks[static_cast<int>(sq)] &
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

  if (kKingAttacks[static_cast<int>(sq)] &
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
    Bitboard single = north(pawns) & empty;
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
    Bitboard single_from_start = north(start_rank) & empty;
    Bitboard double_push = north(single_from_start) & empty;
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
    Bitboard single = south(pawns) & empty;
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
    Bitboard single_from_start = south(start_rank) & empty;
    Bitboard double_push = south(single_from_start) & empty;
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
    Bitboard moves = kKnightAttacks[from_idx] & ~ours;
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
    Bitboard moves = kKingAttacks[from_idx] & ~ours;
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
}  // namespace bby
