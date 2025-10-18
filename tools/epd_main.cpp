#include "board.h"

#include <iostream>
#include <string_view>

int main() {
  constexpr std::string_view start_fen =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
  auto pos = bby::Position::from_fen(start_fen, false);
  std::cout << "EPD runner stub. Zobrist: " << pos.zobrist() << "\n";
  return 0;
}
