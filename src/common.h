#pragma once
// common.h -- Shared primitive types, move encoding, and debug helpers.
// Defines lightweight utilities used across the engine. Keep implementation
// details in the corresponding translation unit when possible.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bby {

constexpr std::size_t kMaxMoves = 256;
constexpr int kMaxPly = 128;

using Bitboard = std::uint64_t;
using Score = int;

enum class Color : std::uint8_t { White = 0, Black = 1 };

constexpr Color flip(Color c) {
  return c == Color::White ? Color::Black : Color::White;
}

constexpr int color_index(Color c) {
  return c == Color::White ? 0 : 1;
}

enum class File : std::uint8_t {
  A = 0,
  B,
  C,
  D,
  E,
  F,
  G,
  H,
  Invalid = 8
};

enum class Rank : std::uint8_t {
  R1 = 0,
  R2,
  R3,
  R4,
  R5,
  R6,
  R7,
  R8,
  Invalid = 8
};

enum class Square : std::uint8_t {
  A1 = 0, B1, C1, D1, E1, F1, G1, H1,
  A2,     B2, C2, D2, E2, F2, G2, H2,
  A3,     B3, C3, D3, E3, F3, G3, H3,
  A4,     B4, C4, D4, E4, F4, G4, H4,
  A5,     B5, C5, D5, E5, F5, G5, H5,
  A6,     B6, C6, D6, E6, F6, G6, H6,
  A7,     B7, C7, D7, E7, F7, G7, H7,
  A8,     B8, C8, D8, E8, F8, G8, H8,
  None = 64
};

constexpr Square make_square(File file, Rank rank) {
  return static_cast<Square>(static_cast<int>(rank) * 8 + static_cast<int>(file));
}

constexpr File file_of(Square sq) {
  return sq == Square::None ? File::Invalid
                            : static_cast<File>(static_cast<int>(sq) & 7);
}

constexpr Rank rank_of(Square sq) {
  return sq == Square::None ? Rank::Invalid
                            : static_cast<Rank>(static_cast<int>(sq) >> 3);
}

constexpr Bitboard bit(Square sq) {
  return sq == Square::None ? 0ULL : (1ULL << static_cast<int>(sq));
}

std::string square_to_string(Square sq);
Square square_from_string(std::string_view token);

enum class PieceType : std::uint8_t { Pawn = 0, Knight, Bishop, Rook, Queen, King, None = 6 };

enum class Piece : std::uint8_t {
  None = 0,
  WPawn,
  WKnight,
  WBishop,
  WRook,
  WQueen,
  WKing,
  BPawn,
  BKnight,
  BBishop,
  BRook,
  BQueen,
  BKing
};

enum class GenStage : std::uint8_t { Captures = 0, Quiets = 1, All = 2 };

constexpr Piece make_piece(Color c, PieceType pt) {
  if (pt == PieceType::None) {
    return Piece::None;
  }
  constexpr std::uint8_t base[2] = {static_cast<std::uint8_t>(Piece::WPawn),
                                    static_cast<std::uint8_t>(Piece::BPawn)};
  return static_cast<Piece>(base[color_index(c)] + static_cast<std::uint8_t>(pt));
}

constexpr Color color_of(Piece pc) {
  if (pc == Piece::None) {
    return Color::White;
  }
  return static_cast<std::uint8_t>(pc) < static_cast<std::uint8_t>(Piece::BPawn)
             ? Color::White
             : Color::Black;
}

constexpr PieceType type_of(Piece pc) {
  if (pc == Piece::None) {
    return PieceType::None;
  }
  const auto raw = static_cast<std::uint8_t>(pc);
  return static_cast<PieceType>((raw - 1) % 6);
}

char piece_to_char(Piece pc);
Piece piece_from_char(char c);

enum class MoveFlag : std::uint8_t {
  Quiet = 0,
  DoublePush = 1,
  KingCastle = 2,
  QueenCastle = 3,
  Capture = 4,
  EnPassant = 5,
  Promotion = 6,
  PromotionCapture = 7
};

