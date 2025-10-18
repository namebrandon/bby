#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "board.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (!data || size == 0) {
    return 0;
  }

  const std::size_t fen_size = std::min<std::size_t>(size, 4096);
  std::string fen(reinterpret_cast<const char*>(data), fen_size);
  bby::Position pos;
  try {
    pos = bby::Position::from_fen(fen, false);
  } catch (...) {
    return 0;
  }

  const std::string baseline_fen = pos.to_fen();
  const std::uint64_t baseline_key = pos.zobrist();

  const std::size_t steps = std::min<std::size_t>(size, 32);
  for (std::size_t idx = 0; idx < steps; ++idx) {
    bby::MoveList moves;
    pos.generate_moves(moves, bby::GenStage::All);
    if (moves.size() == 0) {
      break;
    }
    const bby::Move move = moves[data[idx] % moves.size()];
    bby::Undo undo;
    pos.make(move, undo);
    pos.unmake(move, undo);
    if (pos.zobrist() != baseline_key || pos.to_fen() != baseline_fen) {
      std::abort();
    }
  }

  return 0;
}
