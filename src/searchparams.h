#pragma once
// searchparams.h -- POD containers for search configuration and UCI options.
// These structures are shared between the UCI layer, search, and time manager.

#include <cstdint>

namespace bby {

inline constexpr int kLmrMinDepthDefault = 4;
inline constexpr int kLmrMinMoveDefault = 3;

struct Limits {
  std::int64_t movetime_ms{-1};
  std::int64_t nodes{-1};
  std::int16_t depth{-1};
  std::int64_t wtime_ms{-1};
  std::int64_t btime_ms{-1};
  std::int64_t winc_ms{0};
  std::int64_t binc_ms{0};
  int multipv{1};
  int lmr_min_depth{kLmrMinDepthDefault};
  int lmr_min_move{kLmrMinMoveDefault};
  bool enable_static_futility{true};
  int static_futility_margin{128};
  int static_futility_depth{1};
  bool enable_razoring{true};
  int razor_margin{256};
  int razor_depth{1};
  bool infinite{false};
};

struct SearchKnobs {
  bool enable_null_move{true};
  bool enable_lmr{true};
};

}  // namespace bby