struct Move {
  std::uint32_t value{0};

  constexpr Move() = default;
  constexpr explicit Move(std::uint32_t v) : value(v) {}
  constexpr bool operator==(const Move&) const = default;

  constexpr bool is_null() const { return value == 0; }
};

constexpr Move make_move(Square from, Square to,
                         MoveFlag flag = MoveFlag::Quiet,
                         PieceType promotion = PieceType::None) {
  return Move{static_cast<std::uint32_t>(static_cast<int>(from)) |
              (static_cast<std::uint32_t>(static_cast<int>(to)) << 6) |
              (static_cast<std::uint32_t>(static_cast<int>(promotion)) << 12) |
              (static_cast<std::uint32_t>(static_cast<int>(flag)) << 16)};
}

constexpr Square from_square(Move m) {
  return static_cast<Square>(m.value & 0x3F);
}

constexpr Square to_square(Move m) {
  return static_cast<Square>((m.value >> 6) & 0x3F);
}

constexpr PieceType promotion_type(Move m) {
  return static_cast<PieceType>((m.value >> 12) & 0xF);
}

constexpr MoveFlag move_flag(Move m) {
  return static_cast<MoveFlag>((m.value >> 16) & 0xF);
}

class MoveList {
public:
  MoveList();

  void clear();
  void push_back(Move m);
  [[nodiscard]] std::size_t size() const;
  Move& operator[](std::size_t idx);
  const Move& operator[](std::size_t idx) const;
  Move* begin();
  Move* end();
  const Move* begin() const;
  const Move* end() const;

private:
  std::array<Move, kMaxMoves> moves_{};
  std::size_t count_{0};
};

struct Undo {
  std::uint64_t key{0};
  Move move{};
  Piece captured{Piece::None};
  std::uint8_t castling{0};
  std::uint8_t halfmove_clock{0};
  Square en_passant{Square::None};
};

namespace detail {
[[noreturn]] void bby_trap(const char* expr, const char* file, int line);
void check_finite(double value, const char* expr, const char* file, int line);
}  // namespace detail

}  // namespace bby

#ifdef NDEBUG
#define BBY_ASSERT(expr) do { (void)sizeof(expr); } while (false)
#define BBY_INVARIANT(expr) do { (void)sizeof(expr); } while (false)
#else
#define BBY_ASSERT(expr)                                                          \
  do {                                                                            \
    if (!(expr)) {                                                                \
      ::bby::detail::bby_trap(#expr, __FILE__, __LINE__);                         \
    }                                                                             \
  } while (false)
#define BBY_INVARIANT(expr) BBY_ASSERT(expr)
#endif

#define BBY_TRAP_ON_NAN(value)                                                    \
  ::bby::detail::check_finite(static_cast<double>(value), #value, __FILE__, __LINE__)

namespace bby {

inline MoveList::MoveList() = default;

inline void MoveList::clear() {
  count_ = 0U;
}

inline void MoveList::push_back(Move m) {
  BBY_ASSERT(count_ < moves_.size());
  moves_[count_++] = m;
}

inline std::size_t MoveList::size() const {
  return count_;
}

inline Move& MoveList::operator[](std::size_t idx) {
  BBY_ASSERT(idx < count_);
  return moves_[idx];
}

inline const Move& MoveList::operator[](std::size_t idx) const {
  BBY_ASSERT(idx < count_);
  return moves_[idx];
}

inline Move* MoveList::begin() {
  return moves_.data();
}

inline Move* MoveList::end() {
  return moves_.data() + static_cast<std::ptrdiff_t>(count_);
}

inline const Move* MoveList::begin() const {
  return moves_.data();
}

inline const Move* MoveList::end() const {
  return moves_.data() + static_cast<std::ptrdiff_t>(count_);
}

}  // namespace bby
