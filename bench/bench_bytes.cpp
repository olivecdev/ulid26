// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Throughput benchmarks for ulid26::ulid::to_bytes and ulid26::ulid::from_bytes:
// same harness and methodology as bench_to_chars (deterministic corpus,
// batch-size sweep, items-processed reporting). Run a Release build (e.g.
// the ci preset); Debug numbers are meaningless. The corpus reuses the eight
// known-valid seeds, parsed once, with the byte buffer spelled once at
// startup — so each benchmark measures one direction only.
//
// The corpus below is literal by nature, so magic-number warnings are
// suspended file-wide, as in the sibling benchmarks. The BENCHMARK
// registrations and BENCHMARK_MAIN are framework-imposed names, exempted
// inline below.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>
#include <vector>

#ifdef ULID26_HAVE_SUYASH
#include <ulid.hh>
#endif

namespace {

    constexpr std::size_t kMaxCount = 100000;

    // Deterministic corpus: the same eight known-valid ULIDs as the text
    // benchmarks (nil, max, the spec example and case/shape variants).
    constexpr std::array<std::string_view, 8> kSeeds = {
        "00000000000000000000000000", "7ZZZZZZZZZZZZZZZZZZZZZZZZZ", "01ARZ3NDEKTSV4RRFFQ69G5FAV",
        "01arz3ndektsv4rrffq69g5fav", "0123456789ABCDEFGHJKMNPQRS", "10ARZ3NDEKTSV4RRFFQ69G5FAV",
        "7HZZZZZZZZZZZZZZZZZZZZZZZZ", "00000000000000000000000001",
    };

    // Corpus guards: every seed is exactly one ULID long and parses, so
    // the benchmarks measure the byte path on pinned values (a bad seed
    // would silently benchmark garbage instead).
    constexpr bool all_seeds_sized() {
        for (std::string_view seed : kSeeds) {
            if (seed.size() != ulid26::ulid::kTextSize) {
                return false;
            }
        }
        return true;
    }

    constexpr bool all_seeds_parse() {
        for (std::string_view seed : kSeeds) {
            if (!ulid26::try_parse(seed).has_value()) {
                return false;
            }
        }
        return true;
    }

    static_assert(all_seeds_sized(), "every corpus seed is a 26-character ULID");
    static_assert(all_seeds_parse(), "every corpus seed parses");

#ifdef ULID26_HAVE_SUYASH
    // Suyash decodes uppercase only, so the reference corpus parses the
    // uppercased seeds: identical values on both sides, byte race on equal
    // footing. Startup-only cost, never on the timed path.
    std::string upper_copy(std::string_view seed) {
        std::string out{seed};
        for (char& unit : out) {
            if (unit >= 'a' && unit <= 'z') {
                unit = static_cast<char>(unit - ('a' - 'A'));
            }
        }
        return out;
    }
#endif

    struct corpus {
        corpus() {
            ids.reserve(kMaxCount);
            bytes.reserve(kMaxCount * ulid26::ulid::kBinarySize);
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                // Pinned valid above; .value() never throws here.
                auto const id = ulid26::try_parse(kSeeds.at(pos % kSeeds.size())).value();
                ids.push_back(id);
                std::array<std::byte, ulid26::ulid::kBinarySize> spelled{};
                id.to_bytes(std::span<std::byte, ulid26::ulid::kBinarySize>{spelled});
                for (std::byte unit : spelled) {
                    bytes.push_back(unit);
                }
            }
#ifdef ULID26_HAVE_SUYASH
            ref.reserve(kMaxCount);
            ref_bytes.reserve(kMaxCount * ulid26::ulid::kBinarySize);
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                auto const ref_id = ulid::Unmarshal(upper_copy(kSeeds.at(pos % kSeeds.size())));
                ref.push_back(ref_id);
                std::array<std::uint8_t, ulid26::ulid::kBinarySize> spelled{};
                ulid::MarshalBinaryTo(ref_id, spelled.data());
                for (std::uint8_t unit : spelled) {
                    ref_bytes.push_back(unit);
                }
            }
            // Agreement gate: identical values must spell identical bytes, or
            // the head-to-head is meaningless. Suyash lays out timestamp-first
            // big-endian, exactly like to_bytes.
            agrees = true;
            for (std::string_view seed : kSeeds) {
                std::array<std::byte, ulid26::ulid::kBinarySize> ours_spelled{};
                ulid26::try_parse(seed).value().to_bytes(
                    std::span<std::byte, ulid26::ulid::kBinarySize>{ours_spelled});
                std::array<std::uint8_t, ulid26::ulid::kBinarySize> ref_spelled{};
                ulid::MarshalBinaryTo(ulid::Unmarshal(upper_copy(seed)), ref_spelled.data());
                agrees = agrees && std::memcmp(ours_spelled.data(), ref_spelled.data(),
                                               ours_spelled.size()) == 0;
            }
#endif
        }

        std::vector<ulid26::ulid> ids{};
        std::vector<std::byte>    bytes{};
