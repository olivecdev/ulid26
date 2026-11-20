// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Unit tests for the generation layer: one-shot generation, both monotonic
// generators, reservations as ranges, the best-effort capacity contract and
// the pure state-machine decisions. boost-ext/ut drives the runtime cases.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <thread>
#include <ulid26/generators.hpp>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint64_t kEntropyMax = ~std::uint64_t{0};
    constexpr std::uint64_t kHighMask   = 0xFFFF;

    struct engine_failure {};

    class throwing_rng {
      public:
        using result_type = std::uint64_t;

        static constexpr result_type min() { return 0; }

        static constexpr result_type max() { return ~result_type{0}; }

        result_type operator()() { throw engine_failure{}; }
    };

    [[nodiscard]]
    ulid26::ulid
    make_ulid(std::uint64_t timestamp, std::uint64_t entropy_hi, std::uint64_t entropy_lo) {
        std::array<std::uint8_t, ulid26::ulid::kBinarySize> bytes{};
        std::uint64_t const hi = (timestamp << 16) | (entropy_hi & kHighMask);
        for (std::size_t i = 0; i < 8; ++i) {
            bytes[i]     = static_cast<std::uint8_t>((hi >> (56 - (i * 8))) & 0xFF);
            bytes[8 + i] = static_cast<std::uint8_t>((entropy_lo >> (56 - (i * 8))) & 0xFF);
        }
        return ulid26::ulid::from_bytes(
            std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{bytes}
        );
    }

    static_assert(std::ranges::view<ulid26::reservation>);
    static_assert(std::ranges::random_access_range<ulid26::reservation>);
    static_assert(std::ranges::sized_range<ulid26::reservation>);
    static_assert(std::ranges::common_range<ulid26::reservation>);
    static_assert(std::uniform_random_bit_generator<ulid26::default_rng>);
    static_assert(std::uniform_random_bit_generator<ulid26::system_rng>);

} // namespace

