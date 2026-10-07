// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Throughput benchmark for ulid26::is_valid: same harness and methodology
// as the reference worktree (deterministic corpus, batch-size sweep,
// items-processed reporting). Run a Release build (e.g. the ci preset);
// Debug numbers are meaningless. Only the validator exists in this tree so
// far, so this covers the is_valid accept loop; scalar/batch comparisons
// arrive with those APIs.
//
// The corpus below is literal by nature, so magic-number warnings are
// suspended file-wide, as in the test corpus. The BENCHMARK registration
// and BENCHMARK_MAIN are framework-imposed names, exempted inline below.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>

namespace {

    constexpr std::size_t kMaxCount = 100000;

    // Deterministic corpus: eight known-valid ULIDs (nil, max, the spec
    // example and case/shape variants) cycled to fill the text buffer.
    // Every entry is a valid ULID by construction, so the benchmark
    // measures the accept path, exactly like the reference harness.
    constexpr std::array<std::string_view, 8> kSeeds = {
        "00000000000000000000000000",  "7ZZZZZZZZZZZZZZZZZZZZZZZZZ",  "01ARZ3NDEKTSV4RRFFQ69G5FAV",
        "01arz3ndektsv4rrffq69g5fav",  "0123456789ABCDEFGHJKMNPQRST", "10ARZ3NDEKTSV4RRFFQ69G5FAV",
        "7HZZZZZZZZZZZZZZZZZZZZZZZZZ", "00000000000000000000000001",
    };

    struct corpus {
        corpus() {
            text.reserve(kMaxCount * ulid26::detail::kTextSize);
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                text.append(kSeeds.at(pos % kSeeds.size()));
            }
        }

        std::string text{};
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
                accepted +=
                    ulid26::is_valid(
                        input.substr(pos * ulid26::detail::kTextSize, ulid26::detail::kTextSize)
                    ) ?
                        1U :
                        0U;
            }
            benchmark::DoNotOptimize(accepted);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    // Registration globals and the main entry are framework-imposed names;
    // the registrar objects are also allowed to throw at startup.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)
    // Batch-size sweep: L1-resident (1024), L2-resident (10k), LLC-resident (100k).
    BENCHMARK(bench_is_valid)->Args({1024})->Args({10000})->Args({100000});
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
