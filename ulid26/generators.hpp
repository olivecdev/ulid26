#ifndef ULID26_GENERATORS_HPP
#define ULID26_GENERATORS_HPP
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
// ulid26/generators.hpp — entropy engines, one-shot generation, both monotonic flavors and bulk
// reservations.
//
// The generation layer over <ulid26/value.hpp>: anything that mints ids
// lives here. Pulls <mutex>, <atomic>, <random> and <chrono>; translation
// units that only name, parse or format ids include <ulid26/value.hpp>
// directly to keep their compile times down. Engines are standard
// uniform random bit generators (std::mt19937_64 plugs in with zero glue);
// the two in-house engines are default_rng (ChaCha20, seeded once per
// thread from the OS) and system_rng (std::random_device per draw).
#include <ulid26/value.hpp>
#endif // ULID26_GENERATORS_HPP
