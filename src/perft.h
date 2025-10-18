#pragma once
// perft.h -- Recursive move enumeration helpers for validation and benchmarking.

#include <cstdint>

#include "board.h"

namespace bby {

std::uint64_t perft(Position& pos, int depth);

}  // namespace bby
