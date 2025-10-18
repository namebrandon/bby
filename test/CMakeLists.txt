include(Catch)

add_executable(bby-unit
  unit/board_tests.cpp
  unit/eval_tests.cpp
  unit/hash_tests.cpp
  unit/moveorder_tests.cpp
  unit/debug_tests.cpp
  unit/uci_tests.cpp
  unit/epd_tests.cpp
  unit/perft_tests.cpp
  unit/timeman_tests.cpp
  unit/property_tests.cpp
  unit/pgn_tests.cpp
)

target_link_libraries(bby-unit PRIVATE bby_core Catch2::Catch2WithMain)

target_compile_options(bby-unit PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
)

catch_discover_tests(bby-unit)

option(BBY_BUILD_FUZZERS "Build libFuzzer harness targets" OFF)

if (BBY_BUILD_FUZZERS)
  set(BBY_FUZZ_TARGETS
    fuzz_fen
    fuzz_epd
    fuzz_uci
    fuzz_make_unmake
  )

  foreach(target_name IN LISTS BBY_FUZZ_TARGETS)
    add_executable(${target_name} fuzz/${target_name}.cpp)
    target_link_libraries(${target_name} PRIVATE bby_core)
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      target_compile_options(${target_name} PRIVATE -fsanitize=fuzzer-no-link)
      target_link_options(${target_name} PRIVATE -fsanitize=fuzzer)
    endif()
  endforeach()
endif()
