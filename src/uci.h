#pragma once
// uci.h -- Universal Chess Interface event loop and option wiring.
// Exposes a single entry point used by main() to run the engine in CLI mode.

#include <string_view>

namespace bby {

using UciWriter = void (*)(std::string_view line);

int uci_main();
void set_uci_writer(UciWriter writer);

}  // namespace bby
