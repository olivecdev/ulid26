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
# ulid26_warnings lives in cmake/ulid26_warnings.cmake so tests and examples share
# the single strict set; link it (plus ulid26) into every binary below.
#
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

