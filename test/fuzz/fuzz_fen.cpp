#include <cstddef>
#include <cstdint>
#include <string>

#include "board.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (!data || size == 0 || size > 4096) {
    return 0;
  }

  std::string input(reinterpret_cast<const char*>(data), size);
  try {
    bby::Position::from_fen(input, false);
  } catch (...) {
    // Ignore parse failures; fuzzing is interested in crashes only.
  }
  return 0;
}