int main() {
    using namespace boost::ut;

    "single-shot generation"_test = [] {
        auto const first  = ulid26::generate();
        auto const second = ulid26::generate();
        expect(first != second) << "fresh entropy per one-shot";
        expect(first.timestamp() > 0U) << "timestamp comes from the clock";
        expect(first.timestamp() <= second.timestamp() + 1U) << "timestamps do not run wild";
    };

    "custom engine one-shot"_test = [] {
        bool threw = false;
        try {
            (void)ulid26::generate<throwing_rng>();
        } catch (engine_failure const&) { threw = true; }
        expect(threw) << "generate uses the requested engine";
    };

    "seeded engine is deterministic"_test = [] {
        ulid26::default_rng left{42};
        ulid26::default_rng right{42};
        for (std::size_t i = 0; i < 16; ++i) {
            expect(left() == right()) << "same seed, same word stream";
        }
    };

    "local burst is strictly increasing"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       first = generator.next();
        expect(first.has_value()) << "first id";
        if (!first) {
            return;
        }
        ulid26::ulid previous = *first;
        for (std::size_t i = 0; i < 1024; ++i) {
            auto const current = generator.next();
            expect(current.has_value()) << "id available";
            if (!current) {
                return;
            }
            expect(*current > previous) << "strictly increasing";
            previous = *current;
        }
    };

    "entropy exhaustion fails closed"_test = [] {
        constexpr int kAttempts = 8;
        bool          observed  = false;
        for (int attempt = 0; attempt < kAttempts && !observed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto const                       now = ulid26::generate().timestamp();
            ulid26::unsynchronized_generator generator{make_ulid(now, kHighMask, kEntropyMax)};
            auto const                       single = generator.next();
            if (!single) {
                if (single.error() != ulid26::make_error_code(ulid26::errc::generation_overflow)) {
                    expect(false) << "unexpected next() error";
                    return;
                }
                auto const block = generator.reserve(1);
                expect(block.has_value()) << "exhausted reservation is success";
                if (block) {
                    expect(block->empty()) << "exhausted reservation is empty";
                    observed = true;
                }
            }
        }
        expect(observed) << "same-millisecond exhaustion observed";
    };

    "reservation can be short near entropy exhaustion"_test = [] {
        constexpr std::uint64_t kRequested = 10;
        constexpr std::uint64_t kGranted   = 3;
        constexpr int           kAttempts  = 8;
        bool                    observed   = false;
        for (int attempt = 0; attempt < kAttempts && !observed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto const                       now = ulid26::generate().timestamp();
            ulid26::unsynchronized_generator generator{
                make_ulid(now, kHighMask, kEntropyMax - kGranted)
            };
            auto const block = generator.reserve(kRequested);
            expect(block.has_value()) << "short reservation must succeed";
            if (!block) {
                return;
            }
            if (block->size() != kGranted) {
                continue;
            }
            observed = true;
            expect(block->first().timestamp() == now) << "short grant stays in one millisecond";
            expect(block->last() == make_ulid(now, kHighMask, kEntropyMax))
                << "short grant stops at entropy maximum";
        }
        expect(observed) << "same-millisecond short grant observed";
    };

    "custom engine generator"_test = [] {
        ulid26::unsynchronized_generator<throwing_rng> generator;
        bool                                           threw = false;
        try {
            (void)generator.reserve(1);
        } catch (engine_failure const&) { threw = true; }
        expect(threw) << "reservation uses the generator's engine";
    };

    "reservation range contract"_test = [] {
        ulid26::unsynchronized_generator generator;
        constexpr std::uint64_t          kCount = 64;
        auto const                       block  = generator.reserve(kCount);
        expect(block.has_value()) << "reservation succeeds";
        if (!block) {
            return;
        }
        auto const count = block->size();
        expect(count > 0U && count <= kCount) << "best-effort grant is bounded";
        expect(std::ranges::size(*block) == count) << "range size agrees";
        expect((block->end() - block->begin()) == static_cast<std::ptrdiff_t>(count))
            << "iterator distance agrees";

        std::vector<ulid26::ulid> const collected{block->begin(), block->end()};
        expect(collected.size() == count) << "iteration visits every id";
        expect(collected.back() == block->last()) << "last matches iteration";
        expect(block->first() < block->last()) << "block spans a sorted window";

        auto const timestamp = collected.front().timestamp();
        for (std::size_t index = 0; index < collected.size(); ++index) {
            expect(collected[index] == (*block)[index]) << "indexing matches iteration";
            expect(collected[index].timestamp() == timestamp) << "one timestamp";
            if (index > 0) {
                expect(collected[index] > collected[index - 1]) << "strictly increasing";
            }
        }

        auto const copy = *block;
        expect(std::ranges::equal(*block, copy)) << "copying does not mint a new block";

        auto const after = generator.next();
        expect(after.has_value() && *after > block->last()) << "next continues after the block";
    };

    "reservation random access"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       block = generator.reserve(8);
        expect(block.has_value()) << "reservation succeeds";
        if (!block || block->size() != 8U) {
            return;
        }
        auto const begin = block->begin();
        auto const end   = block->end();
        expect((end - begin) == static_cast<std::ptrdiff_t>(8)) << "difference reports the size";
        expect(begin < end) << "begin sorts before end";

        auto position  = begin;
        position      += 3;
        expect(position == begin + 3) << "addition agrees";
        expect(*position == (*block)[3]) << "iterator value agrees with indexing";
        position -= 2;
        expect(position == begin + 1) << "subtraction agrees";

        auto last = end;
        --last;
        expect(*last == block->last()) << "reverse step reaches the last id";
    };

    "moving a generator re-seeds the source"_test = [] {
        ulid26::unsynchronized_generator source;
        auto const                       first = source.next();
        expect(first.has_value()) << "source generates";
        if (!first) {
            return;
        }
        ulid26::unsynchronized_generator destination{std::move(source)};
        auto const                       second = destination.next();
        expect(second.has_value() && *second > *first) << "destination continues the sequence";
        auto const third = source.next();
        expect(third.has_value()) << "moved-from generator reseeds";
    };

    "reservation composes with views"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       block = generator.reserve(16);
        expect(block.has_value()) << "reservation succeeds";
        if (!block || block->size() < 4U) {
            return;
        }
        std::vector<ulid26::ulid> const expected{block->begin(), block->begin() + 4};
        std::vector<ulid26::ulid>       taken{};
        for (auto const id : *block | std::views::take(4)) {
            taken.push_back(id);
        }
        expect(taken == expected) << "take matches the reservation prefix";
        expect(std::ranges::is_sorted(taken)) << "take preserves order";
    };

    "zero reservation is empty success"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       block = generator.reserve(0);
        expect(block.has_value()) << "zero is not an error";
        if (block) {
            expect(block->empty()) << "zero-sized block";
            expect(block->begin() == block->end()) << "empty range has no distance";
        }
        auto const after = generator.next();
        expect(after.has_value()) << "zero reservation leaves the state untouched";
    };

    "clock regression fails closed"_test = [] {
        auto const future = ulid26::try_parse("7ZZZZZZZZZ0000000000000000");
        expect(future.has_value()) << "future vector parses";
        if (!future) {
            return;
        }
        ulid26::unsynchronized_generator generator{*future};
        auto const                       single = generator.next();
        expect(!single.has_value()) << "single-value generation fails";
        if (!single) {
            expect(single.error() == ulid26::make_error_code(ulid26::errc::time_reversed))
                << "time_reversed";
        }
        auto const block = generator.reserve(4);
        expect(!block.has_value()) << "reservation fails too";
        if (!block) {
            expect(block.error() == ulid26::make_error_code(ulid26::errc::time_reversed))
                << "same error";
        }
    };

    "shared reservations are disjoint across threads"_test = [] {
        constexpr std::size_t                  kThreads   = 4;
        constexpr std::uint64_t                kPerThread = 64;
        std::vector<std::vector<ulid26::ulid>> output(kThreads);
        std::vector<std::thread>               threads{};
        threads.reserve(kThreads);
        for (std::size_t thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([&output, thread] {
                ulid26::shared_generator generator;
                auto const               block = generator.reserve(kPerThread);
                if (!block) {
                    return;
                }
                output.at(thread).assign(block->begin(), block->end());
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        std::vector<ulid26::ulid> all{};
        for (auto const& block : output) {
            expect(!block.empty()) << "each thread got ids";
            expect(block.size() <= kPerThread) << "each grant is bounded";
            expect(std::ranges::is_sorted(block)) << "each block is ordered";
            all.insert(all.end(), block.begin(), block.end());
        }
        std::ranges::sort(all);
        expect(std::adjacent_find(all.begin(), all.end()) == all.end())
            << "blocks are disjoint across threads";
    };

    "shared handles share one sequence"_test = [] {
        ulid26::shared_generator left;
        ulid26::shared_generator right;
        auto const               first  = left.reserve(1);
        auto const               second = right.reserve(1);
        expect(first.has_value() && second.has_value()) << "both reservations succeed";
        if (first && second && !first->empty() && !second->empty()) {
            expect(first->last() < second->first()) << "separate handles advance one sequence";
        }
    };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
