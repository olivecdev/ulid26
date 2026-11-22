// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <print>
#include <string_view>
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>

namespace {
    int fail(std::string_view message) {
        std::println("FAIL: {}", message);
        return 1;
    }

    template <typename Produce>
    auto retry_on_regression(Produce&& produce) {
        auto result = produce();
        for (int attempt = 0;
             !result && result.error() == ulid26::make_error_code(ulid26::errc::time_reversed) &&
             attempt < 8;
             ++attempt) {
            result = produce();
        }
        return result;
    }

    int note_or_fail(std::error_code error, std::string_view what) {
        if (error == ulid26::make_error_code(ulid26::errc::time_reversed)) {
            std::println("note: wall clock stepped backwards; nothing to prove here");
            return 0;
        }
        return fail(what);
    }
} // namespace

int main() {
    ulid26::unsynchronized_generator<ulid26::default_rng> generator{ulid26::default_rng{42}};

    auto first = retry_on_regression([&generator] { return generator.next(); });
    if (!first) {
        return note_or_fail(first.error(), "next failed");
    }
    auto second = retry_on_regression([&generator] { return generator.next(); });
    if (!second) {
        return note_or_fail(second.error(), "next failed");
    }
    if (!(*first < *second)) {
        return fail("monotonic increase broken");
    }

    auto block = retry_on_regression([&generator] { return generator.reserve(64); });
    if (!block) {
        return note_or_fail(block.error(), "reserve failed");
    }
    if (block->empty() || block->size() > 64U) {
        return fail("reservation size out of range");
    }
    if (!(block->first() < block->last())) {
        return fail("reservation first/last unordered");
    }
    if (!((*block)[0ULL] == block->first())) {
        return fail("reservation index zero is not first");
    }
    if (!((*block)[block->size() - 1ULL] == block->last())) {
        return fail("reservation index size-1 is not last");
    }

    bool          seen_first = false;
    ulid26::ulid  previous{};
    std::uint64_t count = 0;
    for (ulid26::ulid id : *block) {
        if (seen_first && !(previous < id)) {
            return fail("reservation not strictly increasing");
        }
        previous   = id;
        seen_first = true;
        ++count;
    }
    if (count != block->size()) {
        return fail("reservation iteration short");
    }

    ulid26::shared_generator<> shared;
    auto shared_one = retry_on_regression([&shared] { return shared.next(); });
    if (!shared_one) {
        return note_or_fail(shared_one.error(), "shared next failed");
    }

    std::println("bulk OK: drained {} ids, last {}", count, block->last());
    return 0;
}
