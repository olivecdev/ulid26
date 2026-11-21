// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Throughput benchmarks for the generation layer: one-shot generate(), the
// two monotonic next() flavors, and reserve()+drain at several block sizes.
// Same harness and methodology as the sibling benchmarks (items-processed
// reporting). Run a Release build (e.g. the bench preset); Debug numbers are
// meaningless.
//
// Every timed loop consumes the produced value's timestamp so the compiler
// cannot drop the generation work. The reserve benchmarks include draining
// the returned range: the reservation itself is one commit, while iteration
// is local arithmetic.
//
// The BENCHMARK registrations and BENCHMARK_MAIN are framework-imposed names,
// exempted inline below.

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

    // Registration globals and the main entry are framework-imposed names;
    // the registrar objects are also allowed to throw at startup.
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
