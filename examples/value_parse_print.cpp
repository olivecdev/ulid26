// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT

#include <array>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>

namespace {
    int fail(std::string_view message) {
        std::println("FAIL: {}", message);
        return 1;
    }
} // namespace

int main() {
    constexpr std::string_view text = "01ARZ3NDEKTSV4RRFFQ69G5FAV";
    if (!ulid26::is_valid(text)) {
        return fail("spec vector rejected");
    }
    if (ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FA*")) {
        return fail("invalid character accepted");
    }
    if (ulid26::is_valid("80000000000000000000000000")) {
        return fail("overflow accepted");
    }

    auto const parsed = ulid26::try_parse(text);
    if (!parsed) {
        return fail("try_parse failed on spec vector");
    }
    if (parsed->timestamp() != 1469922850259ULL) {
        return fail("timestamp mismatch");
    }

    auto const owned = ulid26::ulid::from_string(std::string{text});
    if (!owned) {
        return fail("from_string failed on spec vector");
    }
    if (*owned != *parsed) {
        return fail("from_string disagrees with try_parse");
    }
    auto const lower = ulid26::ulid::from_string("01arz3ndektsv4rrffq69g5fav");
    if (!lower || *lower != *parsed) {
        return fail("lowercase input rejected or mismatched");
    }

    std::string const canonical = parsed->to_string();
    if (canonical != text) {
        return fail("to_string is not canonical");
    }
    if (std::format("{}", *parsed) != canonical) {
        return fail("formatter disagrees with to_string");
    }
    std::array<char, ulid26::ulid::kTextSize> out{};
    parsed->to_chars(std::span{out});
    if (std::string_view{out.data(), out.size()} != text) {
        return fail("to_chars disagrees with to_string");
    }

    std::array<std::byte, ulid26::ulid::kBinarySize> raw{};
    parsed->to_bytes(std::span{raw});
    if (ulid26::ulid::from_bytes(std::span{raw}) != *parsed) {
        return fail("bytes round-trip mismatch");
    }

    if (ulid26::min() >= ulid26::max()) {
        return fail("min/max ordering broken");
    }
    if (*parsed <= ulid26::min()) {
        return fail("spec vector should exceed nil");
    }

    std::println("value OK: {}", canonical);
    return 0;
}
