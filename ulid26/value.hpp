#ifndef ULID26_VALUE_HPP
#define ULID26_VALUE_HPP
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
// ulid26/value.hpp — the self-contained value layer: error machinery, the scalar constexpr text
// codec, the ulid type, parse, hashing and formatting. No generator machinery, so translation units
// that only name, parse or format ids skip the generator templates entirely.
#include <algorithm>
#include <array>
#include <cstdint>
#include <expected>
#include <numeric>
#include <system_error>

namespace ulid26 {
    // Error conditions for every fallible operation in this library.
    // Given a scoped enum can hold values outside of the defined enumerators, value-initializing
    // this errc results in a zero-value, which is intentionally not an error.
    enum class errc : std::int8_t {
        invalid_length = 1, // text input is not 26 characters
        invalid_character,  // byte outside the Crockford base32 alphabet
        overflow,           // decoded value exceeds 7ZZZZZZZZZZZZZZZZZZZZZZZZZ
        time_reversed,      // monotonic generator: the clock stepped backwards
        generation_overflow // monotonic generator: entropy exhausted in one ms
    };

    // A Universally Unique Lexicographically Sortable Identifier (ULID): a 48-bit millisecond
    // timestamp followed by 80 bits of entropy. Pure value type — closed to user manipulation,
    // trivially copyable, fully constexpr.
    class ulid {
        std::uint64_t hi_{0}, low_{0};

        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
        constexpr ulid(std::uint64_t high, std::uint64_t low) noexcept : hi_{high}, low_{low} {}

        constexpr ulid() = default;

      public:
        static constexpr ulid max() noexcept { return ulid{~std::uint64_t{0}, ~std::uint64_t{0}}; }

        static constexpr ulid min() noexcept { return ulid{}; }

        static constexpr std::expected<ulid, errc> try_parse_narrow(std::string_view text) noexcept;
        // Milliseconds since the Unix epoch.
        [[nodiscard]]
        constexpr std::uint64_t timestamp() const noexcept;

        // The member order (hi, then lo) makes the defaulted comparison exactly the spec's
        // left-to-right byte order.
        friend constexpr std::strong_ordering operator<=>(ulid left, ulid right) = default;
    };

    template <typename CharT>
    concept std_char = requires { typename std::char_traits<CharT>::char_type; };
    // Returns true if the text is a valid ULID string, false otherwise.
    template <std_char CharT>
    [[nodiscard]]
    constexpr bool is_valid(std::basic_string_view<CharT> text) noexcept;

    // The fallible constructor of the constexpr domain: returns a valid ULID or an error condition
    // (errc). This should really be an implementation detail if error_code could be made
    // constexpr, but they can't.
    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept;

    // Error category for errc, following the std::error_code idiom.
    class error_category : public std::error_category {
      public:
        [[nodiscard]]
        char const* name() const noexcept override {
            return "ulid";
        }

        [[nodiscard]]
        std::string message(int condition) const override {
            auto cond = static_cast<errc>(condition);
            switch (cond) {
            case errc::invalid_length:
                return "ulid: length must be exactly 26 characters";
            case errc::invalid_character:
                return "ulid: character outside of the Crockford base32 range";
            case errc::overflow:
                return "ulid: value overflows the 128-bit range";
            case errc::time_reversed:
                return "ulid: system clock stepped backwards";
            case errc::generation_overflow:
                return "ulid: entropy exhausted within one millisecond";
            default:
                return "ulid: unknown error";
            }
        }
    };

    [[nodiscard]]
    inline error_category const& ulid_category() noexcept {
        static error_category const instance{};
        return instance;
    }

    [[nodiscard]]
    inline std::error_code make_error_code(errc code) noexcept {
        return std::error_code{static_cast<int>(code), ulid_category()};
    }

