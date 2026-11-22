// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
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

    constexpr std::array<std::string_view, 8> kSeeds = {
        "00000000000000000000000000", "7ZZZZZZZZZZZZZZZZZZZZZZZZZ", "01ARZ3NDEKTSV4RRFFQ69G5FAV",
        "01arz3ndektsv4rrffq69g5fav", "0123456789ABCDEFGHJKMNPQRS", "10ARZ3NDEKTSV4RRFFQ69G5FAV",
        "7HZZZZZZZZZZZZZZZZZZZZZZZZ", "00000000000000000000000001",
    };

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
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                ids.push_back(ulid26::try_parse(kSeeds.at(pos % kSeeds.size())).value());
            }
#ifdef ULID26_HAVE_SUYASH
            ref.reserve(kMaxCount);
            for (std::size_t pos = 0; pos < kMaxCount; ++pos) {
                ref.push_back(ulid::Unmarshal(upper_copy(kSeeds.at(pos % kSeeds.size()))));
            }
            agrees = true;
            for (std::string_view seed : kSeeds) {
                std::array<char, 26> ours_text{};
                ulid26::try_parse(seed).value().to_chars(std::span<char, 26>{ours_text});
                std::array<char, 26> ref_text{};
                ulid::MarshalTo(ulid::Unmarshal(upper_copy(seed)), ref_text.data());
                agrees = agrees && (std::string_view{ours_text.data(), ours_text.size()} ==
                                    std::string_view{ref_text.data(), ref_text.size()});
            }
#endif
        }

        std::vector<ulid26::ulid> ids{};
#ifdef ULID26_HAVE_SUYASH
        std::vector<ulid::ULID> ref{};
        bool                    agrees = true;
#endif
    };

    corpus const& shared_corpus() {
        static corpus const instance{};
        return instance;
    }

    void bench_to_chars(benchmark::State& state) {
        std::size_t const count   = static_cast<std::size_t>(state.range(0));
        corpus const&     fixture = shared_corpus();
        std::string       text(count * ulid26::ulid::kTextSize, '\0');
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                fixture.ids.at(pos).to_chars(
                    std::span<char, ulid26::ulid::kTextSize>{
                        text.data() + pos * ulid26::ulid::kTextSize, ulid26::ulid::kTextSize
                    }
                );
            }
            benchmark::DoNotOptimize(text);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_ours_strings(benchmark::State& state) {
        std::size_t const        count   = static_cast<std::size_t>(state.range(0));
        corpus const&            fixture = shared_corpus();
        std::vector<std::string> output(count);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                output.at(pos) = fixture.ids.at(pos).to_string();
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

#ifdef ULID26_HAVE_SUYASH
    void bench_suyash_blocks(benchmark::State& state) {
        std::size_t const count   = static_cast<std::size_t>(state.range(0));
        corpus const&     fixture = shared_corpus();
        if (!fixture.agrees) {
            state.SkipWithError("corpus mismatch");
            return;
        }
        std::string text(count * ulid26::ulid::kTextSize, '\0');
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                ulid::MarshalTo(fixture.ref.at(pos), text.data() + pos * ulid26::ulid::kTextSize);
            }
            benchmark::DoNotOptimize(text);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_suyash_strings(benchmark::State& state) {
        std::size_t const count   = static_cast<std::size_t>(state.range(0));
        corpus const&     fixture = shared_corpus();
        if (!fixture.agrees) {
            state.SkipWithError("corpus mismatch");
            return;
        }
        std::vector<std::string> output(count);
        for (auto tick : state) {
            for (std::size_t pos = 0; pos < count; ++pos) {
                output.at(pos) = ulid::Marshal(fixture.ref.at(pos));
            }
            benchmark::DoNotOptimize(output);
        }
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }
#endif

    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)
    BENCHMARK(bench_to_chars)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_ours_strings)->Args({1024})->Args({10000})->Args({100000});
#ifdef ULID26_HAVE_SUYASH
    BENCHMARK(bench_suyash_blocks)->Args({1024})->Args({10000})->Args({100000});
    BENCHMARK(bench_suyash_strings)->Args({1024})->Args({10000})->Args({100000});
#endif
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
