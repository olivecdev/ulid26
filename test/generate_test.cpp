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
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <thread>
#include <ulid26/generators.hpp>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint64_t kMaxTimestamp = (std::uint64_t{1} << 48) - 1;
    constexpr std::uint64_t kEntropyMax   = ~std::uint64_t{0};
    constexpr std::uint64_t kHighMask     = 0xFFFF;

    // A deterministic engine that makes the generator's own engine observable:
    // the first two 64-bit words are 0 and 1, so the resulting entropy halves
    // are fixed rather than thread-local random.
    class fixed_rng {
      public:
        using result_type = std::uint64_t;

        static constexpr result_type min() { return 0; }

        static constexpr result_type max() { return ~result_type{0}; }

        result_type operator()() { return next_++; }

      private:
        result_type next_ = 0;
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

    static_assert(
        ulid26::detail::decide_advance((std::uint64_t{1234} << 16) | kHighMask, kEntropyMax, 1234)
            .error() == ulid26::errc::generation_overflow
    );
    static_assert(
        ulid26::detail::decide_advance((std::uint64_t{1234} << 16), 0, 1233).error() ==
        ulid26::errc::time_reversed
    );
    static_assert(
        ulid26::detail::grant_from_start(
            (std::uint64_t{1234} << 16) | kHighMask, kEntropyMax - 2, 10
        ) == 3
    );
    static_assert(
        ulid26::detail::grant_from_start(
            (std::uint64_t{1234} << 16) | kHighMask, kEntropyMax, 10
        ) == 1
    );

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
        auto const id = ulid26::generate<std::mt19937_64>();
        expect(id.timestamp() > 0U) << "custom engine plugs into generate";
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

    "custom engine generator"_test = [] {
        ulid26::unsynchronized_generator<std::mt19937_64> generator{std::mt19937_64{7}};
        auto const                                        first  = generator.next();
        auto const                                        second = generator.next();
        expect(first.has_value() && second.has_value()) << "values";
        if (first && second) {
            expect(*second > *first) << "monotonic";
        }
    };

    "generator uses its own engine"_test = [] {
        ulid26::unsynchronized_generator<fixed_rng> generator{fixed_rng{}};
        auto const                                  block = generator.reserve(1);
        expect(block.has_value() && block->size() == 1U) << "one id";
        if (!block || block->empty()) {
            return;
        }
        std::array<std::uint8_t, ulid26::ulid::kBinarySize> bytes{};
        block->first().to_bytes(std::span<std::uint8_t, ulid26::ulid::kBinarySize>{bytes});
        expect(bytes[6] == 0U && bytes[7] == 0U) << "entropy high half comes from word zero";
        for (std::size_t i = 8; i < 14; ++i) {
            expect(bytes[i] == 0U) << "entropy low half starts empty";
        }
        expect(bytes[14] == 1U && bytes[15] == 0U) << "second engine word enters the low half";
    };

    "reservation range contract"_test = [] {
        ulid26::unsynchronized_generator generator;
        constexpr std::uint64_t          kCount = 64;
        auto const                       block  = generator.reserve(kCount);
        expect(block.has_value()) << "reservation succeeds";
        if (!block) {
            return;
        }
        expect(block->size() == kCount) << "exact grant in a fresh millisecond";
        expect(std::ranges::size(*block) == kCount) << "range size agrees";
        expect(block->first() == (*block)[0]) << "first is index zero";
        expect(block->last() == (*block)[kCount - 1]) << "last is index size - 1";
        expect(block->first() < block->last()) << "block spans a sorted window";

        std::uint64_t index = 0;
        for (auto const id : *block) {
            expect(id == (*block)[index]) << "iteration matches indexing";
            ++index;
        }
        expect(index == kCount) << "range-for visits every id";

        auto const copy = *block;
        expect(std::ranges::equal(*block, copy)) << "copying does not mint a new block";

        auto const after = generator.next();
        expect(after.has_value() && *after > block->last()) << "next continues after the block";
    };

    "reservation composes with views"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       block = generator.reserve(16);
        expect(block.has_value()) << "reservation succeeds";
        if (!block) {
            return;
        }
        std::vector<ulid26::ulid> taken{};
        for (auto const id : *block | std::views::take(4)) {
            taken.push_back(id);
        }
        expect(taken.size() == 4U) << "take() works on the reservation view";
        expect(taken.front() == block->first()) << "take preserves order";
        expect(taken.back() == (*block)[3]) << "take stops at the requested count";
    };

    "zero reservation is empty success"_test = [] {
        ulid26::unsynchronized_generator generator;
        auto const                       block = generator.reserve(0);
        expect(block.has_value()) << "zero is not an error";
        if (block) {
            expect(block->empty()) << "zero-sized block";
            expect(block->size() == 0U) << "size zero";
        }
    };

    "clock regression fails closed"_test = [] {
        auto const                       future = make_ulid(kMaxTimestamp, 0, 0);
        ulid26::unsynchronized_generator generator{future};
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

    "shared reservations are disjoint and ordered"_test = [] {
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
            expect(block.size() == kPerThread) << "each thread got a full block";
            expect(std::ranges::is_sorted(block)) << "each block is ordered";
            all.insert(all.end(), block.begin(), block.end());
        }
        std::ranges::sort(all);
        expect(std::adjacent_find(all.begin(), all.end()) == all.end())
            << "blocks are disjoint across threads";
    };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
