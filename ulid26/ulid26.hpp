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
// ulid26/ulid26.hpp — the include-all umbrella: the value type and the
// generation layer. Prefer the precise header per translation unit instead:
// - <ulid26/value.hpp> when only naming, parsing or formatting ids;
// - <ulid26/generators.hpp> for generation.
// Include this header when the TU genuinely needs both layers.
#include <ulid26/generators.hpp>
#include <ulid26/value.hpp>
#endif // ULID26_HPP
