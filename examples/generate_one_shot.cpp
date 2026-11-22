// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

#include <print>
#include <string>
#include <string_view>
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>

namespace {
    int fail(std::string_view message) {
        std::println("FAIL: {}", message);
        return 1;
    }
} // namespace

int main() {
    ulid26::ulid const first  = ulid26::generate();
    ulid26::ulid const second = ulid26::generate();
    if (first == second) {
        return fail("one-shot drew identical entropy twice");
    }

    std::string const text = first.to_string();
    auto const        back = ulid26::ulid::from_string(text);
    if (!back || *back != first) {
        return fail("one-shot round-trip mismatch");
    }

    ulid26::ulid const sys = ulid26::generate<ulid26::system_rng>();
    if (sys == first) {
        return fail("engines unexpectedly agreed");
    }

    std::println("one-shot OK: {}", text);
    std::println("system  OK: {}", sys);
    return 0;
}
