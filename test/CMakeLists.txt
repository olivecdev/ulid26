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
# with no network; otherwise the pinned release header below is fetched from
# the first reachable of three mirrors. The header is fully self-contained
# (no sibling includes): one 100 KB download, pinned twice — the URLs name the
# exact release and the SHA256 below verifies the content. The download dir
# derives from the version, so bumps can never go stale. If no mirror is
# reachable, configuration continues without the tests (unless
# ULID26_REQUIRE_UT is set); examples never need the header.
find_package(ut QUIET)
if(NOT TARGET Boost::ut)
    set(UT_VERSION v2.3.1)
    set(UT_SHA256 5be9e228ba9055917e5c98a042858ae624ca9285e985b8297bd3ac2e2e49967d)
    set(UT_HEADER_DIR "${CMAKE_BINARY_DIR}/_deps/ut-${UT_VERSION}")
    set(UT_HEADER "${UT_HEADER_DIR}/boost/ut.hpp")
    if(NOT EXISTS "${UT_HEADER}")
        # Best-effort fetch: any single host may be unreachable from a given runner, so try
        # mirrors and never let a failed download fail the configure (offline packagers skip
        # the tests with a warning instead). Attempts are STATUS-checked and hash-verified by
        # hand: EXPECTED_HASH raises a fatal error even when STATUS is given.
        set(UT_URLS
            "https://raw.githubusercontent.com/boost-ext/ut/${UT_VERSION}/include/boost/ut.hpp"
            "https://cdn.jsdelivr.net/gh/boost-ext/ut@${UT_VERSION}/include/boost/ut.hpp"
            "https://github.com/boost-ext/ut/raw/${UT_VERSION}/include/boost/ut.hpp"
        )
        foreach(url IN LISTS UT_URLS)
            file(
                DOWNLOAD "${url}" "${UT_HEADER}"
                TLS_VERIFY ON
                TIMEOUT 30
                STATUS ut_status
            )
            list(GET ut_status 0 ut_code)
            if(ut_code EQUAL 0)
                file(SHA256 "${UT_HEADER}" UT_ACTUAL)
                if(UT_ACTUAL STREQUAL UT_SHA256)
                    break()
                endif()
            endif()
            file(REMOVE "${UT_HEADER}")
        endforeach()
        if(NOT EXISTS "${UT_HEADER}")
            set(ut_message "ulid26: boost-ext/ut ${UT_VERSION} not found and unreachable; skipping tests")
            if(ULID26_REQUIRE_UT)
                message(FATAL_ERROR "${ut_message}")
            endif()
            message(WARNING "${ut_message}")
            return()
        endif()
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

