#pragma once
// board.h -- Core chess position representation with make/unmake operations.
// Maintains bitboards, FEN parsing/serialization, and legality checks.

#include <array>
#include <string>
#include <string_view>

#include "common.h"

namespace bby {

enum CastlingRights : std::uint8_t {
  CastleNone = 0,
  CastleWK = 1 << 0,
  CastleWQ = 1 << 1,
  CastleBK = 1 << 2,
  CastleBQ = 1 << 3
};

class Position {
public:
  Position();

  static Position from_fen(std::string_view fen, bool strict = true);
  std::string to_fen() const;

  [[nodiscard]] Color side_to_move() const { return side_; }
  [[nodiscard]] Bitboard occupancy() const { return occupied_all_; }
  [[nodiscard]] Bitboard occupancy(Color c) const { return occupied_[color_index(c)]; }
  [[nodiscard]] bool in_check(Color color) const;
  [[nodiscard]] std::uint64_t zobrist() const { return zobrist_; }
  [[nodiscard]] Square king_square(Color color) const { return kings_[color_index(color)]; }
  [[nodiscard]] std::uint8_t castling_rights() const { return castling_; }
  [[nodiscard]] Square en_passant_square() const { return ep_square_; }

  void generate_moves(MoveList& out, GenStage stage) const;
  bool is_legal(Move m) const;

  void make(Move m, Undo& undo);
  void unmake(Move m, const Undo& undo);

  [[nodiscard]] Piece piece_on(Square sq) const { return squares_[static_cast<int>(sq)]; }
  [[nodiscard]] std::uint8_t halfmove_clock() const { return halfmove_clock_; }
  [[nodiscard]] std::uint16_t fullmove_number() const { return fullmove_number_; }

private:
  void clear();
  void put_piece(Piece pc, Square sq);
  void remove_piece(Piece pc, Square sq);
  void move_piece(Piece pc, Square from, Square to);
  void set_side_to_move(Color c);
  void set_castling(std::uint8_t rights);
  void set_en_passant(Square sq);
  void recompute_occupancy();
  void recompute_zobrist();
  bool is_square_attacked(Square sq, Color by) const;
  void generate_pseudo_legal(MoveList& out) const;

  std::array<Piece, 64> squares_{};
  std::array<std::array<Bitboard, 6>, 2> pieces_{{}};
  std::array<Bitboard, 2> occupied_{};
  Bitboard occupied_all_{0};
  std::array<Square, 2> kings_{Square::None, Square::None};

  Color side_{Color::White};
  std::uint8_t castling_{CastleNone};
  Square ep_square_{Square::None};
  std::uint8_t halfmove_clock_{0};
  std::uint16_t fullmove_number_{1};
  std::uint64_t zobrist_{0};
};

}  // namespace bby
