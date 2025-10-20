#include "hash.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "debug.h"

namespace bby {

namespace {

std::size_t compute_bucket_count(std::size_t megabytes) {
  const std::size_t bytes = megabytes * 1024 * 1024;
  const std::size_t min_entries = TT::kBucketSize;
  const std::size_t entry_count =
      std::max<std::size_t>(min_entries, bytes / sizeof(TTEntry));
  const std::size_t buckets = std::max<std::size_t>(1, entry_count / TT::kBucketSize);
  return buckets;
}

std::uint8_t age_difference(std::uint8_t current, std::uint8_t stored) {
  return static_cast<std::uint8_t>(current - stored);
}

}  // namespace

TT::TT(std::size_t megabytes)
    : bucket_count_(compute_bucket_count(megabytes)),
      entries_(bucket_count_ * kBucketSize) {}

void TT::set_generation(std::uint8_t gen) {
  generation_ = gen;
}

std::size_t TT::bucket_index(std::uint64_t key) const {
  return bucket_count_ == 0 ? 0 : (key % bucket_count_);
}

bool TT::probe(std::uint64_t key, TTEntry& out) const {
  const std::size_t bucket = bucket_index(key);
  const std::size_t base = bucket * kBucketSize;

  for (std::size_t slot = 0; slot < kBucketSize; ++slot) {
    const TTEntry& entry = entries_[base + slot];
    if (entry.key == key) {
      out = entry;
      if (trace_enabled(TraceTopic::TT)) {
        std::ostringstream oss;
        oss << "probe key=0x" << std::hex << std::setw(16) << std::setfill('0') << key << std::dec
            << " bucket=" << bucket
            << " slot=" << slot
            << " depth=" << static_cast<int>(entry.depth)
            << " bound=" << static_cast<int>(entry.bound)
            << " gen=" << static_cast<int>(entry.generation);
        trace_emit(TraceTopic::TT, oss.str());
      }
      return true;
    }
  }

  if (trace_enabled(TraceTopic::TT)) {
    std::ostringstream oss;
    oss << "probe key=0x" << std::hex << std::setw(16) << std::setfill('0') << key << std::dec
        << " bucket=" << bucket
        << " hit=0";
    trace_emit(TraceTopic::TT, oss.str());
  }
  return false;
}

void TT::store(std::uint64_t key, const TTEntry& in) {
  const std::size_t bucket = bucket_index(key);
  const std::size_t base = bucket * kBucketSize;

  std::size_t target = kBucketSize;
  std::size_t empty_slot = kBucketSize;
  std::size_t replacement_slot = 0;
  int worst_metric = -1;

  for (std::size_t slot = 0; slot < kBucketSize; ++slot) {
    TTEntry& entry = entries_[base + slot];
    if (entry.key == key) {
      target = slot;
      break;
    }
    if (entry.key == 0ULL && empty_slot == kBucketSize) {
      empty_slot = slot;
    }
    const std::uint8_t age = age_difference(generation_, entry.generation);
    const int metric = (static_cast<int>(age) << 8) + (255 - static_cast<int>(entry.depth));
    if (metric > worst_metric) {
      worst_metric = metric;
      replacement_slot = slot;
    }
  }

  if (target == kBucketSize) {
    if (empty_slot != kBucketSize) {
      target = empty_slot;
    } else {
      target = replacement_slot;
    }
  }

  TTEntry& dest = entries_[base + target];
  const bool replacing = dest.key != 0ULL && dest.key != key;
  dest = in;
  dest.key = key;
  dest.generation = generation_;

  if (trace_enabled(TraceTopic::TT)) {
    std::ostringstream oss;
    oss << "store key=0x" << std::hex << std::setw(16) << std::setfill('0') << key << std::dec
        << " bucket=" << bucket
        << " slot=" << target
        << " depth=" << static_cast<int>(in.depth)
        << " bound=" << static_cast<int>(in.bound)
        << " replace=" << (replacing ? 1 : 0);
    trace_emit(TraceTopic::TT, oss.str());
  }
}

}  // namespace bby