    namespace detail {
        constexpr inline std::size_t   kTextSize              = 26;
        constexpr inline std::size_t   kBinarySize            = 16;
        constexpr inline std::size_t   kHalfLength            = 8;
        constexpr inline std::size_t   kAlphabetSize          = 32;
        constexpr inline std::size_t   kDecodeTableSize       = 256;
        constexpr inline std::size_t   kDecimalDigitCount     = 10;
        constexpr inline std::uint32_t kAsciiMax              = 0x7F;
        constexpr inline std::intmax_t kMillisecondsPerSecond = 1000;
        constexpr inline std::size_t   kTimestampChars        = 10;
        constexpr inline std::size_t   kBitsPerChar           = 5;
        constexpr inline std::size_t   kBitsPerByte           = 8;
        constexpr inline std::size_t   kFirstTimestampShift   = 45;
        constexpr inline std::size_t   kCharMask              = 0x1F;
        constexpr inline std::size_t   kLowNibble             = 0x0F;
        constexpr inline std::size_t   kByteMask              = 0xFF;
        constexpr inline std::size_t   kTopByteShift          = 56;
        constexpr inline std::uint8_t  kMaxFirstCharValue     = '7';
        // Entropy chars 10..12 fill hi fully; char 13 straddles the hi/lo halves.
        constexpr inline std::size_t   kEntropyHiGroups       = 3;
        constexpr inline std::size_t   kEntropySplitIndex     = 13;
        constexpr inline std::size_t   kStraddleShift         = 4;
        // Direct group shifts for the encode path (to_chars): groups 0..2 sit
        // in the top 15 bits of the 16 entropy_hi bits, group 3 straddles the
        // halves, groups 4..15 fill the top 60 bits of entropy_lo.
        constexpr inline std::size_t   kEntropyHiTop          = 11;
        constexpr inline std::size_t   kEntropyLoGroups       = 12;
        constexpr inline std::size_t   kLoTopShift            = 60;
        constexpr inline std::size_t   kLoGroupBase           = 55;
        constexpr inline std::uint64_t kTimestampShift        = 16;
        constexpr inline std::uint64_t kEntropyHighMask       = 0xFFFF;
        // MSB of the 8-bit decode table entry is set for invalid characters.
        constexpr inline std::uint64_t kErrorChannelMask      = 0x80;
        // The low 5 bits of the decode table entry are the value channel.
        constexpr inline std::uint64_t kValueChannelMask      = 0x1F;
        constexpr inline std::uint64_t kHashSalt              = 0x9E3779B97F4A7C15ULL;
        constexpr inline std::size_t   kHashShiftLeft         = 6;
        constexpr inline std::size_t   kHashShiftRight        = 2;

        constexpr inline std::string_view kCrockfordAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        static_assert(
            kCrockfordAlphabet.size() == kAlphabetSize,
            "Crockford alphabet must be 32 characters long"
        );

        // Bounds-checked .at() is deliberately avoided: the indices are in range by construction,
        // and .at() is only constexpr from C++26 on (current standard libraries ship it as a DR, so
        // .at() would tie the constexpr decode core to vendor extensions).
        consteval std::array<std::uint8_t, kDecodeTableSize> make_decode_table() {
            static constexpr std::size_t kCaseOffset = 32; // 'a' - 'A'
            // Safety bounds check: the biggest index into the array is the largest letter value in
            // the Crockford alphabet plus the case offset, that must still be smaller than the
            // table size.
            static_assert(
                kCaseOffset + static_cast<std::uint8_t>(kCrockfordAlphabet.back()) <
                    kDecodeTableSize,
                "decode table overflow"
            );

            std::array<std::uint8_t, kDecodeTableSize> table{};
            table.fill(kErrorChannelMask);

            // Maps ASCII digits to their values.
            for (std::uint8_t digit = 0; digit < kDecimalDigitCount; ++digit) {
                unsigned const index = '0' + digit;
                table[index]         = digit;
            }
            // Maps letters in the Crockford alphabet in both cases to their upper case values.
            auto value = static_cast<std::uint8_t>(kDecimalDigitCount);
            for (char const letter : kCrockfordAlphabet.substr(kDecimalDigitCount)) {
                auto const upper           = static_cast<std::uint8_t>(letter);
                table[upper]               = value;
                table[upper + kCaseOffset] = value;
                ++value;
            }
            return table;
        }

        constexpr inline std::array<std::uint8_t, kDecodeTableSize> kDecode = make_decode_table();

        // Constexpr decoding a wide string view into a narrow char array, assuming it contains only
        // valid ASCII characters, returning an error otherwise. Lenght assumed correct, as it's
        // called post length check in is_valid().
        template <std_char CharT>
        [[nodiscard]]
        constexpr std::expected<std::array<char, kTextSize>, errc>
        try_narrowing(std::basic_string_view<CharT> text) noexcept {
            static_assert(
                not std::is_same_v<CharT, char>,
                "do not apply try_narrowing to char, use is_valid directly"
            );
            std::array<char, kTextSize> result{};
            static constexpr auto       kMaxChar = static_cast<CharT>(kAsciiMax);
            for (std::size_t i = 0; i < kTextSize; ++i) {
                if (text[i] > kMaxChar) {
                    return std::unexpected(errc::invalid_character);
                }
                result[i] = static_cast<char>(text[i]);
            }
            return result;
        }

