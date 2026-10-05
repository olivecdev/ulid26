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
project(ulid26 VERSION 0.0.1 LANGUAGES CXX)

option(ULID26_BUILD_TESTS "Build ulid26 tests" ${PROJECT_IS_TOP_LEVEL})
option(ULID26_BUILD_BENCHMARKS "Build ulid26 benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(ULID26_BUILD_FUZZ "Build ulid26 fuzz harness (Clang + libFuzzer only)" OFF)

# Single target, all headers: this is a small header-only library, so one target installs everything
# and every consumer links the same thing. Compile-time granularity lives in the headers, not the
# targets — include only the layer a TU needs (see the layering note in ulid26/ulid26.hpp).
add_library(ulid26 INTERFACE)
add_library(olivec::ulid26 ALIAS ulid26)

target_sources(ulid26
    INTERFACE FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ulid26/batch.hpp
        ulid26/generators.hpp
        ulid26/iostream.hpp
        ulid26/ranges.hpp
        ulid26/ulid26.hpp
        ulid26/value.hpp
)
target_compile_features(ulid26 INTERFACE cxx_std_23)

include(GNUInstallDirs)

install(TARGETS ulid26
    EXPORT ulid26Targets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ulid26Targets
    FILE ulid26Config.cmake
    NAMESPACE olivec::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ulid26
)

if(ULID26_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()

if(ULID26_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(ULID26_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

set(CPACK_PACKAGE_NAME "ulid26")
set(CPACK_PACKAGE_VENDOR "olivec.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++23 ULID implementation")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
