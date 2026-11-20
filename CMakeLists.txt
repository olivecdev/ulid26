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
    VERSION 0.0.1
    DESCRIPTION "C++23 ULID implementation"
    HOMEPAGE_URL "https://olivec.dev"
    LANGUAGES CXX
)

option(ULID26_BUILD_TESTS "Build ulid26 tests" ${PROJECT_IS_TOP_LEVEL})
option(ULID26_BUILD_BENCHMARKS "Build ulid26 benchmarks" OFF)
option(ULID26_BUILD_FUZZ "Build ulid26 fuzz harness (Clang + libFuzzer only)" OFF)

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

include(GNUInstallDirs)

install(TARGETS ulid26
    EXPORT ulid26Targets
    FILE_SET HEADERS
)

install(EXPORT ulid26Targets
    FILE ulid26Config.cmake
    NAMESPACE ulid26::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ulid26
)

# Version file for classic consumers: enables find_package(ulid26 <ver>).
# SameMajorVersion is the library-appropriate policy: the major must match and
# the installed version must cover the request, so 0.x churn stays fail-closed
# without forcing downstream to pin every release (unlike ExactVersion).
# (The CPS path is strict by default: COMPAT_VERSION falls back to VERSION.)
# ARCH_INDEPENDENT because header-only ships no binaries.
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/ulid26ConfigVersion.cmake"
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)
install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/ulid26ConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ulid26"
)

# CPS export (Common Package Specification): declarative sibling of the CMake
# export above, consumable by non-CMake build tools. install(PACKAGE_INFO) is
# stable since CMake 4.3 (experimental gate before that); older configures keep
# the 3.26 floor and simply skip the .cps file.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.3")
    # VERSION and DESCRIPTION are inherited from project() (package name
    # matches PROJECT_NAME); LICENSE has no project() slot, so it lives here.
    # Compatibility story (semver): with no COMPAT_VERSION, CPS accepts only
    # the exact version — correct at 0.x (no promises), but the first 1.x
    # release must add VERSION ${PROJECT_VERSION} (required alongside
    # COMPAT_VERSION — inheritance does not satisfy it) plus
    # COMPAT_VERSION <major>.0, or same-major requests will keep being
    # rejected. Probes proved both sides: bare 1.2.3 rejects 1.0, while the
    # 1.0 floor accepts 1.0/1.2 and rejects 0.9/2.0.
    install(PACKAGE_INFO ulid26
        EXPORT ulid26Targets
        LICENSE "MIT"
    )
endif()

if(ULID26_BUILD_TESTS)
    include(CTest) # calls enable_testing() itself; defines BUILD_TESTING
    # Honor the universal knob too: a packager's -DBUILD_TESTING=OFF must win
    # over our default-ON, on every path where CTest defined the variable.
    if(BUILD_TESTING)
        add_subdirectory(test)
    endif()
endif()

if(ULID26_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(ULID26_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()

# Binary packagers (deb/rpm/vcpkg/...) own their packaging; CPack is only our
# own TGZ/ZIP convenience, so it stays out of embedded builds entirely —
# include(CPack) would otherwise add package* targets to the consumer.
if(PROJECT_IS_TOP_LEVEL)
    # NAME and VERSION intentionally unset: CPack defaults them to
    # PROJECT_NAME / PROJECT_VERSION, and repeating them here only risks drift.
    set(CPACK_PACKAGE_VENDOR "olivec.dev")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    include(CPack)
endif()
