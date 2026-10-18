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
#include <concepts>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <ios>
#include <iosfwd>
#include <iterator>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <system_error>

namespace ulid26 {
    // A Universally Unique Lexicographically Sortable Identifier (ULID): a 48-bit millisecond
    // timestamp followed by 80 bits of entropy. Pure value type — closed to user manipulation,
    // trivially copyable, fully constexpr.
    class ulid;

    namespace literals {
        /// Opt-in operator for the suffix _ulid, which parses a literal at compile time and fails
        /// the build if the literal is invalid.
        consteval ulid operator""_ulid(char const* text, std::size_t size);
    } // namespace literals

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

    // Binary units: byte-sized integers (but never bool) plus std::byte (without dragging the
    // cstddef header). char8_t is excluded by construction (not integral): text stays text, bytes
    // stay bytes, and no overload set ever confuses the two domains.
    template <typename ByteT>
    concept std_byte = std::same_as<ByteT, std::byte> ||
                       (std::integral<ByteT> && sizeof(ByteT) == 1 && !std::same_as<ByteT, bool>);

    // A concept to detect that a specific type is treated by the standard library as a character
    // type, i.e. has a specialization of std::char_traits.
    template <typename CharT>
    concept std_char = requires { typename std::char_traits<CharT>::char_type; };
    // An output iterator writable with CharT units. Guarantees *it++ = unit (required by
    // std::output_iterator itself), never capacity: 26 writable slots stay a documented
    // precondition, exactly like std::copy_n.
    template <typename IterT, typename CharT>
    concept char_sink = std_char<CharT> && std::output_iterator<IterT, CharT>;

    // Returns true if the text is a valid ULID string, false otherwise.
    template <std_char CharT>
    [[nodiscard]]
    constexpr bool is_valid(std::basic_string_view<CharT> text) noexcept;

    // The fallible free function constructor of the constexpr domain for public usage: returns
    // a valid ULID or an error condition (errc). This should really be an implementation detail
    // if error_code could be made constexpr, but they can't.
    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept;

    class ulid {
      private:
        /// Internal representation is just a simple pair of uint64_ts implementing a network order
        /// binary layout.
        std::uint64_t hi_{0}, low_{0};

        /// Infallible constructor for internal use: the caller guarantees that hi and low compose a
        /// valid ULID.
        // NOLINTNEXTLINE(*-swappable-parameters).
        constexpr ulid(std::uint64_t hi, std::uint64_t low) noexcept : hi_{hi}, low_{low} {}

        /// A passkey to gate unsafe APIs so we don't totally expose them nor grant full friendship
        /// to helpers like the custom std::formatter.
        template <std_char CharT>
        class passkey {
            passkey() = default; // Rule of 7.
            // This would be impossible without the templated passkey idiom: a partial template
            // specialization cannot be declared friend and we didn't want to write manually every
            // possible full specialization of the formatter.
            friend class std::formatter<ulid, CharT>;
            friend constexpr std::expected<ulid, errc>
            try_parse<>(std::basic_string_view<CharT> text) noexcept;
        };

        template <std_char CharT, typename IterT>
            requires char_sink<IterT, CharT>
        [[nodiscard]]
        constexpr IterT to_chars_unchecked(IterT slot) const noexcept;

        template <std_char CharT>
        [[nodiscard]]
        constexpr errc try_parse_unchecked(std::basic_string_view<CharT> text) noexcept;

      public:
        constexpr ulid()                                                         = default;
        // The member order (hi, then lo) makes the defaulted comparison exactly the spec's
        // left-to-right byte order.
        friend constexpr std::strong_ordering operator<=>(ulid left, ulid right) = default;

        static constexpr std::size_t kTextSize   = 26;
        static constexpr std::size_t kBinarySize = 16;

        // Milliseconds since the Unix epoch.
        [[nodiscard]]
        constexpr std::uint64_t timestamp() const noexcept;