#ifdef ULID26_HAVE_SUYASH
        std::vector<ulid::ULID>  ref{};
        std::vector<std::uint8_t> ref_bytes{};
        bool                     agrees = true;
#endif
    };

    corpus const& shared_corpus() {
        static corpus const instance{};
        return instance;
    }

    void bench_to_bytes(benchmark::State& state) {
        std::size_t const count   = static_cast<std::size_t>(state.range(0));
        corpus const&     fixture = shared_corpus();
        std::vector<std::byte> output(count * ulid26::ulid::kBinarySize);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                fixture.ids.at(pos).to_bytes(std::span<std::byte, ulid26::ulid::kBinarySize>{
                    output.data() + pos * ulid26::ulid::kBinarySize,
                    ulid26::ulid::kBinarySize});
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_from_bytes(benchmark::State& state) {
        std::size_t const         count   = static_cast<std::size_t>(state.range(0));
        corpus const&             fixture = shared_corpus();
        std::vector<ulid26::ulid> output(count);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                output.at(pos) = ulid26::ulid::from_bytes(
                    std::span<std::byte const, ulid26::ulid::kBinarySize>{
                        fixture.bytes.data() + pos * ulid26::ulid::kBinarySize,
                        ulid26::ulid::kBinarySize});
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

#ifdef ULID26_HAVE_SUYASH
    void bench_suyash_marshal_binary(benchmark::State& state) {
        std::size_t const       count   = static_cast<std::size_t>(state.range(0));
        corpus const&           fixture = shared_corpus();
        if (!fixture.agrees) {
            state.SkipWithError("corpus mismatch");
            return;
        }
        std::vector<std::uint8_t> output(count * ulid26::ulid::kBinarySize);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                ulid::MarshalBinaryTo(
                    fixture.ref.at(pos), output.data() + pos * ulid26::ulid::kBinarySize);
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_suyash_unmarshal_binary(benchmark::State& state) {
        std::size_t const     count   = static_cast<std::size_t>(state.range(0));
        corpus const&         fixture = shared_corpus();
        if (!fixture.agrees) {
            state.SkipWithError("corpus mismatch");
            return;
        }
        std::vector<ulid::ULID> output(count);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                ulid::ULID id{};
                ulid::UnmarshalBinaryFrom(
                    fixture.ref_bytes.data() + pos * ulid26::ulid::kBinarySize, id);
                output.at(pos) = id;
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }
#endif

    // Registration globals and the main entry are framework-imposed names;
    // the registrar objects are also allowed to throw at startup.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)
    // Batch-size sweep: L1-resident (1024), L2-resident (10k), LLC-resident (100k).
    BENCHMARK(bench_to_bytes)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_from_bytes)->Args({1024})->Args({10000})->Args({100000});
#ifdef ULID26_HAVE_SUYASH
    BENCHMARK(bench_suyash_marshal_binary)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_suyash_unmarshal_binary)->Args({1024})->Args({10000})->Args({100000});
#endif
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
