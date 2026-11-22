# SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
# SPDX-License-Identifier: MIT
#
# Install, CMake/CPS package exports, and CPack convenience packaging.
# Included from the top-level CMakeLists after the ulid26 target exists.
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
