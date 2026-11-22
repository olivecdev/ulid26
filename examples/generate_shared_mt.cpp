// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <print>
#include <string_view>
#include <thread>
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>
#include <vector>

namespace {
    constexpr std::size_t kThreads   = 4;
    constexpr std::size_t kPerThread = 256;

    int fail(std::string_view message) {
        std::println("FAIL: {}", message);
        return 1;
    }

    template <typename Produce>
    auto retry_on_regression(Produce&& produce) {
        auto result = produce();
        for (int attempt = 0;
             !result && result.error() == ulid26::make_error_code(ulid26::errc::time_reversed) &&
             attempt < 8;
             ++attempt) {
            result = produce();
        }
        return result;
    }

    struct mint_job {
        ulid26::shared_generator<>& generator;
        std::vector<ulid26::ulid>&  ids;
        std::mutex&                 mutex;
        std::atomic<bool>&          failed;
        std::atomic<bool>&          regressed;
    };

    void mint_loop(mint_job& job) {
        std::vector<ulid26::ulid> local;
        local.reserve(kPerThread);
        for (std::size_t n = 0; n < kPerThread; ++n) {
            auto id = retry_on_regression([&job] { return job.generator.next(); });
            if (!id) {
                if (id.error() == ulid26::make_error_code(ulid26::errc::time_reversed)) {
                    job.regressed.store(true);
                } else {
                    job.failed.store(true);
                }
                return;
            }
            local.push_back(*id);
        }
        std::scoped_lock const guard{job.mutex};
        job.ids.insert(job.ids.end(), local.begin(), local.end());
    }
} // namespace

int main() {
    std::vector<ulid26::ulid> ids;
    ids.reserve(kThreads * kPerThread);
    std::mutex        mutex;
    std::atomic<bool> failed{false};
    std::atomic<bool> regressed{false};

    ulid26::shared_generator<> shared;
    mint_job                   shared_job{shared, ids, mutex, failed, regressed};
    std::thread                first([&] { mint_loop(shared_job); });
    std::thread                second([&] { mint_loop(shared_job); });
    std::thread                third([&] {
        ulid26::shared_generator<> own;
        mint_job                   own_job{own, ids, mutex, failed, regressed};
        mint_loop(own_job);
    });
    std::thread                fourth([&] {
        ulid26::shared_generator<> own;
        mint_job                   own_job{own, ids, mutex, failed, regressed};
        mint_loop(own_job);
    });
    first.join();
    second.join();
    third.join();
    fourth.join();

    if (regressed.load()) {
        std::println("note: wall clock stepped backwards; nothing to prove here");
        return 0;
    }
    if (failed.load()) {
        return fail("shared next failed");
    }
    if (ids.size() != kThreads * kPerThread) {
        return fail("lost ids across threads");
    }
    // Push order is scheduling order, not commit order: sort to prove uniqueness.
    std::ranges::sort(ids);
    for (std::size_t i = 1; i < ids.size(); ++i) {
        if (!(ids[i - 1] < ids[i])) {
            return fail("duplicate or unordered ids");
        }
    }

    std::println("shared MT OK: {} unique ids across {} threads", ids.size(), kThreads);
    return 0;
}