        /// Binary serialization API: 16 bytes in big-endian order, any 16-bytes value is inherently
        /// a valid ULID, thus the API is infallible.
        template <std_byte ByteT>
        [[nodiscard]]
        static constexpr ulid from_bytes(std::span<ByteT const, kBinarySize> bytes) noexcept;
        template <std_byte ByteT>
        constexpr void to_bytes(std::span<ByteT, kBinarySize> bytes) const noexcept;

        /// Text oriented API: 26 characters in Crockford base32 alphabet, range from
        /// "00000000000000000000000000" to "7ZZZZZZZZZZZZZZZZZZZZZZZZZ". The text API is fallible,
        /// as not all 26-character strings are valid ULIDs. Functions returning std::error_code
        /// cannot be made constexpr.
        /// from_string: parses a string view into a ULID.
        template <std_char CharT>
        [[nodiscard]]
        static std::expected<ulid, std::error_code>
        from_string(std::basic_string_view<CharT> text) noexcept;

        /// from_string overload for C-style string literals (null-terminated).
        template <std_char CharT, std::size_t N>
        [[nodiscard]]
        // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
        static std::expected<ulid, std::error_code> from_string(CharT const (&text)[N]) noexcept {
            return from_string<CharT>(std::basic_string_view<CharT>{text, N - 1});
        }

        /// from_string overload for owning strings (std::basic_string types).
        template <std_char CharT, typename TraitsT, typename AllocT>
        [[nodiscard]]
        static std::expected<ulid, std::error_code>
        from_string(std::basic_string<CharT, TraitsT, AllocT> const& text) noexcept {
            return from_string<CharT>(std::basic_string_view<CharT>{text.data(), text.size()});
        }

        // to_chars: Non-allocating write of the canonical 26 characters into caller storage, in any
        // character type. This is the zero-allocation path: the client sizes the string (or
        // array, or CharT[26]), the library only fills it. See to_string for the owning form.
        template <std_char CharT = char>
        constexpr void to_chars(std::span<CharT, kTextSize> out) const noexcept;

        // to_string: convenience API to construct owning string form in any character type and
        // allocator: exactly one allocation via resize_and_overwrite, no default-fill, no
        // intermediate copy. Hot paths that cannot allocate use to_chars into caller storage.
        template <std_char CharT = char, typename AllocT = std::allocator<CharT>>
        [[nodiscard]]
        std::basic_string<CharT, std::char_traits<CharT>, AllocT>
        to_string(AllocT const& alloc = AllocT{}) const {
            std::basic_string<CharT, std::char_traits<CharT>, AllocT> text(alloc);
            text.resize_and_overwrite(kTextSize, [this](CharT* buffer, std::size_t) {
                to_chars(std::span<CharT, kTextSize>{buffer, kTextSize});
                return kTextSize;
            });
            return text;
        }

        [[nodiscard]]
        static constexpr std::size_t hash_value(ulid value) noexcept;

        // Outputs into a sink iterator. Precondition: the sink must have at least 26 writable
        // slots, otherwise the behavior is undefined. This precondition is not verifiable in this
        // function. Given the ease of misuse, it's gated under the passkey<CharT>.
        template <std_char CharT, typename IterT>
            requires char_sink<IterT, CharT>
        [[nodiscard]]
        constexpr IterT to_chars_unchecked(IterT slot, passkey<CharT> /*unused*/) const noexcept {
            return to_chars_unchecked<CharT>(slot);
        }

        // Fallible mutator for the constexpr domain: returns an error condition if the parse failed
        // without mutating *this, performing full validation while parsing, except input length.
        // Precondition: text.size() == kTextSize, otherwise the behavior is undefined. Given the
        // ease of misuse, it's gated under the passkey<CharT>.
        template <std_char CharT>
        [[nodiscard]]
        constexpr errc try_parse_assume_length(
            std::basic_string_view<CharT> text, passkey<CharT> /*unused*/
        ) noexcept {
            return try_parse_unchecked<CharT>(text);
        }

