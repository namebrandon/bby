#include <iostream>
#include <string>
#include "../third_party/chess-library/chess.hpp"

int main() {
  std::string fen;
  if (!std::getline(std::cin, fen)) {
    std::cerr << "missing fen" << std::endl;
    return 1;
  }
  std::string san;
  if (!std::getline(std::cin, san)) {
    std::cerr << "missing san" << std::endl;
    return 1;
  }
  try {
    chess::Board board(fen);
    const chess::Move move = chess::uci::parseSan(board, san);
    if (move == chess::Move::NO_MOVE) {
      std::cerr << "error: no move" << std::endl;
      return 1;
    }
    std::cout << chess::uci::moveToUci(move) << std::endl;
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << std::endl;
    return 1;
  }
  return 0;
}
