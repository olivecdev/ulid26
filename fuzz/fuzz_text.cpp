// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <ulid26/value.hpp>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    static constexpr std::size_t kMaxProbe = 64;
    if (size > kMaxProbe) {
        return 0;
    }
    std::string_view const raw{reinterpret_cast<char const*>(data), size};

    bool const                                      valid  = ulid26::is_valid(raw);
    std::expected<ulid26::ulid, ulid26::errc> const parsed = ulid26::try_parse(raw);
    assert(valid == parsed.has_value());

    if (parsed.has_value()) {
        std::array<char, ulid26::ulid::kTextSize> buffer{};
        parsed->to_chars(std::span<char, ulid26::ulid::kTextSize>{buffer});
        std::expected<ulid26::ulid, ulid26::errc> const reparsed =
            ulid26::try_parse(std::string_view{buffer.data(), buffer.size()});
        assert(reparsed.has_value() && *reparsed == *parsed);
    }
    return 0;
}
