#pragma once
// hash.h -- Transposition table storage and probing helpers.
// Implements a simple fixed-size bucketed table with generation tracking.

#include <cstdint>
#include <vector>

#include "common.h"

namespace bby {

struct TTEntry {
  std::uint64_t key{0};
  Score score{0};
  std::uint8_t depth{0};
  std::uint8_t flags{0};
  Move best_move{};
  std::uint8_t generation{0};
};

class TT {
public:
  explicit TT(std::size_t megabytes);

  void set_generation(std::uint8_t gen);
  bool probe(std::uint64_t key, TTEntry& out) const;
  void store(std::uint64_t key, const TTEntry& in);

private:
  std::vector<TTEntry> buckets_;
  std::uint8_t generation_{0};
};

}  // namespace bby
