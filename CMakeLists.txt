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

set(OUTPUT_ROOT "${CMAKE_BINARY_DIR}/out/${CMAKE_BUILD_TYPE}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_ROOT}")

add_library(bby_core
  src/common.cpp
  src/bbinit.cpp
  src/board.cpp
  src/debug.cpp
  src/moveorder.cpp
  src/eval.cpp
  src/hash.cpp
  src/perft.cpp
  src/search.cpp
  src/timeman.cpp
  src/uci.cpp
  src/syzygy/tbcore.c
  src/syzygy/tbprobe.cpp
)

target_include_directories(bby_core PUBLIC src)

target_compile_options(bby_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wshadow -Wconversion -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(bby src/main.cpp)
target_link_libraries(bby PRIVATE bby_core)

add_executable(bby-perft tools/perft_main.cpp)
target_link_libraries(bby-perft PRIVATE bby_core)

add_executable(bby-epd tools/epd_main.cpp)
target_link_libraries(bby-epd PRIVATE bby_core)

enable_testing()
add_subdirectory(test)
