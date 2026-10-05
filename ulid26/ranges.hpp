#ifndef ULID26_RANGES_HPP
#define ULID26_RANGES_HPP
//
//             ░██ ░██       ░██  ░██████   ░██████
//             ░██           ░██ ░██   ░██ ░██   ░██
//  ░██    ░██ ░██ ░██ ░████████       ░██ ░██
//  ░██    ░██ ░██ ░██░██    ░██   ░█████  ░███████
//  ░██    ░██ ░██ ░██░██    ░██  ░██      ░██   ░██
//  ░██   ░███ ░██ ░██░██   ░███ ░██       ░██   ░██
//   ░█████░██ ░██ ░██ ░█████░██ ░████████  ░██████
//
//
//   SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
//   SPDX-License-Identifier: MIT
//
// ulid26/ranges.hpp - iterator adapter for the generators.
//
// Exposes any monotonic generator as a single-pass std::ranges::input_range
// of ulid values, so generation composes with the standard pipeline:
//
//   for (auto u : ulid26::ranges::values(g) | std::views::take(10))
//       consume(u);
//
// Contention contract (read before sharing a generator across threads):
//   - A view borrows its generator (it holds a pointer) and drains it one
//     next() per increment. Views are single-pass: copying a view or an
//     iterator shares the underlying sequence, never forks it — the
//     istream_view shape, for the same reason (the sequence lives elsewhere).
//   - Pinned thread: view over monotonic_local. Zero synchronization.
//   - Migrating thread: view over monotonic_shared. Correct everywhere.
//   - Concurrent multi-threaded draining of one shared generator is CORRECT
//     (every increment is a single atomic commit; global order is preserved)
//     but contended: one CAS per id. High-ingestion pipelines should prefer
//     next(count) reservations — one commit per block — and drain each block
//     locally instead.
//   - take(n) over a view pulls n+1 times in a range-for (the trailing pull
//     is discarded by take, but the generator still advances): harmless — no
//     id is ever lost or duplicated — yet last() will reflect the extra pull.
//     The istream_view shape behaves identically, for the same reason.
//   - The range is infinite until failure: on error iteration ends and the
//     code is parked in the generator, retrievable via last_error().
//     Always check it after a loop that can end on its own.
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>

#endif // ULID26_RANGES_HPP
