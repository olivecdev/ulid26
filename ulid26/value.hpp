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
#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace ulid26 {
    class ulid;

    namespace detail {
        struct parts_access;
    } // namespace detail

    namespace literals {
        /// Opt-in operator for the suffix _ulid, which parses a literal at compile time and fails
        /// the build if the literal is invalid.
        consteval ulid operator""_ulid(char const* text, std::size_t size);
    } // namespace literals

    /// Error conditions for fallible operations. A value-initialized errc is not an error.
    enum class errc : std::int8_t {
        invalid_length = 1,
        invalid_character,
        overflow,
        time_reversed,
        generation_overflow
    };

    /// Byte-sized integral types except bool and char8_t, plus std::byte.
    template <typename ByteT>
    concept std_byte = std::same_as<ByteT, std::byte> ||
                       (std::integral<ByteT> && sizeof(ByteT) == 1 && !std::same_as<ByteT, bool> &&
                        !std::same_as<ByteT, char8_t>);

    /// Character types accepted by the text API.
    template <typename CharT>
    concept std_char =
        std::same_as<CharT, char> || std::same_as<CharT, wchar_t> || std::same_as<CharT, char8_t> ||
        std::same_as<CharT, char16_t> || std::same_as<CharT, char32_t>;
    /// Output iterator writable with CharT units; capacity is not checked.
    template <typename IterT, typename CharT>
    concept char_sink = std_char<CharT> && std::output_iterator<IterT, CharT>;

    /// Returns true when text is a valid ULID.
    template <std_char CharT>
    [[nodiscard]]
    constexpr bool is_valid(std::basic_string_view<CharT> text) noexcept;

    /// Parses text into a ULID or an errc.
    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept;

    /// A 48-bit millisecond timestamp followed by 80 bits of entropy.
    class ulid {
      public:
        constexpr ulid()                                                         = default;
        // Member order makes the default comparison match network byte order.
        friend constexpr std::strong_ordering operator<=>(ulid left, ulid right) = default;

        static constexpr std::size_t kTextSize   = 26;
        static constexpr std::size_t kBinarySize = 16;

        /// Milliseconds since the Unix epoch.
        [[nodiscard]]
        constexpr std::uint64_t timestamp() const noexcept;

        /// Decodes 16 big-endian bytes. Every 16-byte value is a valid ULID.
        template <typename ByteT>
            requires std_byte<std::remove_const_t<ByteT>>
        [[nodiscard]]
        static constexpr ulid from_bytes(std::span<ByteT, kBinarySize> bytes) noexcept;
        template <std_byte ByteT>
        constexpr void to_bytes(std::span<ByteT, kBinarySize> bytes) const noexcept;

        /// Parses 26 Crockford base32 characters.
        template <std_char CharT>
        [[nodiscard]]
        static std::expected<ulid, std::error_code>
        from_string(std::basic_string_view<CharT> text) noexcept;

        template <std_char CharT, std::size_t N>
        [[nodiscard]]
        // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
        static std::expected<ulid, std::error_code> from_string(CharT const (&text)[N]) noexcept {
            return from_string<CharT>(std::basic_string_view<CharT>{std::span{text}.data(), N - 1});
        }

        template <std_char CharT, typename TraitsT, typename AllocT>
        [[nodiscard]]
        static std::expected<ulid, std::error_code>
        from_string(std::basic_string<CharT, TraitsT, AllocT> const& text) noexcept {
            return from_string<CharT>(std::basic_string_view<CharT>{text.data(), text.size()});
        }

        /// Writes the canonical 26 characters into out.
        template <std_char CharT = char>
        constexpr void to_chars(std::span<CharT, kTextSize> out) const noexcept;

        /// Returns the canonical 26-character string.
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

        /// Unchecked text codec entry points; callers establish the size preconditions.
        template <std_char CharT>
        class unsafe {
            friend struct std::formatter<ulid, CharT>;
            friend constexpr std::expected<ulid, errc>
            try_parse<>(std::basic_string_view<CharT> text) noexcept;
            friend class ulid;

            template <typename IterT>
                requires char_sink<IterT, CharT>
            [[nodiscard]]
            static constexpr IterT to_chars_unchecked(ulid value, IterT slot) noexcept;

            /// Precondition: text.size() == kTextSize.
            [[nodiscard]]
            static constexpr std::expected<ulid, errc>
            try_parse_unchecked(std::basic_string_view<CharT> text) noexcept;
        };

      private:
        std::uint64_t hi_{0}, low_{0};

        // NOLINTNEXTLINE(*-swappable-parameters).
        constexpr ulid(std::uint64_t hi, std::uint64_t low) noexcept : hi_{hi}, low_{low} {}

        friend constexpr ulid max() noexcept;

        friend constexpr ulid min() noexcept;

        friend struct detail::parts_access;

        template <std_char CharT, typename Traits = std::char_traits<CharT>>
        friend std::basic_ostream<CharT, Traits>&
        operator<<(std::basic_ostream<CharT, Traits>& out, ulid value) {
            std::array<CharT, ulid::kTextSize> buffer{};
            value.to_chars(std::span<CharT, ulid::kTextSize>{buffer});
            return out.write(buffer.data(), static_cast<std::streamsize>(ulid::kTextSize));
        }

        template <std_char CharT, typename Traits = std::char_traits<CharT>>
        friend std::basic_istream<CharT, Traits>&
        operator>>(std::basic_istream<CharT, Traits>& in, ulid& value) {
            typename std::basic_istream<CharT, Traits>::sentry const guard(in);
            if (!guard) {
                return in;
            }
            std::array<CharT, ulid::kTextSize> chars{};
            if (!in.read(chars.data(), static_cast<std::streamsize>(ulid::kTextSize))) {
                return in;
            }
            auto const parsed =
                try_parse(std::basic_string_view<CharT>{chars.data(), chars.size()});
            if (!parsed) {
                in.setstate(std::ios_base::failbit);
            } else {
                value = *parsed;
            }
            return in;
        }
    };

    constexpr ulid max() noexcept { return ulid{~std::uint64_t{0}, ~std::uint64_t{0}}; }

    constexpr ulid min() noexcept { return ulid{}; }

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
                return "ulid: value or clock exceeds the representable range";
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
        // Entropy chars 10..12 fill hi; char 13 straddles the hi/lo halves.
        constexpr inline std::size_t   kEntropyHiGroups       = 3;
        constexpr inline std::size_t   kEntropySplitIndex     = 13;
        constexpr inline std::size_t   kStraddleShift         = 4;
        // Encode shifts: groups 0..2 in hi, group 3 straddles, groups 4..15 in lo.
        constexpr inline std::size_t   kEntropyHiTop          = 11;
        constexpr inline std::size_t   kEntropyLoGroups       = 12;
        constexpr inline std::size_t   kLoTopShift            = 60;
        constexpr inline std::size_t   kLoGroupBase           = 55;
        constexpr inline std::size_t   kTimestampBits         = 48;
        constexpr inline std::size_t   kEntropyBits           = 80;
        constexpr inline std::size_t   kEntropyBytes          = kEntropyBits / kBitsPerByte;
        constexpr inline std::size_t   kEntropyHiBytes        = 2;
        constexpr inline std::uint64_t kTimestampShift        = kEntropyHiBytes * kBitsPerByte;
        constexpr inline std::uint64_t kEntropyHighMask = (std::uint64_t{1} << kTimestampShift) - 1;
        static_assert(
            kTimestampBits + kEntropyBits == ulid::kBinarySize * kBitsPerByte,
            "timestamp and entropy must fill the binary ULID"
        );
        // Decode table entries: MSB is the error channel, low five bits the value channel.
        constexpr inline std::uint64_t kErrorChannelMask = 0x80;
        constexpr inline std::uint64_t kValueChannelMask = 0x1F;
        constexpr inline std::uint64_t kHashSalt         = 0x9E3779B97F4A7C15ULL;
        constexpr inline std::size_t   kHashShiftLeft    = 6;
        constexpr inline std::size_t   kHashShiftRight   = 2;

        constexpr inline std::string_view kCrockfordAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        static_assert(
            kCrockfordAlphabet.size() == kAlphabetSize,
            "Crockford alphabet must be 32 characters long"
        );

        // constraints: libstdc++/libc++ pre-C++26 .at() is not constexpr; Remove with C++26.
        consteval std::array<std::uint8_t, kDecodeTableSize> make_decode_table() {
            static constexpr std::size_t kCaseOffset = 32; // 'a' - 'A'
            static_assert(
                kCaseOffset + static_cast<std::uint8_t>(kCrockfordAlphabet.back()) <
                    kDecodeTableSize,
                "decode table overflow"
            );

            std::array<std::uint8_t, kDecodeTableSize> table{};
            table.fill(kErrorChannelMask);

            for (std::uint8_t digit = 0; digit < kDecimalDigitCount; ++digit) {
                unsigned const index = '0' + digit;
                table[index]         = digit;
            }
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

        template <std_char CharT>
        [[nodiscard]]
        constexpr std::expected<std::array<char, ulid::kTextSize>, errc>
        try_narrowing_assume_length(std::basic_string_view<CharT> text) noexcept {
            static_assert(not std::is_same_v<CharT, char>, "do not apply try_narrowing to char");
            std::array<char, ulid::kTextSize> result{};
            static constexpr auto kMaxChar = static_cast<std::make_unsigned_t<CharT>>(kAsciiMax);
            for (std::size_t i = 0; i < result.size(); ++i) {
                if (static_cast<std::make_unsigned_t<CharT>>(text[i]) > kMaxChar) {
                    return std::unexpected(errc::invalid_character);
                }
                result[i] = static_cast<char>(text[i]);
            }
            return result;
        }

        [[nodiscard]]
        constexpr bool is_valid_narrowed(std::string_view text) noexcept {
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
                .value_or(false);
        }
    }

    template <std_char CharT, std::size_t N>
    [[nodiscard]]
    // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
    constexpr bool is_valid(CharT const (&text)[N]) noexcept {
        return is_valid(std::basic_string_view<CharT>{std::span{text}.data(), N - 1});
    }

    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc>
    ulid::unsafe<CharT>::try_parse_unchecked(std::basic_string_view<CharT> text) noexcept {
        if constexpr (std::is_same_v<CharT, char>) {
            using namespace detail;
            std::uint64_t bad  = 0;
            auto          fold = [&bad](std::uint64_t acc, char unit) -> std::uint64_t {
                std::uint64_t const value = kDecode[static_cast<std::uint8_t>(unit)];
                bad                       = bad | value;
                return (acc << kBitsPerChar) | (value & kValueChannelMask);
            };

            std::string_view const tstamp = text.substr(0, kTimestampChars);
            std::uint64_t          timestamp =
                std::accumulate(tstamp.begin(), tstamp.end(), std::uint64_t{0}, fold);

            std::string_view const entropy_h = text.substr(kTimestampChars, kEntropyHiGroups);
            std::uint64_t          entropy_high =
                std::accumulate(entropy_h.begin(), entropy_h.end(), std::uint64_t{0}, fold);

            std::uint64_t const straddle_value =
                kDecode[static_cast<std::uint8_t>(text[kEntropySplitIndex])];
            if ((straddle_value & kErrorChannelMask) != 0) {
                return std::unexpected(errc::invalid_character);
            }
            entropy_high = (entropy_high << 1U) | (straddle_value >> kStraddleShift);

            std::string_view const entropy_l   = text.substr(kEntropySplitIndex + 1);
            std::uint64_t          entropy_low = std::accumulate(
                entropy_l.begin(), entropy_l.end(), straddle_value & kLowNibble, fold
            );

            if ((bad & kErrorChannelMask) != 0) {
                return std::unexpected(errc::invalid_character);
            }
            if (text[0] > kMaxFirstCharValue) {
                return std::unexpected(errc::overflow);
            }
            return ulid{(timestamp << kTimestampShift) | entropy_high, entropy_low};
        } else {
            auto const narrow = detail::try_narrowing_assume_length(text);
            if (!narrow) {
                return std::unexpected(narrow.error());
            }
            return ulid::unsafe<char>::try_parse_unchecked(
                std::string_view{narrow->data(), narrow->size()}
            );
        }
    }

    constexpr std::uint64_t ulid::timestamp() const noexcept {
        return hi_ >> detail::kTimestampShift;
    }

    namespace literals {
        /// Compile-time literal; invalid text fails the build.
        [[nodiscard]]
        consteval ulid operator""_ulid(char const* text, std::size_t size) {
            if (size != ulid::kTextSize) {
                throw std::runtime_error("ulid: literal must be exactly 26 characters long");
            }
            auto const parsed = try_parse(std::string_view{text, size});
            if (!parsed) {
                throw std::runtime_error(
                    "ulid: literal does not conform to the Crockford base32 alphabet"
                );
            }
            return *parsed;
        }

    } // namespace literals

    template <std_char CharT>
    [[nodiscard]]
    constexpr std::expected<ulid, errc> try_parse(std::basic_string_view<CharT> text) noexcept {
        if (text.size() != ulid::kTextSize) {
            return std::unexpected(errc::invalid_length);
        }
        return ulid::unsafe<CharT>::try_parse_unchecked(text);
    }

    template <std_char CharT, std::size_t N>
    [[nodiscard]]
    // NOLINTNEXTLINE(*-avoid-c-arrays): This is a deduction helper for string literals.
    constexpr std::expected<ulid, errc> try_parse(CharT const (&text)[N]) noexcept {
        return try_parse(std::basic_string_view<CharT>{std::span{text}.data(), N - 1});
    }

    template <std_char CharT>
    [[nodiscard]]
    std::expected<ulid, std::error_code>
    ulid::from_string(std::basic_string_view<CharT> text) noexcept {
        return ulid26::try_parse<CharT>(text).transform_error(make_error_code);
    }

    template <std_char CharT>
    template <typename IterT>
        requires char_sink<IterT, CharT>
    [[nodiscard]]
    constexpr IterT ulid::unsafe<CharT>::to_chars_unchecked(ulid value, IterT slot) noexcept {
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): local helper.
        auto encode = [&slot](std::uint64_t part, std::size_t shift, std::size_t boundary) {
            for (std::size_t i = 0; i < boundary; ++i) {
                auto const index  = static_cast<std::uint8_t>((part >> shift) & detail::kCharMask);
                *slot++           = static_cast<CharT>(detail::kCrockfordAlphabet[index]);
                shift            -= detail::kBitsPerChar;
            }
        };

        encode(value.timestamp(), detail::kFirstTimestampShift, detail::kTimestampChars);

        std::uint64_t const entropy_hi = value.hi_ & detail::kEntropyHighMask;
        encode(entropy_hi, detail::kEntropyHiTop, detail::kEntropyHiGroups);

        auto const straddle = static_cast<std::uint32_t>(
            ((entropy_hi & 1U) << detail::kStraddleShift) |
            ((value.low_ >> detail::kLoTopShift) & detail::kLowNibble)
        );
        *slot++ = static_cast<CharT>(detail::kCrockfordAlphabet[straddle]);

        encode(value.low_, detail::kLoGroupBase, detail::kEntropyLoGroups);
        return slot;
    }

    template <std_char CharT>
    constexpr void ulid::to_chars(std::span<CharT, ulid::kTextSize> out) const noexcept {
        [[maybe_unused]]
        auto iter = unsafe<CharT>::to_chars_unchecked(*this, out.begin());
    }

    [[nodiscard]]
    constexpr std::size_t ulid::hash_value(ulid value) noexcept {
        std::uint64_t const mixed  = value.low_ + detail::kHashSalt +
                                     (value.hi_ << detail::kHashShiftLeft) +
                                     (value.hi_ >> detail::kHashShiftRight);
        std::uint64_t const folded = value.hi_ ^ mixed;
        if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
            auto const folded32 = static_cast<std::uint32_t>(folded ^ (folded >> 32U));
            return folded32;
        }
        return static_cast<std::size_t>(folded);
    }

    template <typename ByteT>
        requires std_byte<std::remove_const_t<ByteT>>
    [[nodiscard]]
    constexpr ulid ulid::from_bytes(std::span<ByteT, kBinarySize> bytes) noexcept {
        auto get_uint64 = [](std::span<ByteT const, detail::kHalfLength> octets) -> std::uint64_t {
            std::uint64_t value = 0;
            for (ByteT const octet : octets) {
                value = (value << detail::kBitsPerByte) |
                        static_cast<std::uint64_t>(static_cast<std::uint8_t>(octet));
            }
            return value;
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
            return ulid26::ulid::unsafe<CharT>::to_chars_unchecked(value, ctx.out());
        }
    };
} // namespace std
#endif // ULID26_VALUE_HPP
