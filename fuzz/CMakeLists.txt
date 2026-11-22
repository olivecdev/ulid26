# SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
# SPDX-License-Identifier: MIT
#
# libFuzzer harness over the text-codec boundary (Clang only: libFuzzer is a
# Clang runtime; the fuzz preset already pins CMAKE_CXX_COMPILER=clang++).
# No ulid26_warnings here by convention (see cmake/ulid26_warnings.cmake):
# the strict set applies to the test and example binaries, fuzz stays
# warning-clean by construction.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "fuzz harness requires Clang (libFuzzer)")
endif()

add_executable(fuzz_text fuzz_text.cpp)
target_link_libraries(fuzz_text PRIVATE ulid26)
target_compile_options(fuzz_text PRIVATE -fsanitize=fuzzer,address -fno-omit-frame-pointer)
target_link_options(fuzz_text PRIVATE -fsanitize=fuzzer,address)
