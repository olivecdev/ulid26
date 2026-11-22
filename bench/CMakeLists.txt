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

# Reference implementation for head-to-head benchmarks (bench-only
# dependency, never linked into the library itself). Excluded on MSVC:
# without __int128 suyash falls back to its struct-based header, whose
# integer-oriented API this benchmark is not written against.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    FetchContent_Declare(
        suyash_ulid
        GIT_REPOSITORY https://github.com/suyash/ulid
        GIT_TAG        3a46362db2c0f5fece10e59a52bdefbf7eec0877
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(suyash_ulid)
endif()

# No ulid26_warnings here by convention (see cmake/ulid26_warnings.cmake): the
# strict set applies to the test and example binaries, benchmarks stay
# warning-clean by construction instead.
add_executable(bench_is_valid bench_is_valid.cpp)
target_link_libraries(bench_is_valid PRIVATE ulid26 benchmark::benchmark)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_include_directories(bench_is_valid PRIVATE ${suyash_ulid_SOURCE_DIR}/src)
    target_compile_definitions(bench_is_valid PRIVATE ULID26_HAVE_SUYASH)
endif()

add_executable(bench_generation bench_generation.cpp)
target_link_libraries(bench_generation PRIVATE ulid26 benchmark::benchmark)

add_executable(bench_to_chars bench_to_chars.cpp)
target_link_libraries(bench_to_chars PRIVATE ulid26 benchmark::benchmark)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_include_directories(bench_to_chars PRIVATE ${suyash_ulid_SOURCE_DIR}/src)
    target_compile_definitions(bench_to_chars PRIVATE ULID26_HAVE_SUYASH)
endif()

add_executable(bench_bytes bench_bytes.cpp)
target_link_libraries(bench_bytes PRIVATE ulid26 benchmark::benchmark)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_include_directories(bench_bytes PRIVATE ${suyash_ulid_SOURCE_DIR}/src)
    target_compile_definitions(bench_bytes PRIVATE ULID26_HAVE_SUYASH)
endif()

add_executable(bench_streams bench_streams.cpp)
target_link_libraries(bench_streams PRIVATE ulid26 benchmark::benchmark)