      private: /// Friends
        friend constexpr ulid max() noexcept;

        friend constexpr ulid min() noexcept;

        // literal suffix operator needs friendship.
        friend consteval ulid literals::operator""_ulid(char const* text, std::size_t size);

        /// Interoperability with the standard library: stream operators for text I/O.
        template <std_char CharT, typename Traits = std::char_traits<CharT>>
        friend std::basic_ostream<CharT, Traits>&
        operator<<(std::basic_ostream<CharT, Traits>& out, ulid value) {
            // I wanted to use the iterator approach to implement this, but it costs about 2x for
            // the buffered stream case (the common one):
            // `auto sink = std::ostreambuf_iterator<CharT, Traits>{out.rdbuf()};`
            // thus a stack array offers a much better
            // performance.
            std::array<CharT, ulid::kTextSize> buffer{};
            value.to_chars(std::span<CharT, ulid::kTextSize>{buffer});
            return out.write(buffer.data(), static_cast<std::streamsize>(ulid::kTextSize));
        }

        template <std_char CharT, typename Traits = std::char_traits<CharT>>
        friend std::basic_istream<CharT, Traits>&
        operator>>(std::basic_istream<CharT, Traits>& in, ulid& value) {
            typename std::basic_istream<CharT, Traits>::sentry guard(in);
            if (!guard) {
                return in;
            }
            std::array<CharT, ulid::kTextSize> chars{};
            if (!in.read(chars.data(), static_cast<std::streamsize>(ulid::kTextSize))) {
                return in; // read() already set failbit (and eofbit at end of input)
            }
            auto const err = value.try_parse_unchecked<CharT>(
                std::basic_string_view<CharT>{chars.data(), chars.size()}
            );
            if (err != errc{}) {
                in.setstate(std::ios_base::failbit);
            }
            return in;
        }
    }; // class ulid

    constexpr ulid max() noexcept { return ulid{~std::uint64_t{0}, ~std::uint64_t{0}}; }

    constexpr ulid min() noexcept { return ulid{}; }

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

