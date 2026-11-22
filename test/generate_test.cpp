// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <algorithm>
#include <array>
#include <boost/ut.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <random>
#include <ranges>
#include <thread>
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint64_t kEntropyMax = ~std::uint64_t{0};
    constexpr std::uint64_t kHighMask   = 0xFFFF;

    struct engine_failure : std::exception {};

    class throwing_rng {
      public:
        using result_type = std::uint64_t;

        static constexpr result_type min() { return 0; }

        static constexpr result_type max() { return ~result_type{0}; }

        result_type operator()() const {
            if (fail) {
                throw engine_failure{};
            }
            return 0;
        }

        bool volatile fail = true;
    };

    [[nodiscard]]
    ulid26::ulid make_ulid(std::uint64_t timestamp, ulid26::detail::parts entropy) {
        constexpr std::uint64_t kTimestampShift = 16;
        return ulid26::detail::assemble_ulid(
            ulid26::detail::parts{
                .hi = (timestamp << kTimestampShift) | (entropy.hi & kHighMask),
                .lo = entropy.lo,
            }
        );
    }

    static_assert(std::ranges::view<ulid26::reservation>);
    static_assert(std::ranges::random_access_range<ulid26::reservation>);
    static_assert(std::ranges::sized_range<ulid26::reservation>);
    static_assert(std::ranges::common_range<ulid26::reservation>);
    static_assert(std::uniform_random_bit_generator<ulid26::default_rng>);
    static_assert(std::uniform_random_bit_generator<ulid26::system_rng>);

} // namespace

namespace {
    using namespace boost::ut;

    void test_single_shot_generation() {
        auto const first  = ulid26::generate();
        auto const second = ulid26::generate();
        expect(first != second) << "fresh entropy per one-shot";
        expect(first.timestamp() > 0U) << "timestamp comes from the clock";
        expect(first.timestamp() <= second.timestamp() + 1U) << "timestamps do not run wild";
    }

    void test_custom_engine_one_shot() {
        bool threw = false;
        try {
            (void)ulid26::generate<throwing_rng>();
        } catch (engine_failure const&) { threw = true; }
        expect(threw) << "generate uses the requested engine";
    }

    void test_seeded_engine_is_deterministic() {
        ulid26::default_rng left{42};
        ulid26::default_rng right{42};
        for (std::size_t i = 0; i < 16; ++i) {
            expect(left() == right()) << "same seed, same word stream";
        }
    }

    void test_rfc_8439_chacha20_block_vector() {
        constexpr std::array<std::uint32_t, 16> input = {
            0x61707865U, 0x3320646EU, 0x79622D32U, 0x6B206574U, 0x03020100U, 0x07060504U,
            0x0B0A0908U, 0x0F0E0D0CU, 0x13121110U, 0x17161514U, 0x1B1A1918U, 0x1F1E1D1CU,
            0x00000001U, 0x09000000U, 0x4A000000U, 0x00000000U,
        };
        constexpr std::array<std::uint32_t, 16> expected = {
            0xE4E7F110U, 0x15593BD1U, 0x1FDD0F50U, 0xC47120A3U, 0xC7F4D1C7U, 0x0368C033U,
            0x9AAA2204U, 0x4E6CD4C3U, 0x466482D2U, 0x09AA9F07U, 0x05D7C214U, 0xA2028BD9U,
            0xD19C12B5U, 0xB94E16DEU, 0xE883D0CBU, 0x4E3C50A2U,
        };
        expect(ulid26::detail::chacha20_block(input) == expected)
            << "RFC 8439 section 2.3.2 block vector";
    }

    void test_reservation_count_is_capped_at_int64_max() {
        constexpr auto kCap = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        ulid26::unsynchronized_generator<> generator;
        auto const block = generator.reserve(std::numeric_limits<std::uint64_t>::max());
        expect(block.has_value()) << "oversized request still succeeds";
        if (block) {
            expect(block->size() == kCap) << "grant is capped";
            expect(block->end() - block->begin() == std::numeric_limits<std::int64_t>::max())
                << "range difference is representable";
        }
    }

    void test_local_burst_is_strictly_increasing() {
        ulid26::unsynchronized_generator<> generator;
        auto const                         first = generator.next();
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
    }

    void test_entropy_exhaustion_fails_next_closed() {
        constexpr int kAttempts = 16;
        bool          observed  = false;
        for (int attempt = 0; attempt < kAttempts && !observed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto const                         now = ulid26::generate().timestamp();
            ulid26::unsynchronized_generator<> generator{
                make_ulid(now, {.hi = kHighMask, .lo = kEntropyMax})
            };
            auto const single = generator.next();
            if (single) {
                continue;
            }
            if (single.error() != ulid26::make_error_code(ulid26::errc::generation_overflow)) {
                expect(false) << "unexpected next() error";
                return;
            }
            observed = true;
        }
        expect(observed) << "same-millisecond exhaustion observed";
    }

