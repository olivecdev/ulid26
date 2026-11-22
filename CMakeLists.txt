#                                                        
#             ░██ ░██       ░██  ░██████   ░██████  
#             ░██           ░██ ░██   ░██ ░██   ░██ 
#  ░██    ░██ ░██ ░██ ░████████       ░██ ░██       
#  ░██    ░██ ░██ ░██░██    ░██   ░█████  ░███████  
#  ░██    ░██ ░██ ░██░██    ░██  ░██      ░██   ░██ 
#  ░██   ░███ ░██ ░██░██   ░███ ░██       ░██   ░██ 
#   ░█████░██ ░██ ░██ ░█████░██ ░████████  ░██████  
#                                                   
#                                                   
#   SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
#   SPDX-License-Identifier: MIT
#
cmake_minimum_required(VERSION 3.26)
project(
    ulid26
    VERSION 0.9.0
    DESCRIPTION "C++23 ULID implementation"
    HOMEPAGE_URL "https://olivec.dev"
    LANGUAGES CXX
)

option(ULID26_BUILD_TESTS "Build ulid26 tests" ${PROJECT_IS_TOP_LEVEL})
option(ULID26_BUILD_EXAMPLES "Build ulid26 examples" ${PROJECT_IS_TOP_LEVEL})
option(ULID26_BUILD_BENCHMARKS "Build ulid26 benchmarks" OFF)
option(ULID26_BUILD_FUZZ "Build ulid26 fuzz harness (Clang + libFuzzer only)" OFF)
option(ULID26_FORCE_MUTEX "Compile only the mutex tier (for ThreadSanitizer)" OFF)

# ---- Library ----
# Single target, all headers: this is a small header-only library, so one target installs everything
# and every consumer links the same thing. Compile-time granularity lives in the headers, not the
# targets — include only the layer a TU needs (see the layering note in ulid26/ulid26.hpp).
add_library(ulid26 INTERFACE)
add_library(ulid26::ulid26 ALIAS ulid26)

target_sources(ulid26
    INTERFACE FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ulid26/generators.hpp
        ulid26/ulid26.hpp
        ulid26/value.hpp
)
target_compile_features(ulid26 INTERFACE cxx_std_23)
if(ULID26_FORCE_MUTEX)
    target_compile_definitions(ulid26 INTERFACE ULID26_FORCE_MUTEX=1)
endif()

# ---- Install, package exports, CPack ----
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ulid26_install.cmake)

# ---- Subdirectories ----
# CTest defines the universal BUILD_TESTING knob; a packager's
# -DBUILD_TESTING=OFF must win over our default-ON options below.
# The strict warning set lives here too: only test and example binaries use it.
if(ULID26_BUILD_TESTS OR ULID26_BUILD_EXAMPLES)
    include(CTest) # calls enable_testing() itself
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ulid26_warnings.cmake)
endif()
if(ULID26_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(test)
endif()
if(ULID26_BUILD_EXAMPLES AND BUILD_TESTING)
    add_subdirectory(examples)
endif()
if(ULID26_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
if(ULID26_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
