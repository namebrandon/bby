#include "hash.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "debug.h"

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
  const bool hit = entry.key == key;
  if (trace_enabled(TraceTopic::TT)) {
    std::ostringstream oss;
    oss << "probe key=0x" << std::hex << std::setw(16) << std::setfill('0') << key << std::dec
        << " bucket=" << idx
        << " hit=" << (hit ? 1 : 0);
    if (hit) {
      oss << " depth=" << static_cast<int>(entry.depth)
          << " flags=" << static_cast<int>(entry.flags)
          << " gen=" << static_cast<int>(entry.generation);
    }
    trace_emit(TraceTopic::TT, oss.str());
  }
  if (hit) {
    out = entry;
    return true;
  }
  return false;
}

void TT::store(std::uint64_t key, const TTEntry& in) {
  const auto idx = key % buckets_.size();
  auto& entry = buckets_[idx];
  const bool replacing = entry.key != 0ULL && entry.key != key;
  if (trace_enabled(TraceTopic::TT)) {
    std::ostringstream oss;
    oss << "store key=0x" << std::hex << std::setw(16) << std::setfill('0') << key << std::dec
        << " bucket=" << idx
        << " depth=" << static_cast<int>(in.depth)
        << " flags=" << static_cast<int>(in.flags)
        << " replace=" << (replacing ? 1 : 0);
    trace_emit(TraceTopic::TT, oss.str());
  }
  entry = in;
  entry.key = key;
  entry.generation = generation_;
}

}  // namespace bby