    void test_exhausted_reservation_is_empty_success() {
        constexpr int kAttempts = 16;
        bool          observed  = false;
        for (int attempt = 0; attempt < kAttempts && !observed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto const                         now = ulid26::generate().timestamp();
            ulid26::unsynchronized_generator<> generator{
                make_ulid(now, {.hi = kHighMask, .lo = kEntropyMax})
            };
            auto const block = generator.reserve(1);
            expect(block.has_value()) << "exhausted reservation is success";
            if (!block) {
                return;
            }
            if (block->empty()) {
                observed = true;
            }
        }
        expect(observed) << "same-millisecond empty grant observed";
    }

    void test_reservation_can_be_short_near_entropy_exhaustion() {
        constexpr std::uint64_t kRequested = 10;
        constexpr std::uint64_t kGranted   = 3;
        constexpr int           kAttempts  = 8;
        bool                    observed   = false;
        for (int attempt = 0; attempt < kAttempts && !observed; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            auto const                         now = ulid26::generate().timestamp();
            ulid26::unsynchronized_generator<> generator{
                make_ulid(now, {.hi = kHighMask, .lo = kEntropyMax - kGranted})
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
            expect(block->last() == make_ulid(now, {.hi = kHighMask, .lo = kEntropyMax}))
                << "short grant stops at entropy maximum";
        }
        expect(observed) << "same-millisecond short grant observed";
    }

    void test_custom_engine_generator() {
        ulid26::unsynchronized_generator<throwing_rng> generator;
        bool                                           threw = false;
        try {
            expect(!generator.reserve(1).has_value()) << "reservation should not succeed";
        } catch (engine_failure const&) { threw = true; }
        expect(threw) << "reservation uses the generator's engine";
    }

    void test_reservation_range_contract() {
        ulid26::unsynchronized_generator<> generator;
        constexpr std::uint64_t            kCount = 64;
        auto const                         block  = generator.reserve(kCount);
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
    }

    void test_reservation_random_access() {
        ulid26::unsynchronized_generator<> generator;
        auto const                         block = generator.reserve(8);
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
    }

    void test_moving_a_generator_leaves_the_source_assignable() {
        ulid26::unsynchronized_generator<> source;
        auto const                         first = source.next();
        expect(first.has_value()) << "source generates";
        if (!first) {
            return;
        }
        ulid26::unsynchronized_generator<> destination{std::move(source)};
        auto const                         second = destination.next();
        expect(second.has_value() && *second > *first) << "destination continues the sequence";
        source           = ulid26::unsynchronized_generator<>{};
        auto const third = source.next();
        expect(third.has_value()) << "moved-from generator remains assignable";
    }

    void test_reservation_composes_with_views() {
        ulid26::unsynchronized_generator<> generator;
        auto const                         block = generator.reserve(16);
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
    }

    void test_zero_reservation_is_empty_success() {
        ulid26::unsynchronized_generator<> generator;
        auto const                         block = generator.reserve(0);
        expect(block.has_value()) << "zero is not an error";
        if (block) {
            expect(block->empty()) << "zero-sized block";
            expect(block->begin() == block->end()) << "empty range has no distance";
            expect(block->last() == ulid26::min()) << "empty block has a nil last";
        }
        auto const after = generator.next();
        expect(after.has_value()) << "zero reservation leaves the state untouched";

        ulid26::shared_generator<> shared;
        auto const                 shared_block = shared.reserve(0);
        expect(shared_block.has_value()) << "shared zero is not an error";
        if (shared_block) {
            expect(shared_block->empty()) << "shared zero-sized block";
        }
    }

    void test_state_machine_overflow_and_carry() {
        using ulid26::detail::decide_advance;
        std::uint64_t volatile at_modulus = ulid26::detail::kTimestampModulus;
        auto const overflow               = decide_advance({}, at_modulus);
        expect(!overflow.has_value()) << "unrepresentable clock fails";
        if (!overflow) {
            expect(overflow.error() == ulid26::errc::overflow) << "overflow";
        }

        std::uint64_t volatile zero = 0;
        auto const carry            = decide_advance({.hi = 0, .lo = kEntropyMax}, zero);
        expect(carry.has_value() && !carry->needs_fresh) << "exhausted low half carries";
        if (carry) {
            expect(carry->next.hi == 1 && carry->next.lo == 0) << "carry increments the high half";
        }
    }

    void test_clock_regression_fails_closed() {
        auto const future = ulid26::try_parse("7ZZZZZZZZZ0000000000000000");
        expect(future.has_value()) << "future vector parses";
        if (!future) {
            return;
        }
        ulid26::unsynchronized_generator<> generator{*future};
        auto const                         single = generator.next();
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
    }

    void test_shared_reservations_are_disjoint_across_threads() {
        constexpr std::size_t                  kThreads   = 4;
        constexpr std::uint64_t                kPerThread = 64;
        std::vector<std::vector<ulid26::ulid>> output(kThreads);
        std::vector<std::thread>               threads{};
        threads.reserve(kThreads);
        for (std::size_t thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back([&output, thread] {
                ulid26::shared_generator<> generator;
                auto const                 block = generator.reserve(kPerThread);
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
    }

    void test_shared_handles_share_one_sequence() {
        ulid26::shared_generator<> left;
        ulid26::shared_generator<> right;
        auto const                 first  = left.reserve(1);
        auto const                 second = right.reserve(1);
        expect(first.has_value() && second.has_value()) << "both reservations succeed";
        if (first && second && !first->empty() && !second->empty()) {
            expect(first->last() < second->first()) << "separate handles advance one sequence";
        }
    }

    void test_shared_clock_regression_fails_closed() {
        auto const future = ulid26::try_parse("7ZZZZZZZZZ0000000000000000");
        expect(future.has_value()) << "future vector parses";
        if (!future) {
            return;
        }
        auto const parts  = ulid26::detail::split_ulid(*future);
        auto&      shared = ulid26::detail::global_shared_state<ulid26::default_rng>();
        shared.data       = ulid26::detail::state{.ts_hi = parts.hi, .lo = parts.lo};

        ulid26::shared_generator<> generator;
        auto const                 single = generator.next();
        expect(!single.has_value()) << "shared single-value generation fails";
        if (!single) {
            expect(single.error() == ulid26::make_error_code(ulid26::errc::time_reversed))
                << "shared time_reversed";
        }
        auto const block = generator.reserve(4);
        expect(!block.has_value()) << "shared reservation fails too";
        if (!block) {
            expect(block.error() == ulid26::make_error_code(ulid26::errc::time_reversed))
                << "shared same error";
        }
        shared.data = {};
    }

    void test_shared_next_shares_one_sequence() {
        ulid26::shared_generator<> left;
        ulid26::shared_generator<> right;
        auto const                 first  = left.next();
        auto const                 second = right.next();
        expect(first.has_value() && second.has_value()) << "both single ids succeed";
        if (first && second) {
            expect(*first < *second) << "separate handles advance one sequence";
        }

        auto const block = left.reserve(4);
        auto const after = right.next();
        expect(block.has_value() && after.has_value()) << "mixed calls succeed";
        if (block && after && !block->empty()) {
            expect(block->last() < *after) << "next continues after a reservation";
        }
    }

    void run_tests() {
        "single-shot generation"_test         = test_single_shot_generation;
        "custom engine one-shot"_test         = test_custom_engine_one_shot;
        "seeded engine is deterministic"_test = test_seeded_engine_is_deterministic;
        "RFC 8439 ChaCha20 block vector"_test = test_rfc_8439_chacha20_block_vector;
        "reservation count is capped at int64 max"_test =
            test_reservation_count_is_capped_at_int64_max;
        "local burst is strictly increasing"_test     = test_local_burst_is_strictly_increasing;
        "entropy exhaustion fails next closed"_test   = test_entropy_exhaustion_fails_next_closed;
        "exhausted reservation is empty success"_test = test_exhausted_reservation_is_empty_success;
        "reservation can be short near entropy exhaustion"_test =
            test_reservation_can_be_short_near_entropy_exhaustion;
        "custom engine generator"_test    = test_custom_engine_generator;
        "reservation range contract"_test = test_reservation_range_contract;
        "reservation random access"_test  = test_reservation_random_access;
        "moving a generator leaves the source assignable"_test =
            test_moving_a_generator_leaves_the_source_assignable;
        "reservation composes with views"_test   = test_reservation_composes_with_views;
        "zero reservation is empty success"_test = test_zero_reservation_is_empty_success;
        "state machine overflow and carry"_test  = test_state_machine_overflow_and_carry;
        "clock regression fails closed"_test     = test_clock_regression_fails_closed;
        "shared reservations are disjoint across threads"_test =
            test_shared_reservations_are_disjoint_across_threads;
        "shared handles share one sequence"_test    = test_shared_handles_share_one_sequence;
        "shared clock regression fails closed"_test = test_shared_clock_regression_fails_closed;
        "shared next shares one sequence"_test      = test_shared_next_shares_one_sequence;
    }
} // namespace

int main() {
    try {
        run_tests();
    } catch (...) { return 1; }
    return 0;
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
