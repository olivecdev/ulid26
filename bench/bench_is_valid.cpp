// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Throughput benchmarks for ulid26::is_valid and ulid26::try_parse, plus a
// head-to-head try_parse vs suyash/ulid::UnmarshalFrom (unchecked decode).
// Same harness and methodology as the sibling benchmarks (deterministic
// corpus, batch-size sweep, items-processed reporting). Run a Release build
// (e.g. the ci preset); Debug numbers are meaningless. The scalar try_parse
// loop is the counterpart of the reference bench_decode_scalar: identical
// shape, same corpus.
//
// The corpus below is literal by nature, so magic-number warnings are
// suspended file-wide, as in the test corpus. The BENCHMARK registrations
// and BENCHMARK_MAIN are framework-imposed names, exempted inline below.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>
#include <vector>

#ifdef ULID26_HAVE_SUYASH
#include <ulid.hh>
#endif

namespace {

    constexpr std::size_t kMaxCount = 100000;

    // Deterministic corpus: eight known-valid ULIDs (nil, max, the spec
    // example and case/shape variants) cycled to fill the text buffer.
    // Every entry is a valid ULID by construction, so the benchmarks
    // measure the accept path, exactly like the reference harness.
    constexpr std::array<std::string_view, 8> kSeeds = {
        "00000000000000000000000000", "7ZZZZZZZZZZZZZZZZZZZZZZZZZ", "01ARZ3NDEKTSV4RRFFQ69G5FAV",
        "01arz3ndektsv4rrffq69g5fav", "0123456789ABCDEFGHJKMNPQRS", "10ARZ3NDEKTSV4RRFFQ69G5FAV",
        "7HZZZZZZZZZZZZZZZZZZZZZZZZ", "00000000000000000000000001",
    };

    // Corpus guard: every seed must be exactly one ULID long, so the
    // benchmarks always measure the accept path (a wrong-sized seed would
    // silently benchmark the early reject instead).
    constexpr bool all_seeds_sized() {
        for (std::string_view seed : kSeeds) {
            if (seed.size() != ulid26::ulid::kTextSize) {
                return false;
            }
        }
        return true;
    }

    static_assert(all_seeds_sized(), "every corpus seed is a 26-character ULID");

#ifdef ULID26_HAVE_SUYASH
    // Suyash decodes uppercase only, so its corpus uses the uppercased seeds.
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
            text.reserve(kMaxCount * ulid26::ulid::kTextSize);
#ifdef ULID26_HAVE_SUYASH
            upper_text.reserve(kMaxCount * ulid26::ulid::kTextSize);
#endif
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                text.append(kSeeds.at(pos % kSeeds.size()));
#ifdef ULID26_HAVE_SUYASH
                upper_text.append(upper_copy(kSeeds.at(pos % kSeeds.size())));
#endif
            }
        }

        std::string text{};
#ifdef ULID26_HAVE_SUYASH
        std::string upper_text{};
#endif
    };

    corpus const& shared_corpus() {
        static corpus const instance{};
        return instance;
    }

    void bench_is_valid(benchmark::State& state) {
        std::size_t const      count   = static_cast<std::size_t>(state.range(0));
        corpus const&          fixture = shared_corpus();
        std::string_view const input{fixture.text};
        // Loop accumulator by design; DoNotOptimize reads the final sum.
        // NOLINTNEXTLINE(misc-const-correctness)
        std::size_t            accepted = 0;
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                accepted += ulid26::is_valid(
                                input.substr(pos * ulid26::ulid::kTextSize, ulid26::ulid::kTextSize)
                            ) ?
                                1U :
                                0U;
            }
            benchmark::DoNotOptimize(accepted);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_try_parse(benchmark::State& state) {
        std::size_t const         count   = static_cast<std::size_t>(state.range(0));
        corpus const&             fixture = shared_corpus();
        std::vector<ulid26::ulid> output{};
        output.reserve(count);
        std::string_view const input{fixture.text};
        for (auto tick : state) {
            output.clear();
            for (std::size_t pos = 0; pos < count; ++pos) {
                auto const part = ulid26::try_parse(
                    input.substr(pos * ulid26::ulid::kTextSize, ulid26::ulid::kTextSize)
                );
                if (!part) {
                    state.SkipWithError("bad corpus");
                    return;
                }
                output.push_back(*part);
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

#ifdef ULID26_HAVE_SUYASH
    void bench_suyash_decode(benchmark::State& state) {
        std::size_t const       count   = static_cast<std::size_t>(state.range(0));
        corpus const&           fixture = shared_corpus();
        std::vector<ulid::ULID> output(count);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                ulid::UnmarshalFrom(
                    fixture.upper_text.data() + pos * ulid26::ulid::kTextSize, output.at(pos)
                );
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
    BENCHMARK(bench_is_valid)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_try_parse)->Args({1024})->Args({10000})->Args({100000});
#ifdef ULID26_HAVE_SUYASH
    BENCHMARK(bench_suyash_decode)->Args({1024})->Args({10000})->Args({100000});
#endif
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
