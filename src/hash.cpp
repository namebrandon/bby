#include "hash.h"

#include <algorithm>

namespace bby {

namespace {
std::size_t bucket_count(std::size_t megabytes) {
  constexpr std::size_t entry_size = sizeof(TTEntry);
  const std::size_t bytes = megabytes * 1024 * 1024;
  return std::max<std::size_t>(1, bytes / entry_size);
}
}

TT::TT(std::size_t megabytes) : buckets_(bucket_count(megabytes)) {}

void TT::set_generation(std::uint8_t gen) {
  generation_ = gen;
}

bool TT::probe(std::uint64_t key, TTEntry& out) const {
  const auto idx = key % buckets_.size();
  const auto& entry = buckets_[idx];
  if (entry.key == key) {
    out = entry;
    return true;
  }
  return false;
}

void TT::store(std::uint64_t key, const TTEntry& in) {
  auto& entry = buckets_[key % buckets_.size()];
  entry = in;
  entry.key = key;
  entry.generation = generation_;
}

}  // namespace bby
