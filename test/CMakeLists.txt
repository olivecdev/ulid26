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
# Strict warnings for our own executables, scoped per toolchain so the flags never leak to
# third-party sources or downstream consumers (an INTERFACE propagation of -Werror would be
# hostile). The classic set (-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
# -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wpedantic), plus -Wnull-dereference and -Weffc++
# (both verified silent on this codebase) and the GCC-only -Wuseless-cast, -Wduplicated-* and
# -Wlogical-op. Deliberately absent: -Weverything (denylist model — new Clang versions add warnings
# that would break the build on toolchain upgrade; the explicit allowlist is stable) and
# -Wlifetime (only exists on a special Clang branch). Since the library is INTERFACE (header-only)
# the test binaries are the only compiled code where we it makes sense to apply those warnings.
# We could also apply in benchmarks and fuzz testing, but that's really not necessary if we get
# enough coverage with just the tests.
add_library(ulid26_warnings INTERFACE)
target_compile_options(ulid26_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wshadow;-Wnon-virtual-dtor;-Wold-style-cast;-Wcast-align;-Wnull-dereference;-Weffc++;-Wpedantic;-Wuseless-cast;-Wdouble-promotion;-Wduplicated-cond;-Wduplicated-branches;-Wlogical-op;-Wformat=2;-Wimplicit-fallthrough=5>
    $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wshadow;-Wnon-virtual-dtor;-Wold-style-cast;-Wcast-align;-Wnull-dereference;-Weffc++;-Wpedantic;-Wdouble-promotion;-Wformat=2;-Wimplicit-fallthrough>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8;/Zc:__cplusplus>
)

# boost-ext/ut: single-header, macro-free test framework for the test
# binaries (test-only dependency, never linked into the library itself).
# Discovery prefers a system copy — nixpkgs `ut` in our dev shell, Debian
# libboost-ext-ut-dev, Conan boost-ext-ut — so offline packager builds test
# with no network; otherwise the pinned release header below is fetched.
# The header is fully self-contained (no sibling includes): one 100 KB
# download, pinned twice — the URL names the exact release and EXPECTED_HASH
# verifies the content. The download dir derives from the version, so bumps
# can never go stale.
find_package(ut QUIET)
if(NOT TARGET Boost::ut)
    set(UT_VERSION v2.3.1)
    set(UT_HEADER_DIR "${CMAKE_BINARY_DIR}/_deps/ut-${UT_VERSION}")
    set(UT_HEADER "${UT_HEADER_DIR}/boost/ut.hpp")
    if(NOT EXISTS "${UT_HEADER}")
        file(
            DOWNLOAD
                https://raw.githubusercontent.com/boost-ext/ut/${UT_VERSION}/include/boost/ut.hpp
                "${UT_HEADER}"
            EXPECTED_HASH SHA256=5be9e228ba9055917e5c98a042858ae624ca9285e985b8297bd3ac2e2e49967d
            TLS_VERIFY ON
            TIMEOUT 60
        )
    endif()

    add_library(ut INTERFACE)
    target_include_directories(ut SYSTEM INTERFACE "${UT_HEADER_DIR}")
    add_library(Boost::ut ALIAS ut)
endif()

add_executable(ulid26_value_test value_test.cpp)
target_link_libraries(ulid26_value_test PRIVATE ulid26_warnings ulid26 Boost::ut)
add_test(NAME ulid26_value_test COMMAND ulid26_value_test)

add_executable(ulid26_generate_test generate_test.cpp)
target_link_libraries(ulid26_generate_test PRIVATE ulid26_warnings ulid26 Boost::ut)
add_test(NAME ulid26_generate_test COMMAND ulid26_generate_test)

if(NOT ULID26_FORCE_MUTEX)
    add_executable(ulid26_generate_mutex_test generate_test.cpp)
    target_link_libraries(ulid26_generate_mutex_test PRIVATE ulid26_warnings ulid26 Boost::ut)
    target_compile_definitions(ulid26_generate_mutex_test PRIVATE ULID26_FORCE_MUTEX=1)
    add_test(NAME ulid26_generate_mutex_test COMMAND ulid26_generate_mutex_test)
endif()

