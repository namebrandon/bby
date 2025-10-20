#pragma once
// bbinit.h -- One-time engine initialization: RNG seeds, CPU dispatch, table bootstrap.
// In production this module wires together CPU feature detection and random seeds.

#include <cstdint>
#include <string>

namespace bby {

struct InitOptions {
  bool enable_bmi2{true};
  std::uint64_t rng_seed{0x1234'5678'9abc'def0ULL};
};

struct InitState {
  InitOptions options{};
};

InitState initialize(const InitOptions& opts = {});
std::string cpu_feature_summary(const InitState& state);

}  // namespace bby
