#include "bbinit.h"

#include <sstream>

#include "attacks.h"

namespace bby {

InitState initialize(const InitOptions& opts) {
  init_attacks(opts.enable_bmi2);
  return InitState{opts};
}

std::string cpu_feature_summary(const InitState& state) {
  std::ostringstream oss;
  oss << "BMI2=" << (state.options.enable_bmi2 ? "on" : "off");
  return oss.str();
}

}  // namespace bby
