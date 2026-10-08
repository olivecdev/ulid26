// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// fuzz/fuzz_text.cpp — libFuzzer harness over the text-codec boundary.
// Feeds raw fuzzer bytes at try_parse/is_valid and asserts the two
// invariants the test suite pins: the validity contract
// (is_valid ⟺ try_parse.has_value()) and the round-trip fixed point
// (parse → to_chars → re-parse compares equal). Clang + libFuzzer only;
// see CMakeLists.txt in this directory.

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <ulid26/value.hpp>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    // The codec decides at exactly 26 characters; allow modest overshoot so
    // the invalid_length path is exercised, then bail — wider inputs only
    // re-prove the length check at mutation cost.
    static constexpr std::size_t kMaxProbe = 64;
    if (size > kMaxProbe) {
        return 0;
    }
    std::string_view const raw{reinterpret_cast<char const*>(data), size};

    // Gate 1 — the contract: validity and parseability agree.
    bool const                                      valid  = ulid26::is_valid(raw);
    std::expected<ulid26::ulid, ulid26::errc> const parsed = ulid26::try_parse(raw);
    assert(valid == parsed.has_value());

    // Gate 2 — the round trip: parse → to_chars → re-parse is a fixed point.
    if (parsed.has_value()) {
        std::array<char, ulid26::ulid::kTextSize> buffer{};
        parsed->to_chars(std::span<char, ulid26::ulid::kTextSize>{buffer});
        std::expected<ulid26::ulid, ulid26::errc> const reparsed =
            ulid26::try_parse(std::string_view{buffer.data(), buffer.size()});
        assert(reparsed.has_value() && *reparsed == *parsed);
    }
    return 0;
}
