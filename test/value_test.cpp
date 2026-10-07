// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Unit tests for ulid26::is_valid (ulid26/value.hpp) with boost-ext/ut.
// Every vector below mirrors the is_valid coverage of the reference
// worktree's differential suite: known vectors, every character width,
// malformed positions, the first-character cap and length mismatches,
// plus the constexpr proofs as static_asserts.
//
// The corpus is literal by nature, so magic-number warnings are suspended
// file-wide. ut itself is a third-party header: it arrives via SYSTEM
// includes (see test/CMakeLists.txt), so our -Werror set never fires
// inside it.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <string_view>
#include <ulid26/value.hpp>

namespace {

    constexpr std::string_view kExample = "01ARZ3NDEKTSV4RRFFQ69G5FAV";
    constexpr std::string_view kMax     = "7ZZZZZZZZZZZZZZZZZZZZZZZZZ";
    constexpr std::string_view kNil     = "00000000000000000000000000";

    static_assert(ulid26::is_valid(kExample));
    static_assert(ulid26::is_valid(kMax));
    static_assert(ulid26::is_valid(kNil));
    static_assert(!ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FA*"));
    static_assert(!ulid26::is_valid("80000000000000000000000000"));
    static_assert(!ulid26::is_valid("short"));

    // Copies a string_view into a fixed array (test-side staging only).
    template <std::size_t N>
    std::array<char, N> to_array(std::string_view text) {
        std::array<char, N> out{};
        for (std::size_t index = 0; index < N; ++index) {
            out.at(index) = text.at(index);
        }
        return out;
    }

} // namespace

int main() {
    using namespace boost::ut;

    "known vectors"_test = [] {
        expect(ulid26::is_valid(kExample)) << "example is valid";
        expect(ulid26::is_valid(kMax)) << "max is valid";
        expect(ulid26::is_valid(kNil)) << "nil is valid";
        expect(ulid26::is_valid("01arz3ndektsv4rrffq69g5fav")) << "lowercase is valid";
    };

    "character widths"_test = [] {
        expect(ulid26::is_valid(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAV"}))
            << "is_valid<wchar_t> accepts";
        expect(!ulid26::is_valid(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAV\u00E9"}))
            << "is_valid<wchar_t> rejects non-ASCII";
        // Every character type validates through the same narrow anchor:
        // ASCII but non-alphabet units still reject (not just non-ASCII).
        expect(ulid26::is_valid(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FAV"}))
            << "is_valid<char8_t> accepts";
        expect(!ulid26::is_valid(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FA*"}))
            << "is_valid<char8_t> rejects non-alphabet";
        expect(ulid26::is_valid(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAV"}))
            << "is_valid<char16_t> accepts";
        expect(!ulid26::is_valid(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FA!"}))
            << "is_valid<char16_t> rejects non-alphabet";
        expect(!ulid26::is_valid(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAV\u00E9"}))
            << "is_valid<char16_t> rejects non-ASCII";
        expect(ulid26::is_valid(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FAV"}))
            << "is_valid<char32_t> accepts";
        expect(!ulid26::is_valid(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FA*"}))
            << "is_valid<char32_t> rejects non-alphabet";
    };

    "malformed positions"_test = [] {
        constexpr std::array<char, 13> kBad = {
            '*',
            'I',
            'i',
            'L',
            'l',
            'O',
            'o',
            'U',
            'u',
            '\x7F',
            '\x10',
            static_cast<char>(0x80),
            static_cast<char>(0xFF),
        };
        bool all_rejected = true;
        for (std::size_t pos = 0; pos < 26; ++pos) {
            for (char const bad : kBad) {
                auto single    = to_array<26>(kExample);
                single.at(pos) = bad;
                all_rejected   = all_rejected &&
                                 !ulid26::is_valid(std::string_view{single.data(), single.size()});
            }
        }
        expect(all_rejected) << "every position rejects every bad byte";
    };

    "first character cap"_test = [] {
        constexpr std::array<char, 6> kOver  = {'8', '9', 'A', 'B', 'G', 'Z'};
        bool                          capped = true;
        for (char const first : kOver) {
            auto single  = to_array<26>(kExample);
            single.at(0) = first;
            capped = capped && !ulid26::is_valid(std::string_view{single.data(), single.size()});
        }
        capped = capped && ulid26::is_valid(kMax);
        expect(capped) << "first-char cap enforced";
    };

    "lengths"_test = [] {
        expect(!ulid26::is_valid("")) << "empty text is invalid";
        expect(!ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FA")) << "25 chars are invalid";
        expect(!ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FAVA")) << "27 chars are invalid";
    };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
