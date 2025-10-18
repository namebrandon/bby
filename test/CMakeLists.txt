include(Catch)

add_executable(bby-unit
  unit/board_tests.cpp
  unit/eval_tests.cpp
  unit/hash_tests.cpp
  unit/moveorder_tests.cpp
  unit/debug_tests.cpp
  unit/uci_tests.cpp
  unit/perft_tests.cpp
  unit/timeman_tests.cpp
)

target_link_libraries(bby-unit PRIVATE bby_core Catch2::Catch2WithMain)

target_compile_options(bby-unit PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

catch_discover_tests(bby-unit)
