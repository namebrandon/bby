#pragma once
// hash.h -- Transposition table storage and probing helpers.
// Provides 4-way buckets with age/depth-aware replacement and generation tracking.

#include <cstdint>
#include <vector>

#include "common.h"

namespace bby {

enum class BoundType : std::uint8_t { Exact = 0, Lower = 1, Upper = 2 };

struct alignas(16) TTEntry {
  std::uint64_t key{0};
  Move best_move{};
  Score score{0};
  Score static_eval{0};
  std::uint8_t depth{0};
  std::uint8_t generation{0};
  BoundType bound{BoundType::Exact};
  std::uint8_t padding{0};
};

class TT {
public:
  explicit TT(std::size_t megabytes);

  static constexpr std::size_t kBucketSize = 4;

  void set_generation(std::uint8_t gen);
  bool probe(std::uint64_t key, TTEntry& out) const;
  void store(std::uint64_t key, const TTEntry& in);
  int hashfull() const;

private:
  [[nodiscard]] std::size_t bucket_index(std::uint64_t key) const;

  std::size_t bucket_count_{1};
  std::vector<TTEntry> entries_;
  std::uint8_t generation_{0};
  std::size_t used_slots_{0};
};

}  // namespace bby
