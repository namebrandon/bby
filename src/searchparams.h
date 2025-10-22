#pragma once
// searchparams.h -- POD containers for search configuration and UCI options.
// These structures are shared between the UCI layer, search, and time manager.

#include <cstdint>

namespace bby {

inline constexpr int kLmrMinDepthDefault = 2;
inline constexpr int kLmrMinMoveDefault = 2;

struct Limits {
  std::int64_t movetime_ms{-1};
  std::int64_t nodes{-1};
  std::int16_t depth{-1};
  std::int64_t wtime_ms{-1};
  std::int64_t btime_ms{-1};
  std::int64_t winc_ms{0};
  std::int64_t binc_ms{0};
  int movestogo{-1};
  int mate{-1};
  int multipv{1};
  int lmr_min_depth{kLmrMinDepthDefault};
  int lmr_min_move{kLmrMinMoveDefault};
  bool enable_static_futility{true};
  int static_futility_margin{128};
  int static_futility_depth{1};
  bool enable_razoring{true};
  int razor_margin{256};
  int razor_depth{1};
  bool enable_multi_cut{true};
  int multi_cut_min_depth{4};
  int multi_cut_reduction{2};
  int multi_cut_candidates{8};
  int multi_cut_threshold{3};
  int history_weight_scale{100};
  int counter_history_weight_scale{50};
  int continuation_history_weight_scale{50};
  bool enable_null_move{true};
  int null_min_depth{2};
  int null_base_reduction{2};
  int null_depth_scale{4};
  int null_eval_margin{120};
  int null_verification_depth{1};
  bool enable_recapture_extension{true};
  bool enable_check_extension{true};
  int recapture_extension_depth{4};
  int check_extension_depth{3};
  bool infinite{false};
};

struct SearchKnobs {
  bool enable_null_move{true};
  bool enable_lmr{true};
};

}  // namespace bby
