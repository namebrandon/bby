#include <cstddef>
#include <cstdint>
#include <string>

#include "epd/epd.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (!data || size == 0 || size > 4096) {
    return 0;
  }

  std::string line(reinterpret_cast<const char*>(data), size);
  bby::EpdRecord record;
  std::string error;
  bby::parse_epd_line(line, record, error);
  return 0;
}
