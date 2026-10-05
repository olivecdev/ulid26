#ifndef ULID26_HPP
#define ULID26_HPP
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

// ulid26/ulid26.hpp — the include-all umbrella: value, generators, batch text API and the ranges
// adapter. Prefer the precise header per translation unit instead:
// - <ulid26/value.hpp> when only naming, parsing or formatting ids;
// - <ulid26/generators.hpp> for generation;
// - <ulid26/batch.hpp> for bulk text;
// - <ulid26/ranges.hpp> for pipelines.
// Each layer from above includes all it needs from the previous layers.
// Include this header when the TU genuinely needs several layers.
#include <ulid26/batch.hpp>
#include <ulid26/generators.hpp>
#include <ulid26/ranges.hpp>
#include <ulid26/value.hpp>
#endif // ULID26_HPP
