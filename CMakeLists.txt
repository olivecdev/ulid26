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

option(ulid26_BUILD_TESTS "Build ulid26 tests" ${PROJECT_IS_TOP_LEVEL})
add_library(ulid26)
add_library(olivec::ulid26 ALIAS ulid26)

target_sources(ulid26
    PUBLIC FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
        ulid26/ulid26.hpp
)
target_compile_features(ulid26 PUBLIC cxx_std_26)

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

if(ulid26_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(test)
endif()

set(CPACK_PACKAGE_NAME "ulid26")
set(CPACK_PACKAGE_VENDOR "olivec.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++26 ULID implementation")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
