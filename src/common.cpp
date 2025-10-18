#include "common.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace bby {

namespace {
constexpr std::array<char, 13> kPieceChars = {
    '.', 'P', 'N', 'B', 'R', 'Q', 'K', 'p', 'n', 'b', 'r', 'q', 'k'};
}

std::string square_to_string(Square sq) {
  if (sq == Square::None) {
    return "--";
  }
  const char file = static_cast<char>('a' + static_cast<int>(file_of(sq)));
  const char rank = static_cast<char>('1' + static_cast<int>(rank_of(sq)));
  return std::string{file, rank};
}

Square square_from_string(std::string_view token) {
  if (token.size() < 2) {
    return Square::None;
  }
  const char file_char = static_cast<char>(std::tolower(token[0]));
  const char rank_char = token[1];
  if (file_char < 'a' || file_char > 'h' || rank_char < '1' || rank_char > '8') {
    return Square::None;
  }
  const auto file = static_cast<File>(file_char - 'a');
  const auto rank = static_cast<Rank>(rank_char - '1');
  return make_square(file, rank);
}

char piece_to_char(Piece pc) {
  return kPieceChars[static_cast<std::uint8_t>(pc)];
}

Piece piece_from_char(char c) {
  switch (c) {
    case 'P':
      return Piece::WPawn;
    case 'N':
      return Piece::WKnight;
    case 'B':
      return Piece::WBishop;
    case 'R':
      return Piece::WRook;
    case 'Q':
      return Piece::WQueen;
    case 'K':
      return Piece::WKing;
    case 'p':
      return Piece::BPawn;
    case 'n':
      return Piece::BKnight;
    case 'b':
      return Piece::BBishop;
    case 'r':
      return Piece::BRook;
    case 'q':
      return Piece::BQueen;
    case 'k':
      return Piece::BKing;
    default:
      return Piece::None;
  }
}

namespace detail {

[[noreturn]] void bby_trap(const char* expr, const char* file, int line) {
  std::cerr << "BBY assertion failed: " << expr << " (" << file << ':' << line << ")\n";
  std::abort();
}

void check_finite(double value, const char* expr, const char* file, int line) {
#ifndef NDEBUG
  if (!std::isfinite(value)) {
    bby_trap(expr, file, line);
  }
#else
  (void)value;
  (void)expr;
  (void)file;
  (void)line;
#endif
}

}  // namespace detail

}  // namespace bby
