# SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
# SPDX-License-Identifier: MIT
#
# Strict warnings for our own executables, scoped per toolchain so the flags never leak to
# third-party sources or downstream consumers (an INTERFACE propagation of -Werror would be
# hostile). The classic set (-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow
# -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wpedantic), plus -Wnull-dereference and -Weffc++
# (both verified silent on this codebase) and the GCC-only -Wuseless-cast, -Wduplicated-* and
# -Wlogical-op. Deliberately absent: -Weverything (denylist model — new Clang versions add warnings
# that would break the build on toolchain upgrade; the explicit allowlist is stable) and
# -Wlifetime (only exists on a special Clang branch). Since the library is INTERFACE (header-only)
# the test and example binaries are the only compiled code where we it makes sense to apply those
# warnings. Benchmarks and fuzz stay warning-clean by construction instead.
add_library(ulid26_warnings INTERFACE)
target_compile_options(ulid26_warnings INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wshadow;-Wnon-virtual-dtor;-Wold-style-cast;-Wcast-align;-Wnull-dereference;-Weffc++;-Wpedantic;-Wuseless-cast;-Wdouble-promotion;-Wduplicated-cond;-Wduplicated-branches;-Wlogical-op;-Wformat=2;-Wimplicit-fallthrough=5>
    $<$<CXX_COMPILER_ID:Clang>:-Wall;-Wextra;-Werror;-Wconversion;-Wsign-conversion;-Wshadow;-Wnon-virtual-dtor;-Wold-style-cast;-Wcast-align;-Wnull-dereference;-Weffc++;-Wpedantic;-Wdouble-promotion;-Wformat=2;-Wimplicit-fallthrough>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/utf-8;/Zc:__cplusplus>
)