        // Bounds-checked .at() is deliberately avoided: the indices are in range by
        // construction, and .at() is only constexpr from C++26 on (current standard libraries
        // ship it as a DR, so .at() would tie the constexpr decode core to vendor extensions).
        consteval std::array<std::uint8_t, kDecodeTableSize> make_decode_table() {
            static constexpr std::size_t kCaseOffset = 32; // 'a' - 'A'
            // Safety bounds check: the biggest index into the array is the largest letter value
            // in the Crockford alphabet plus the case offset, that must still be smaller than
            // the table size.
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

        // Constexpr decoding a wide string view into a narrow char array, provided it contains
        // only valid ASCII characters, returning an error condition otherwise. Lenght assumed
        // correct, as it's called post length check in is_valid().
        template <std_char CharT>
        [[nodiscard]]
        constexpr std::expected<std::array<char, ulid::kTextSize>, errc>
        try_narrowing_assume_length(std::basic_string_view<CharT> text) noexcept {
            // static_assert(
            //     not std::is_same_v<CharT, char>,
            //     "do not apply try_narrowing to char, use is_valid directly"
            // );
            std::array<char, ulid::kTextSize> result{};
            static constexpr auto             kMaxChar = static_cast<CharT>(kAsciiMax);
            for (std::size_t i = 0; i < result.size(); ++i) {
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
        if (text.size() != ulid::kTextSize) {
            return false;
        }
        if constexpr (std::is_same_v<CharT, char>) {
            return detail::is_valid_narrowed(text);
        } else {
            return detail::try_narrowing_assume_length(text)
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

    template <>
    [[nodiscard]]
    constexpr errc ulid::try_parse_unchecked(std::basic_string_view<char> text) noexcept {
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
            return errc::invalid_character;
        }
        entropy_high = (entropy_high << 1U) | (straddle_value >> kStraddleShift);

        std::string_view entropy_l = text.substr(kEntropySplitIndex + 1);
        std::uint64_t    entropy_low =
            std::accumulate(entropy_l.begin(), entropy_l.end(), straddle_value & kLowNibble, fold);

        if ((bad & kErrorChannelMask) != 0) {
            return errc::invalid_character;
        }
        if (text[0] > kMaxFirstCharValue) {
            return errc::overflow;
        }
        // The actual state is only ever touched if the parse succeeded, so the object remains
        // unchanged on error.
        hi_  = (timestamp << kTimestampShift) | entropy_high;
        low_ = entropy_low;
        return errc{};
    }

    template <std_char CharT>
    [[nodiscard]]
    constexpr errc ulid::try_parse_unchecked(std::basic_string_view<CharT> text) noexcept {
        static_assert(
            not std::is_same_v<CharT, char>,
            "do not apply try_parse to char, use the char overload directly"
        );
        auto const narrow = detail::try_narrowing_assume_length(text);
        if (!narrow) {
            return narrow.error();
        }
        return try_parse_unchecked<char>(
            std::basic_string_view<char>{narrow->data(), narrow->size()}
        );
    }

    constexpr std::uint64_t ulid::timestamp() const noexcept {
        return hi_ >> detail::kTimestampShift;
    }

    namespace literals {

        // Compile-time-checked literal for test vectors and sentinels:
        // invalid text fails the build at the literal, so constants spell
        // themselves (using namespace ulid26::literals to opt in).
        [[nodiscard]]
        consteval ulid operator""_ulid(char const* text, std::size_t size) {
            if (size != ulid::kTextSize) {
                throw std::runtime_error("ulid: literal must be exactly 26 characters long");
            }
            ulid       result{};
            auto const err = result.try_parse_unchecked(std::string_view{text, size});
            if (err != errc{}) {
                throw std::runtime_error(
                    "ulid: literal does not conform to the Crockford base32 alphabet"
                );
            }
            return result;
        }

    } // namespace literals

    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept {
        std::expected<ulid, errc> result{};
        if (text.size() != ulid::kTextSize) {
            result = std::unexpected(errc::invalid_length);
            return result;
        }
        auto err = result->try_parse_assume_length(text, {});
        if (err != errc{}) {
            result = std::unexpected(err);
        }
        return result;
    }

    template <std_char CharT, std::size_t N>
    [[nodiscard]]
    // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
    constexpr std::expected<ulid, errc> try_parse(CharT const (&text)[N]) noexcept {
        return try_parse(std::basic_string_view<CharT>{text, N - 1});
    }

    template <std_char CharT>
    [[nodiscard]]
    std::expected<ulid, std::error_code>
    ulid::from_string(std::basic_string_view<CharT> text) noexcept {
        return ulid26::try_parse<CharT>(text).transform_error(make_error_code);
    }

    template <std_char CharT, typename IterT>
        requires char_sink<IterT, CharT>
    [[nodiscard]]
    constexpr IterT ulid::to_chars_unchecked(IterT slot) const noexcept {
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): local private helper.
        auto encode = [&slot](std::uint64_t value, std::size_t shift, std::size_t boundary) {
            for (std::size_t i = 0; i < boundary; ++i) {
                auto const index  = static_cast<std::uint8_t>((value >> shift) & detail::kCharMask);
                *slot++           = static_cast<CharT>(detail::kCrockfordAlphabet[index]);
                shift            -= detail::kBitsPerChar;
            }
        };

        encode(timestamp(), detail::kFirstTimestampShift, detail::kTimestampChars);

        std::uint64_t const entropy_hi = hi_ & detail::kEntropyHighMask;
        encode(entropy_hi, detail::kEntropyHiTop, detail::kEntropyHiGroups);

        auto const straddle = static_cast<std::uint32_t>(
            ((entropy_hi & 1U) << detail::kStraddleShift) |
            ((low_ >> detail::kLoTopShift) & detail::kLowNibble)
        );
        *slot++ = static_cast<CharT>(detail::kCrockfordAlphabet[straddle]);

        encode(low_, detail::kLoGroupBase, detail::kEntropyLoGroups);
        return slot;
    }

    template <std_char CharT>
    constexpr void ulid::to_chars(std::span<CharT, ulid::kTextSize> out) const noexcept {
        // Compile-time checked that out has exactly 26 writable slots.
        [[maybe_unused]]
        auto iter = to_chars_unchecked<CharT>(out.begin());
    }

    [[nodiscard]]
    constexpr std::size_t ulid::hash_value(ulid value) noexcept {
        std::uint64_t const mixed  = value.low_ + detail::kHashSalt +
                                     (value.hi_ << detail::kHashShiftLeft) +
                                     (value.hi_ >> detail::kHashShiftRight);
        std::uint64_t const folded = value.hi_ ^ mixed;
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
            // Fold the full 64-bit mix before narrowing so 32-bit platforms
            // remain sensitive to the upper timestamp and entropy bits.
            auto const folded32 = static_cast<std::uint32_t>(folded ^ (folded >> 32U));
            return folded32;
        }
        // Exact where size_t is 64 bits; the 32-bit narrowing above returns
        // first, so this cast only silences -Wconversion there.
        return static_cast<std::size_t>(folded);
    }

    template <std_byte ByteT>
    [[nodiscard]]
    constexpr ulid ulid::from_bytes(std::span<ByteT const, kBinarySize> bytes) noexcept {
        auto get_uint64 = [](std::span<ByteT const, detail::kHalfLength> octets) -> std::uint64_t {
            return std::accumulate(
                octets.begin(), octets.end(), std::uint64_t{0}, [](std::uint64_t acc, ByteT elem) {
                    // We need to go unsigned before expanding into uint64_t, otherwise the sign bit
                    // (if ByteT is signed) would propagate and fill the upper bits of the uint64_t
                    // to make it "negative" in two's complement.
                    return (acc << detail::kBitsPerByte) |
                           static_cast<std::uint64_t>(static_cast<std::uint8_t>(elem));
                }
            );
        };
        return ulid{
            get_uint64(bytes.template first<detail::kHalfLength>()),
            get_uint64(bytes.template last<detail::kHalfLength>()),
        };
    }

    template <std_byte ByteT>
    constexpr void ulid::to_bytes(std::span<ByteT, kBinarySize> bytes) const noexcept {
        auto slot       = bytes.begin();
        auto put_uint64 = [&slot](std::uint64_t value) {
            for (std::size_t i = 0; i < detail::kHalfLength; ++i) {
                *slot++   = static_cast<ByteT>(value >> detail::kTopByteShift);
                value   <<= detail::kBitsPerByte;
            }
        };
        put_uint64(hi_);
        put_uint64(low_);
    }
} // namespace ulid26

// Interoperability with the standard library:
namespace std {
    template <>
    struct is_error_code_enum<ulid26::errc> : true_type {};

    template <>
    struct hash<ulid26::ulid> {
        [[nodiscard]]
        std::size_t operator()(ulid26::ulid value) const noexcept {
            return ulid26::ulid::hash_value(value);
        }
    };

    template <ulid26::std_char CharT>
    struct formatter<ulid26::ulid, CharT> {
        constexpr auto parse(std::basic_format_parse_context<CharT>& ctx) {
            if (ctx.begin() == ctx.end()) {
                throw std::format_error("empty range");
            }
            if (*ctx.begin() != '}') {
                throw std::format_error("invalid format");
            }
            return ctx.begin();
        }

        template <typename FmtCtx>
        [[nodiscard]]
        FmtCtx::iterator format(ulid26::ulid value, FmtCtx& ctx) const {
            return value.to_chars_unchecked<CharT>(ctx.out(), {});
        }
    };
} // namespace std
#endif // ULID26_VALUE_HPP
