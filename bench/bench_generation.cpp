// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <ulid26/generators.hpp>

namespace {

    template <typename Generator>
    void bench_next(benchmark::State& state) {
        Generator     generator{};
        std::uint64_t acc = 0;
        for (auto tick : state) {
            auto const id = generator.next();
            if (!id) {
                state.SkipWithError("next failed");
                return;
            }
            acc ^= id->timestamp();
        }
        benchmark::DoNotOptimize(acc);
    }

    void bench_generate(benchmark::State& state) {
        std::uint64_t acc = 0;
        for (auto tick : state) {
            acc ^= ulid26::generate().timestamp();
        }
        benchmark::DoNotOptimize(acc);
    }

    void bench_unsync_next(benchmark::State& state) {
        bench_next<ulid26::unsynchronized_generator<>>(state);
    }

    void bench_shared_next(benchmark::State& state) {
        bench_next<ulid26::shared_generator<>>(state);
    }

    template <typename Generator>
    void bench_reserve(benchmark::State& state) {
        std::size_t const count = static_cast<std::size_t>(state.range(0));
        Generator         generator{};
        std::uint64_t     acc = 0;
        for (auto tick : state) {
            auto const block = generator.reserve(count);
            if (!block || block->size() != count) {
                state.SkipWithError("short reservation");
                return;
            }
            for (auto const id : *block) {
                acc ^= id.timestamp();
            }
        }
        benchmark::DoNotOptimize(acc);
        state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(count));
    }

    void bench_unsync_reserve(benchmark::State& state) {
        bench_reserve<ulid26::unsynchronized_generator<>>(state);
    }

    void bench_shared_reserve(benchmark::State& state) {
        bench_reserve<ulid26::shared_generator<>>(state);
    }

    // NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)
    BENCHMARK(bench_generate);
    BENCHMARK(bench_unsync_next);
    BENCHMARK(bench_shared_next);
    BENCHMARK(bench_unsync_reserve)->Arg(1)->Arg(16)->Arg(64)->Arg(256)->Arg(1024);
    BENCHMARK(bench_shared_reserve)->Arg(1)->Arg(16)->Arg(64)->Arg(256)->Arg(1024);
    // NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables,bugprone-throwing-static-initialization,cert-err58-cpp)

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
BENCHMARK_MAIN();

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
