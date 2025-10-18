#include "bbinit.h"

#include <mutex>
#include <sstream>

#include "common.h"

namespace bby {

namespace {
InitOptions& opts_storage() {
  static InitOptions opts{};
  return opts;
}

std::once_flag& init_flag() {
  static std::once_flag flag;
  return flag;
}
}  // namespace

void initialize(const InitOptions& opts) {
  std::call_once(init_flag(), [&]() { opts_storage() = opts; });
}

std::string cpu_feature_summary() {
  std::ostringstream oss;
  const auto& opts = opts_storage();
  oss << "BMI2=" << (opts.enable_bmi2 ? "on" : "off");
  return oss.str();
}

const InitOptions& init_options() {
  return opts_storage();
}

}  // namespace bby