        [[nodiscard]]
        // Lenght assumed correct, as it's called post length check in is_valid().
        constexpr bool is_valid_narrowed(std::string_view text) noexcept {
            // The first character must be 0..7 to avoid overflow.
            if (text[0] > kMaxFirstCharValue) {
                return false;
            }
            return !std::any_of(text.begin(), text.end(), [](char elem) {
                return kDecode[static_cast<std::uint8_t>(elem)] & kErrorChannelMask;
            });
        }
    } // namespace detail

    template <std_char CharT>
    [[nodiscard]]
    constexpr bool is_valid(std::basic_string_view<CharT> text) noexcept {
        if (text.size() != detail::kTextSize) {
            return false;
        }
        if constexpr (std::is_same_v<CharT, char>) {
            return detail::is_valid_narrowed(text);
        } else {
            return detail::try_narrowing(text)
                .transform([](auto&& narrowed) {
                    return detail::is_valid_narrowed(
                        std::string_view{narrowed.data(), narrowed.size()}
                    );
                })
                .value_or(false); // The validation result or false if it errored.
        }
    }

    template <std_char CharT, std::size_t N>
    [[nodiscard]]
    // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
    constexpr bool is_valid(CharT const (&text)[N]) noexcept {
        return is_valid(std::basic_string_view<CharT>{text, N - 1});
    }

    [[nodiscard]]
    // Try parsing text as a ULID, assuming the length is correct.
    constexpr std::expected<ulid, errc> ulid::try_parse_narrow(std::string_view text) noexcept {
        using namespace detail;
        std::uint64_t bad  = 0;
        auto          fold = [&bad](std::uint64_t acc, char elem) -> std::uint64_t {
            std::uint64_t const value = kDecode[static_cast<std::uint8_t>(elem)];
            bad                       = bad | value;                    // carry validity channel
            return (acc << kBitsPerChar) | (value & kValueChannelMask); // value channel
        };

        std::string_view tstamp = text.substr(0, kTimestampChars);
        std::uint64_t    timestamp =
            std::accumulate(tstamp.begin(), tstamp.end(), std::uint64_t{0}, fold);

        std::string_view entropy_h = text.substr(kTimestampChars, kEntropyHiGroups);
        std::uint64_t    entropy_high =
            std::accumulate(entropy_h.begin(), entropy_h.end(), std::uint64_t{0}, fold);

        std::uint64_t const straddle_value =
            kDecode[static_cast<std::uint8_t>(text[kEntropySplitIndex])];
        if ((straddle_value & kErrorChannelMask) != 0) {
            return std::unexpected(errc::invalid_character);
        }
        entropy_high = (entropy_high << 1U) | (straddle_value >> kStraddleShift);

        std::string_view entropy_l = text.substr(kEntropySplitIndex + 1);
        std::uint64_t    entropy_low =
            std::accumulate(entropy_l.begin(), entropy_l.end(), straddle_value & kLowNibble, fold);

        if ((bad & kErrorChannelMask) != 0) {
            return std::unexpected(errc::invalid_character);
        }
        if (text[0] > kMaxFirstCharValue) {
            return std::unexpected(errc::overflow);
        }
        std::uint64_t const high = (timestamp << kTimestampShift) | entropy_high;
        return ulid{high, entropy_low};
    }

    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept {
        if (text.size() != detail::kTextSize) {
            return std::unexpected(errc::invalid_length);
        }
        if constexpr (std::is_same_v<CharT, char>) {
            return ulid::try_parse_narrow(text);
        } else {
            return detail::try_narrowing(text).and_then([](auto&& narrowed) {
                return ulid::try_parse_narrow(std::string_view{narrowed.data(), narrowed.size()});
            });
        }
    }

    template <std_char CharT, std::size_t N>
    [[nodiscard]]
    // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
    constexpr std::expected<ulid, errc> try_parse(CharT const (&text)[N]) noexcept {
        return try_parse(std::basic_string_view<CharT>{text, N - 1});
    }

    constexpr std::uint64_t ulid::timestamp() const noexcept {
        return hi_ >> detail::kTimestampShift;
    }
} // namespace ulid26

// Interoperability with the standard library:
namespace std {
    template <>
    struct is_error_code_enum<ulid26::errc> : true_type {};
} // namespace std
#endif // ULID26_VALUE_HPP
