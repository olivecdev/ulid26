# SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
# SPDX-License-Identifier: MIT

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/v1.9.5.zip
)
FetchContent_MakeAvailable(googlebenchmark)

# No ulid26_warnings here by convention (see test/CMakeLists.txt): the
# strict set applies to the test binaries, benchmarks stay warning-clean
# by construction instead.
add_executable(bench_is_valid bench_is_valid.cpp)
target_link_libraries(bench_is_valid PRIVATE ulid26 benchmark::benchmark)
