#include <iostream>
#include <string_view>

#include "bench_cli.h"
#include "uci.h"

int main(int argc, const char* argv[]) {
  if (argc > 1 && std::string_view(argv[1]) == "bench") {
    return bby::bench_cli_main(argc - 2, argv + 2);
  }
  std::cout << bby::engine_name() << " by " << bby::engine_author() << std::endl;
  return bby::uci_main();
}
