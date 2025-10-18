#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace bby {
void uci_fuzz_feed(std::string_view payload);
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (!data || size == 0 || size > 4096) {
    return 0;
  }

  std::string buffer(reinterpret_cast<const char*>(data), size);
  if (buffer.empty() || buffer.back() != '\n') {
    buffer.push_back('\n');
  }
  bby::uci_fuzz_feed(buffer);
  return 0;
}
