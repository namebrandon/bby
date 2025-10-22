cmake_minimum_required(VERSION 3.20)
project(bby LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.5.3
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(BBY_SANITIZER "" CACHE STRING "Sanitizer instrumentation (asan-ubsan, tsan, or empty)")

set(_bby_sanitize_compile_flags "")
set(_bby_sanitize_link_flags "")

if(BBY_SANITIZER STREQUAL "asan-ubsan")
  list(APPEND _bby_sanitize_compile_flags
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all)
  list(APPEND _bby_sanitize_link_flags
    -fsanitize=address,undefined)
elseif(BBY_SANITIZER STREQUAL "tsan")
  list(APPEND _bby_sanitize_compile_flags
    -fsanitize=thread
    -fno-omit-frame-pointer)
  list(APPEND _bby_sanitize_link_flags
    -fsanitize=thread)
elseif(BBY_SANITIZER)
  message(WARNING "Unknown sanitizer configuration '${BBY_SANITIZER}'")
endif()

if(_bby_sanitize_compile_flags)
  if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(${_bby_sanitize_compile_flags})
    add_link_options(${_bby_sanitize_link_flags})
  else()
    message(WARNING "Sanitizers requested but compiler '${CMAKE_CXX_COMPILER_ID}' is not supported")
  endif()
endif()

set(BBY_OUTPUT_NAME "" CACHE STRING "Override engine executable name")
set(BBY_EVAL_FILE "" CACHE STRING "Optional evaluation file to embed")

add_library(bby_core
  src/common.cpp
  src/bbinit.cpp
  src/board.cpp
  src/attacks.cpp
  src/debug.cpp
  src/moveorder.cpp
  src/eval.cpp
  src/hash.cpp
  src/perft.cpp
  src/pgn.cpp
  src/epd.cpp
  src/search.cpp
  src/search_stack.cpp
  src/qsearch_probe.cpp
  src/timeman.cpp
  src/uci.cpp
  src/syzygy/tbcore.c
  src/syzygy/tbprobe.cpp
)

target_include_directories(bby_core PUBLIC src)

target_compile_options(bby_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wshadow -Wconversion -Wpedantic>
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-mbmi2 -mpopcnt>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(bby
  src/main.cpp
  src/bench_cli.cpp
)
target_link_libraries(bby PRIVATE bby_core)
if(BBY_OUTPUT_NAME)
  set_target_properties(bby PROPERTIES OUTPUT_NAME "${BBY_OUTPUT_NAME}")
endif()

add_executable(bby-perft tools/perft_main.cpp)
target_link_libraries(bby-perft PRIVATE bby_core)

add_executable(bby-epd tools/epd_main.cpp)
target_link_libraries(bby-epd PRIVATE bby_core)

add_executable(bby-san-to-uci tools/san_to_uci.cpp)
target_include_directories(bby-san-to-uci PRIVATE third_party/chess-library)

enable_testing()
add_subdirectory(test)
