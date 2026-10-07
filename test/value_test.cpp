// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Unit tests for ulid26::is_valid and ulid26::try_parse (ulid26/value.hpp)
// with boost-ext/ut. Every vector below mirrors the coverage of the
// reference worktree's suites: known vectors, every character width,
// malformed positions, the first-character cap, length mismatches and
// parse/is_valid agreement, plus the constexpr proofs as static_asserts.
//
// The corpus is literal by nature, so magic-number warnings are suspended
// file-wide. ut itself is a third-party header: it arrives via SYSTEM
// includes (see test/CMakeLists.txt), so our -Werror set never fires
// inside it.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
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

    constexpr std::uint64_t kExampleTimestamp = 1469922850259ULL;
    constexpr std::uint64_t kMaxTimestamp     = 281474976710655ULL; // 2^48 - 1

    // Reference vector pinned through the timestamp: the time field of the
    // spec's example id, plus the all-maximum boundary.
    static_assert(ulid26::try_parse(kExample)->timestamp() == kExampleTimestamp);
    static_assert(ulid26::try_parse(kMax)->timestamp() == kMaxTimestamp);
    // Decoding is case-insensitive and boundary-exact.
    static_assert(*ulid26::try_parse("01arz3ndektsv4rrffq69g5fav") == *ulid26::try_parse(kExample));
    static_assert(*ulid26::try_parse(kMax) == ulid26::ulid::max());
    static_assert(*ulid26::try_parse(kNil) == ulid26::ulid::min());
    static_assert(ulid26::ulid::min() < ulid26::ulid::max());
    // Malformed input is rejected with the precise error, including the
    // precedence case: an invalid character outranks overflow.
    static_assert(
        ulid26::try_parse("80000000000000000000000000").error() == ulid26::errc::overflow
    );
    static_assert(
        ulid26::try_parse("01ARZ3NDEKTSV4RRFFQ69G5FA*").error() == ulid26::errc::invalid_character
    );
    static_assert(
        ulid26::try_parse("81ARZ3NDEKTSV4RRFFQ69G5FA*").error() == ulid26::errc::invalid_character
    );
    static_assert(ulid26::try_parse("short").error() == ulid26::errc::invalid_length);
    static_assert(
        ulid26::try_parse("01ARZ3NDEKTSV4RRFFQ69G5FAVA").error() == ulid26::errc::invalid_length
    );
    // Overlong wide input must report length, not narrow the valid prefix.
    static_assert(
        ulid26::try_parse(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAVV"}).error() ==
        ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FAVV"}).error() ==
        ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAVV"}).error() ==
        ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FAVV"}).error() ==
        ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse("IIIIIIIIIIIIIIIIIIIIIIIIII").error() == ulid26::errc::invalid_character
    );
    static_assert(
        ulid26::try_parse("LLLLLLLLLLLLLLLLLLLLLLLLLL").error() == ulid26::errc::invalid_character
    );
    // Every character type parses through the same narrow anchor, still at
    // compile time.
    static_assert(
        ulid26::try_parse(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAV"})->timestamp() ==
        kExampleTimestamp
    );
    static_assert(
        ulid26::try_parse(std::wstring_view{L"short"}).error() == ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FAV"})->timestamp() ==
        kExampleTimestamp
    );
    static_assert(
        ulid26::try_parse(std::u8string_view{u8"short"}).error() == ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAV"})->timestamp() ==
        kExampleTimestamp
    );
    static_assert(
        ulid26::try_parse(std::u16string_view{u"short"}).error() == ulid26::errc::invalid_length
    );
    static_assert(
        ulid26::try_parse(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FAV"})->timestamp() ==
        kExampleTimestamp
    );
    static_assert(
        ulid26::try_parse(std::u32string_view{U"short"}).error() == ulid26::errc::invalid_length
    );

    // Copies a string_view into a fixed array (test-side staging only).
    template <std::size_t N>
    std::array<char, N> to_array(std::string_view text) {
        std::array<char, N> out{};
        for (std::size_t index = 0; index < N; ++index) {
            out.at(index) = text.at(index);
        }
        return out;
    }

    // Splitmix-style deterministic byte source: the soup corpus is identical
    // on every run and every platform.
    struct deterministic_rng {
        explicit deterministic_rng(std::uint64_t seed) : state_(seed) {}

        [[nodiscard]]
        std::uint8_t next_byte() {
            state_ = (state_ * 6364136223846793005ULL) + 1442695040888963407ULL;
            return static_cast<std::uint8_t>(state_ >> 56U);
        }

      private:
        std::uint64_t state_;
    };

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
        expect(!ulid26::is_valid(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FA\u00E9"}))
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
        expect(!ulid26::is_valid(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FA\u00E9"}))
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
        expect(!ulid26::is_valid(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAVV"}))
            << "27 wide chars are invalid";
    };

    "try_parse values"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        auto const max     = ulid26::try_parse(kMax);
        auto const nil     = ulid26::try_parse(kNil);
        bool const parsed  = example.has_value() && max.has_value() && nil.has_value();
        expect(parsed) << "vectors parse";
        if (parsed) {
            expect(example->timestamp() == kExampleTimestamp) << "example timestamp";
            expect(max->timestamp() == kMaxTimestamp) << "max timestamp";
            expect(*max == ulid26::ulid::max()) << "max parses to ulid::max";
            expect(nil->timestamp() == 0) << "nil timestamp";
            expect(*nil == ulid26::ulid::min()) << "nil parses to ulid::min";
            expect(*nil < *example) << "nil sorts first";
            expect(*example < *max) << "example sorts before max";
            expect(ulid26::ulid::min() < ulid26::ulid::max()) << "min sorts before max";
        }
        auto const lowered = ulid26::try_parse("01arz3ndektsv4rrffq69g5fav");
        if (parsed && lowered.has_value()) {
            expect(*lowered == *example) << "lowercase parses equal";
        }
        expect(parsed && lowered.has_value()) << "lowercase parses";
    };

    "try_parse errors"_test = [] {
        auto const overflow = ulid26::try_parse("80000000000000000000000000");
        expect(!overflow.has_value() && overflow.error() == ulid26::errc::overflow)
            << "overflow reports";
        auto const bad_char = ulid26::try_parse("01ARZ3NDEKTSV4RRFFQ69G5FA*");
        expect(!bad_char.has_value() && bad_char.error() == ulid26::errc::invalid_character)
            << "invalid character reports";
        auto const both_bad = ulid26::try_parse("81ARZ3NDEKTSV4RRFFQ69G5FA*");
        expect(!both_bad.has_value() && both_bad.error() == ulid26::errc::invalid_character)
            << "invalid character outranks overflow";
        auto const bad_length = ulid26::try_parse("too short");
        expect(!bad_length.has_value() && bad_length.error() == ulid26::errc::invalid_length)
            << "short length reports";
        // Every position rejects every bad byte with the character error.
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
        bool precise = true;
        for (std::size_t pos = 0; pos < 26; ++pos) {
            for (char const bad : kBad) {
                auto single    = to_array<26>(kExample);
                single.at(pos) = bad;
                auto const result =
                    ulid26::try_parse(std::string_view{single.data(), single.size()});
                precise = precise && !result.has_value() &&
                          result.error() == ulid26::errc::invalid_character;
            }
        }
        expect(precise) << "every position reports invalid_character";
        // Overflow first characters report overflow, and max stays valid.
        constexpr std::array<char, 6> kOver  = {'8', '9', 'A', 'B', 'G', 'Z'};
        bool                          capped = true;
        for (char const first : kOver) {
            auto single       = to_array<26>(kExample);
            single.at(0)      = first;
            auto const result = ulid26::try_parse(std::string_view{single.data(), single.size()});
            capped = capped && !result.has_value() && result.error() == ulid26::errc::overflow;
        }
        auto const max = ulid26::try_parse(kMax);
        capped         = capped && max.has_value() && *max == ulid26::ulid::max();
        expect(capped) << "first-char cap reports overflow";
    };

    "try_parse wide"_test = [] {
        auto const from_wide = ulid26::try_parse(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAV"});
        auto const from_u8  = ulid26::try_parse(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FAV"});
        auto const from_u16 = ulid26::try_parse(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAV"});
        auto const from_u32 = ulid26::try_parse(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FAV"});
        bool const accepted = from_wide.has_value() && from_u8.has_value() &&
                              from_u16.has_value() && from_u32.has_value();
        expect(accepted) << "wide texts parse";
        if (accepted) {
            expect(from_wide->timestamp() == kExampleTimestamp) << "wchar_t timestamp";
            expect(from_u8->timestamp() == kExampleTimestamp) << "char8_t timestamp";
            expect(from_u16->timestamp() == kExampleTimestamp) << "char16_t timestamp";
            expect(from_u32->timestamp() == kExampleTimestamp) << "char32_t timestamp";
        }
        auto const short_wide = ulid26::try_parse(std::wstring_view{L"short"});
        expect(!short_wide.has_value() && short_wide.error() == ulid26::errc::invalid_length)
            << "wchar_t rejects short input";
        auto const bad_wide =
            ulid26::try_parse(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FA\u00E9"});
        expect(!bad_wide.has_value() && bad_wide.error() == ulid26::errc::invalid_character)
            << "wchar_t rejects non-ASCII";
        auto const long_wide = ulid26::try_parse(std::wstring_view{L"01ARZ3NDEKTSV4RRFFQ69G5FAVV"});
        auto const long_u8 = ulid26::try_parse(std::u8string_view{u8"01ARZ3NDEKTSV4RRFFQ69G5FAVV"});
        auto const long_u16 =
            ulid26::try_parse(std::u16string_view{u"01ARZ3NDEKTSV4RRFFQ69G5FAVV"});
        auto const long_u32 =
            ulid26::try_parse(std::u32string_view{U"01ARZ3NDEKTSV4RRFFQ69G5FAVV"});
        expect(!long_wide.has_value() && long_wide.error() == ulid26::errc::invalid_length)
            << "wchar_t rejects overlong input";
        expect(!long_u8.has_value() && long_u8.error() == ulid26::errc::invalid_length)
            << "char8_t rejects overlong input";
        expect(!long_u16.has_value() && long_u16.error() == ulid26::errc::invalid_length)
            << "char16_t rejects overlong input";
        expect(!long_u32.has_value() && long_u32.error() == ulid26::errc::invalid_length)
            << "char32_t rejects overlong input";
    };

    "parse agrees with is_valid"_test = [] {
        // Differential soup: random 26-byte strings over the full byte
        // range (half) and over the Crockford alphabet (half, so valid
        // ULIDs appear too). Validator and parser must agree on validity.
        constexpr std::string_view kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        constexpr std::size_t      kCases    = 2048;
        deterministic_rng          rng{0x9E3779B97F4A7C15ULL};
        bool                       agreed = true;
        for (std::size_t trial = 0; trial < kCases && agreed; ++trial) {
            bool const alphabet_biased = (static_cast<unsigned>(rng.next_byte()) & 1U) == 0U;
            std::array<char, 26> soup{};
            for (char& slot : soup) {
                std::uint8_t const byte = rng.next_byte();
                if (alphabet_biased) {
                    slot = kAlphabet.at(static_cast<std::size_t>(byte) & 31U);
                } else {
                    slot = static_cast<char>(byte);
                }
            }
            std::string_view const view{soup.data(), soup.size()};
            auto const             parsed = ulid26::try_parse(view);
            agreed = agreed && (ulid26::is_valid(view) == parsed.has_value());
        }
        expect(agreed) << "is_valid agrees with try_parse on 2048 soups";
    };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
