# SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
# SPDX-License-Identifier: MIT
#
# End-to-end usage proofs, also linked from the README quick start. Each is a
# complete program: nonzero exit on failure, so plain ctest runs them with no
# golden output to maintain. They stay out of the clang-tidy gate (usage, not
# test TUs) and install nothing.
add_executable(ulid26_example_value value_parse_print.cpp)
target_link_libraries(ulid26_example_value PRIVATE ulid26_warnings ulid26)
add_test(NAME ulid26_example_value COMMAND ulid26_example_value)

add_executable(ulid26_example_one_shot generate_one_shot.cpp)
target_link_libraries(ulid26_example_one_shot PRIVATE ulid26_warnings ulid26)
add_test(NAME ulid26_example_one_shot COMMAND ulid26_example_one_shot)

add_executable(ulid26_example_bulk generate_bulk.cpp)
target_link_libraries(ulid26_example_bulk PRIVATE ulid26_warnings ulid26)
add_test(NAME ulid26_example_bulk COMMAND ulid26_example_bulk)

add_executable(ulid26_example_shared_mt generate_shared_mt.cpp)
target_link_libraries(ulid26_example_shared_mt PRIVATE ulid26_warnings ulid26)
add_test(NAME ulid26_example_shared_mt COMMAND ulid26_example_shared_mt)
