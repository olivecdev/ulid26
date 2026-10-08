// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Throughput benchmarks for the ulid26 stream operators: same harness and
// methodology as the sibling benchmarks (deterministic corpus, batch-size
// sweep, items-processed reporting). Run a Release build (e.g. the ci
// preset); Debug numbers are meaningless. Output inserts into an exact-fit
// buffered sink (no streambuf growth on the timed path); input extracts
// from a zero-copy memory buffer over the corpus text (no string copies).
// A stream object is built per repetition and amortized over the batch.
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
#include <istream>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>
#include <vector>

namespace {

    constexpr std::size_t kMaxCount = 100000;

    // Deterministic corpus: eight known-valid ULIDs (nil, max, the spec
    // example and case/shape variants) cycled to fill the id buffer.
    constexpr std::array<std::string_view, 8> kSeeds = {
        "00000000000000000000000000", "7ZZZZZZZZZZZZZZZZZZZZZZZZZ", "01ARZ3NDEKTSV4RRFFQ69G5FAV",
        "01arz3ndektsv4rrffq69g5fav", "0123456789ABCDEFGHJKMNPQRS", "10ARZ3NDEKTSV4RRFFQ69G5FAV",
        "7HZZZZZZZZZZZZZZZZZZZZZZZZ", "00000000000000000000000001",
    };

    // Corpus guards: every seed is exactly one ULID long and parses, so
    // the benchmarks measure the stream path on pinned values (a bad seed
    // would silently benchmark the failure path instead).
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

    // Output sink with an exact-fit put area: sized for the batch, so the
    // timed path never grows (no overflow, no allocation).
    struct buffered_sink : std::streambuf {
        explicit buffered_sink(std::size_t size) : buffer_(size) { reset(); }

        void reset() { setp(buffer_.data(), buffer_.data() + buffer_.size()); }

        ~buffered_sink() override = default;

        std::vector<char> buffer_{};
    };

    // Zero-copy input over existing storage: the get area aliases the corpus
    // text (setg takes mutable pointers but extraction never writes through
    // them).
    struct memory_source : std::streambuf {
        memory_source(char const* begin, char const* end) {
            char* base = const_cast<char*>(begin);
            setg(base, base, base + (end - begin));
        }

        ~memory_source() override = default;
    };

    struct corpus {
        corpus() {
            ids.reserve(kMaxCount);
            text.reserve(kMaxCount * ulid26::ulid::kTextSize);
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                // Pinned valid above; .value() never throws here.
                ids.push_back(ulid26::try_parse(kSeeds.at(pos % kSeeds.size())).value());
                text.append(kSeeds.at(pos % kSeeds.size()));
            }
        }

        std::vector<ulid26::ulid> ids{};
        std::string               text{};
    };

    corpus const& shared_corpus() {
        static corpus const instance{};
        return instance;
    }

    void bench_stream_out(benchmark::State& state) {
        std::size_t const count   = static_cast<std::size_t>(state.range(0));
        corpus const&     fixture = shared_corpus();
        // Loop accumulator by design; DoNotOptimize reads the final sum, so
        // every repetition's stores stay observable.
        // NOLINTNEXTLINE(misc-const-correctness)
        std::uint64_t     total   = 0;
        for (auto tick : state) {
            buffered_sink sink(count * ulid26::ulid::kTextSize);
            std::ostream  out(&sink);
            for (std::size_t pos = 0; pos < count; ++pos) {
                out << fixture.ids.at(pos);
            }
            for (char unit : sink.buffer_) {
                total += static_cast<std::uint64_t>(static_cast<std::uint8_t>(unit));
            }
            benchmark::DoNotOptimize(total);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_stream_in(benchmark::State& state) {
        std::size_t const         count   = static_cast<std::size_t>(state.range(0));
        corpus const&             fixture = shared_corpus();
        std::vector<ulid26::ulid> output(count);
        for (auto tick : state) {
            memory_source source(
                fixture.text.data(), fixture.text.data() + count * ulid26::ulid::kTextSize
            );
            std::istream in(&source);
            for (std::size_t pos = 0; pos < count; ++pos) {
                in >> output.at(pos);
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    // Registration globals and the main entry are framework-imposed names;
    // the registrar objects are also allowed to throw at startup.
    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)
    // Batch-size sweep: L1-resident (1024), L2-resident (10k), LLC-resident (100k).
    BENCHMARK(bench_stream_out)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_stream_in)->Args({1024})->Args({10000})->Args({100000});
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
