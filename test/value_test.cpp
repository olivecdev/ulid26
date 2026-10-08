// SPDX-FileCopyrightText: 2026 Carlos Nihelton <cn@olivec.dev>
// SPDX-License-Identifier: MIT
//
// Unit tests for ulid26::is_valid, ulid26::try_parse and ulid26::ulid::to_chars
// (ulid26/value.hpp) with boost-ext/ut. Every vector below mirrors the
// coverage of the reference worktree's suites: known vectors, every
// character width, malformed positions, the first-character cap, length
// mismatches and parse/is_valid agreement, plus the constexpr proofs as
// static_asserts. The encode side adds spelling vectors, the lowercase
// normalization and round-trip proofs: decode inverts encode.
//
// The corpus is literal by nature, so magic-number warnings are suspended
// file-wide. ut itself is a third-party header: it arrives via SYSTEM
// includes (see test/CMakeLists.txt), so our -Werror set never fires
// inside it.

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

#include <array>
#include <bit>
#include <boost/ut.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
// PROBE scaffolding (investigation only): the format-strategy probe below
// needs std::formattable. This include serves the test, not the library.
#include <format>
#include <functional>
#include <iterator>
#include <span>
#include <sstream>
#include <streambuf>
#include <unordered_set>
#include <vector>
#include <string>
#include <string_view>
#include <ulid26/value.hpp>

namespace {

    using ulid26::literals::operator""_ulid;

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
    static_assert(*ulid26::try_parse(kMax) == ulid26::max());
    static_assert(*ulid26::try_parse(kNil) == ulid26::min());
    static_assert(ulid26::min() < ulid26::max());
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

    // Constexpr spelling of an id into a fixed array (proof-side staging:
    // to_chars writes caller storage, so the proof owns the buffer).
    constexpr std::array<char, ulid26::ulid::kTextSize> spell(ulid26::ulid id) {
        std::array<char, ulid26::ulid::kTextSize> out{};
        id.to_chars(std::span<char, ulid26::ulid::kTextSize>{out});
        return out;
    }

    constexpr bool spells(ulid26::ulid id, std::string_view text) {
        auto const             out  = spell(id);
        std::string_view const view{out.data(), out.size()};
        return view == text;
    }

    // Boundaries and the spec example spell exactly.
    static_assert(spells(ulid26::min(), kNil));
    static_assert(spells(ulid26::max(), kMax));
    static_assert(spells(*ulid26::try_parse(kExample), kExample));
    // Encoding normalizes: lowercase parses, uppercase spells.
    static_assert(spells(*ulid26::try_parse("01arz3ndektsv4rrffq69g5fav"), kExample));
    // Maximum timestamp with nil entropy: the two fields spell independently.
    static_assert(spells(*ulid26::try_parse("7ZZZZZZZZZ0000000000000000"), "7ZZZZZZZZZ0000000000000000"));

    // Decode inverts encode, and every spelling validates.
    constexpr bool round_trips(ulid26::ulid id) {
        auto const             text = spell(id);
        std::string_view const view{text.data(), text.size()};
        return ulid26::is_valid(view) && *ulid26::try_parse(view) == id;
    }

    static_assert(round_trips(ulid26::min()));
    static_assert(round_trips(ulid26::max()));
    static_assert(round_trips(*ulid26::try_parse(kExample)));

    // hash_value is deterministic and constexpr: pinned golden values on the
    // 64-bit path (pure integer arithmetic, same input, same hash, every
    // 64-bit platform). The nil hash is the salt itself: combine(0, 0).
    static_assert(ulid26::ulid::hash_value(ulid26::min()) == 11400714819323198485ULL);
    static_assert(ulid26::ulid::hash_value(ulid26::max()) == 2434343235958965292ULL);
    static_assert(
        ulid26::ulid::hash_value(*ulid26::try_parse(kExample)) == 4695206663305829115ULL
    );
    // Equal ids hash equal (including across case), boundaries hash distinct.
    static_assert(
        ulid26::ulid::hash_value(*ulid26::try_parse("01arz3ndektsv4rrffq69g5fav")) ==
        ulid26::ulid::hash_value(*ulid26::try_parse(kExample))
    );
    static_assert(
        ulid26::ulid::hash_value(ulid26::min()) !=
        ulid26::ulid::hash_value(ulid26::max())
    );

    // The consteval literal spells compile-time constants: boundaries, the
    // example and the lowercase normalization. (Invalid text fails the build
    // at the literal by design, so the negative case is untestable in-suite.)
    static_assert("00000000000000000000000000"_ulid == ulid26::min());
    static_assert("7ZZZZZZZZZZZZZZZZZZZZZZZZZ"_ulid == ulid26::max());
    static_assert("01ARZ3NDEKTSV4RRFFQ69G5FAV"_ulid == *ulid26::try_parse(kExample));
    static_assert("01arz3ndektsv4rrffq69g5fav"_ulid == "01ARZ3NDEKTSV4RRFFQ69G5FAV"_ulid);
    static_assert("01ARZ3NDEKTSV4RRFFQ69G5FAV"_ulid.timestamp() == kExampleTimestamp);

    // Constexpr spelling of an id into bytes (proof-side staging, mirroring
    // spell() on the text side).
    constexpr std::array<std::uint8_t, ulid26::ulid::kBinarySize>
    encode_bytes(ulid26::ulid id) {
        std::array<std::uint8_t, ulid26::ulid::kBinarySize> out{};
        id.to_bytes(std::span<std::uint8_t, ulid26::ulid::kBinarySize>{out});
        return out;
    }

    constexpr std::array<std::uint8_t, ulid26::ulid::kBinarySize>
    filled(std::uint8_t value) {
        std::array<std::uint8_t, ulid26::ulid::kBinarySize> out{};
        out.fill(value);
        return out;
    }

    // Big-endian layout, MSB first: the timestamp occupies bytes 0..5, the
    // entropy halves follow, so lexicographic byte order is ULID order.
    constexpr std::array<std::uint8_t, ulid26::ulid::kBinarySize> kExampleBytes = {
        0x01, 0x56, 0x3E, 0x3A, 0xB5, 0xD3, 0xD6, 0x76,
        0x4C, 0x61, 0xEF, 0xB9, 0x93, 0x02, 0xBD, 0x5B,
    };

    static_assert(encode_bytes(ulid26::min()) == filled(0x00));
    static_assert(encode_bytes(ulid26::max()) == filled(0xFF));
    static_assert(encode_bytes(*ulid26::try_parse(kExample)) == kExampleBytes);

    // Bytes invert to the identical value, including patterns no text spelling
    // can name (every 128-bit pattern is a valid value).
    constexpr bool round_trips_bytes(ulid26::ulid id) {
        auto const bytes = encode_bytes(id);
        return ulid26::ulid::from_bytes(
                   std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{bytes}
               ) == id;
    }

    static_assert(round_trips_bytes(ulid26::min()));
    static_assert(round_trips_bytes(ulid26::max()));
    static_assert(round_trips_bytes(*ulid26::try_parse(kExample)));

    // Text and bytes name the same values.
    constexpr bool agrees_text_bytes(ulid26::ulid id) {
        auto const bytes = encode_bytes(id);
        auto const text  = spell(id);
        return *ulid26::try_parse(std::string_view{text.data(), text.size()}) ==
               ulid26::ulid::from_bytes(
                   std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{bytes}
               );
    }

    static_assert(agrees_text_bytes(ulid26::min()));
    static_assert(agrees_text_bytes(ulid26::max()));
    static_assert(agrees_text_bytes(*ulid26::try_parse(kExample)));

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

    // Bit-exact spelling check for any byte type: compares through uint8_t via
    // bit_cast, so signed types compare patterns (not sign-extended values)
    // and std::byte needs no comparison operator.
    template <typename ByteT>
    bool spells_bytes(
        ulid26::ulid id, std::array<std::uint8_t, ulid26::ulid::kBinarySize> const& pin
    ) {
        std::array<ByteT, ulid26::ulid::kBinarySize> out{};
        id.to_bytes(std::span<ByteT, ulid26::ulid::kBinarySize>{out});
        for (std::size_t index = 0; index < out.size(); ++index) {
            if (std::bit_cast<std::uint8_t>(out.at(index)) != pin.at(index)) {
                return false;
            }
        }
        return true;
    }

    // Byte soup over an arbitrary 128-bit space: from_bytes is infallible, so
    // every pattern must survive the round trip — including high bytes in
    // signed buffers, where naive widening sign-extends.
    template <typename ByteT>
    bool bytes_hold(deterministic_rng& rng, std::size_t cases) {
        bool held = true;
        for (std::size_t trial = 0; trial < cases && held; ++trial) {
            std::array<ByteT, ulid26::ulid::kBinarySize> block{};
            for (auto& slot : block) {
                slot = std::bit_cast<ByteT>(rng.next_byte());
            }
            auto const id = ulid26::ulid::from_bytes(
                std::span<ByteT const, ulid26::ulid::kBinarySize>{block}
            );
            std::array<ByteT, ulid26::ulid::kBinarySize> back{};
            id.to_bytes(std::span<ByteT, ulid26::ulid::kBinarySize>{back});
            held = held && std::memcmp(back.data(), block.data(), back.size()) == 0;
        }
        return held;
    }

    // Streambuf whose every write fails: the sink-failure probe for the
    // stream output operator's badbit path.
    struct failing_streambuf : std::streambuf {
        ~failing_streambuf() override = default;

      protected:
        int_type overflow(int_type) override {
            return traits_type::eof();
        }
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
            expect(example.value().timestamp() == kExampleTimestamp) << "example timestamp";
            expect(max.value().timestamp() == kMaxTimestamp) << "max timestamp";
            expect(max.value() == ulid26::max()) << "max parses to ulid::max";
            expect(nil.value().timestamp() == 0) << "nil timestamp";
            expect(nil.value() == ulid26::min()) << "nil parses to ulid::min";
            expect(nil.value() < example.value()) << "nil sorts first";
            expect(example.value() < max.value()) << "example sorts before max";
            expect(ulid26::min() < ulid26::max()) << "min sorts before max";
        }
        auto const lowered = ulid26::try_parse("01arz3ndektsv4rrffq69g5fav");
        if (parsed && lowered.has_value()) {
            expect(lowered.value() == example.value()) << "lowercase parses equal";
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
        capped         = capped && max.has_value() && max.value() == ulid26::max();
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
            expect(from_wide.value().timestamp() == kExampleTimestamp) << "wchar_t timestamp";
            expect(from_u8.value().timestamp() == kExampleTimestamp) << "char8_t timestamp";
            expect(from_u16.value().timestamp() == kExampleTimestamp) << "char16_t timestamp";
            expect(from_u32.value().timestamp() == kExampleTimestamp) << "char32_t timestamp";
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

    "to_chars vectors"_test = [] {
        std::array<char, 26> out{};
        ulid26::min().to_chars(std::span<char, 26>{out});
        expect(std::string_view{out.data(), out.size()} == kNil) << "nil spells zeros";
        ulid26::max().to_chars(std::span<char, 26>{out});
        expect(std::string_view{out.data(), out.size()} == kMax) << "max spells cap";
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (example.has_value()) {
            example.value().to_chars(std::span<char, 26>{out});
            expect(std::string_view{out.data(), out.size()} == kExample) << "example round-spells";
            expect(example.value().to_string() == std::string{kExample})
                << "to_string owns the spelling";
        }
        // Encoding normalizes: lowercase in, uppercase out.
        auto const lowered = ulid26::try_parse("01arz3ndektsv4rrffq69g5fav");
        expect(lowered.has_value()) << "lowercase parses";
        if (lowered.has_value()) {
            lowered.value().to_chars(std::span<char, 26>{out});
            expect(std::string_view{out.data(), out.size()} == kExample)
                << "encode normalizes to uppercase";
        }
    };

    "to_chars widths"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        bool same = true;
        std::array<char16_t, 26> wide16{};
        example.value().to_chars(std::span<char16_t, 26>{wide16});
        std::array<char32_t, 26> wide32{};
        example.value().to_chars(std::span<char32_t, 26>{wide32});
        std::array<wchar_t, 26> wide{};
        example.value().to_chars(std::span<wchar_t, 26>{wide});
        std::array<char8_t, 26> narrow8{};
        example.value().to_chars(std::span<char8_t, 26>{narrow8});
        for (std::size_t index = 0; index < 26; ++index) {
            auto const unit = static_cast<char32_t>(kExample[index]);
            same = same && wide16.at(index) == static_cast<char16_t>(unit) &&
                   wide32.at(index) == unit && wide.at(index) == static_cast<wchar_t>(unit) &&
                   narrow8.at(index) == static_cast<char8_t>(unit);
        }
        expect(same) << "every width spells the same code units";
    };

    "to_chars round-trip"_test = [] {
        // Canonical soup: uppercase alphabet only, so every valid soup is
        // already in canonical spelling and must survive the round trip
        // byte-exact. The first-character cap rejects most soups; the
        // survivors still cover interior, straddle and boundary bits.
        constexpr std::string_view kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        constexpr std::size_t      kCases    = 2048;
        deterministic_rng          rng{0x0123456789ABCDEFULL};
        bool                       held  = true;
        std::size_t                valid = 0;
        for (std::size_t trial = 0; trial < kCases && held; ++trial) {
            std::array<char, 26> soup{};
            for (char& slot : soup) {
                slot = kAlphabet.at(static_cast<std::size_t>(rng.next_byte()) & 31U);
            }
            std::string_view const view{soup.data(), soup.size()};
            auto const             parsed = ulid26::try_parse(view);
            if (parsed.has_value()) {
                ++valid;
                auto const text = spell(parsed.value());
                held = held && std::string_view{text.data(), text.size()} == view;
            }
        }
        expect(valid > 0) << "soup covered valid ids";
        expect(held) << "decode inverts encode on canonical soup";
    };

    "format vectors"_test = [] {
        expect(std::format("{}", ulid26::min()) == std::string{kNil}) << "nil formats";
        expect(std::format("{}", ulid26::max()) == std::string{kMax}) << "max formats";
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (example.has_value()) {
            expect(std::format("{}", example.value()) == std::string{kExample})
                << "example formats";
        }
    };

    "format_to sinks"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        // Growing sink: the iterator sizes the storage itself, no trust needed.
        std::string grown;
        std::format_to(std::back_inserter(grown), "{}", example.value());
        expect(grown == std::string{kExample}) << "back_insert sink grows";
        // Fixed sink: exactly 26 slots, the formatter's documented precondition.
        std::array<char, 26> fixed{};
        auto const end =
            std::format_to_n(fixed.data(), fixed.size(), "{}", example.value());
        expect(end.size == 26) << "format_to_n reports full size";
        expect(std::string_view{fixed.data(), fixed.size()} == kExample) << "fixed sink fills";
    };

    "stream output vectors"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::ostringstream spelled;
        spelled << example.value();
        expect(spelled.str() == std::string{kExample}) << "example streams";
        std::ostringstream nil;
        nil << ulid26::min();
        expect(nil.str() == std::string{kNil}) << "nil streams";
        std::ostringstream maxed;
        maxed << ulid26::max();
        expect(maxed.str() == std::string{kMax}) << "max streams";
    };

    "stream output widths"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::wostringstream wide;
        wide << example.value();
        std::wstring const expected{kExample.begin(), kExample.end()};
        expect(wide.str() == expected) << "wchar_t stream spells";
    };

    "stream output chaining"_test = [] {
        std::ostringstream chained;
        chained << ulid26::min() << ulid26::max();
        expect(chained.str() == std::string{kNil} + std::string{kMax}) << "insertions chain";
    };

    "stream output prefailed"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        // A failed stream stays failed and gains nothing: the sentry path.
        std::ostringstream broken;
        broken.setstate(std::ios_base::badbit);
        broken << example.value();
        expect(!broken) << "failed stream stays failed";
        expect(broken.str().empty()) << "failed stream gains nothing";
    };

    "stream output failure"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        // Every sputc fails here: the operator must report badbit.
        failing_streambuf buf{};
        std::ostream        failing{&buf};
        failing << example.value();
        expect(failing.bad()) << "write failure sets badbit";
    };

    "stream input round-trip"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::istringstream in{std::string{kExample}};
        ulid26::ulid       id{};
        in >> id;
        expect(static_cast<bool>(in)) << "exact read stays good";
        expect(id == example.value()) << "input parses to example";
        std::istringstream nil{std::string{kNil}};
        ulid26::ulid       nil_id{};
        nil >> nil_id;
        expect(nil_id == ulid26::min()) << "nil reads";
        std::istringstream maxed{std::string{kMax}};
        ulid26::ulid       max_id{};
        maxed >> max_id;
        expect(max_id == ulid26::max()) << "max reads";
    };

    "stream input short"_test = [] {
        // Fewer than 26 units: read() fails, failbit set, value untouched.
        std::istringstream in{"short"};
        ulid26::ulid       id = ulid26::max();
        in >> id;
        expect(!in) << "short input fails";
        expect(id == ulid26::max()) << "failed input leaves value untouched";
        std::istringstream empty{""};
        ulid26::ulid       empty_id = ulid26::max();
        empty >> empty_id;
        expect(!empty) << "empty input fails";
        expect(empty_id == ulid26::max()) << "empty input leaves value untouched";
    };

    "stream input long"_test = [] {
        // Overlong input: exactly 26 units consumed, the rest stays put.
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::istringstream in{std::string{kExample} + "XYZ"};
        ulid26::ulid       id{};
        in >> id;
        expect(static_cast<bool>(in)) << "overlong input stays good";
        expect(id == example.value()) << "overlong input parses the prefix";
        std::string rest{};
        std::getline(in, rest);
        expect(rest == "XYZ") << "remainder stays in the stream";
    };

    "stream input bad character"_test = [] {
        // Well-formed length, invalid alphabet: failbit, value untouched.
        std::string        bad{kExample};
        bad.back()         = '*';
        std::istringstream in{bad};
        ulid26::ulid       id = ulid26::max();
        in >> id;
        expect(!in) << "bad character fails";
        expect(id == ulid26::max()) << "bad character leaves value untouched";
    };

    "stream input whitespace"_test = [] {
        // Leading whitespace: the sentry skips it, the id parses.
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::istringstream in{"   " + std::string{kExample}};
        ulid26::ulid       id{};
        in >> id;
        expect(static_cast<bool>(in)) << "whitespace-prefixed input stays good";
        expect(id == example.value()) << "whitespace is skipped";
    };

    "stream input widths"_test = [] {
        // Wide streams take the template overload with explicit arguments;
        // non-ASCII wide units reject with the value untouched.
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        std::wistringstream in{L"01ARZ3NDEKTSV4RRFFQ69G5FAV"};
        ulid26::ulid       id{};
        in >> id;
        expect(static_cast<bool>(in)) << "wide input stays good";
        expect(id == example.value()) << "wide input parses";
        std::wistringstream bad{L"01ARZ3NDEKTSV4RRFFQ69G5FA\u00E9"};
        ulid26::ulid       bad_id = ulid26::max();
        bad >> bad_id;
        expect(!bad) << "non-ASCII wide input fails";
        expect(bad_id == ulid26::max()) << "non-ASCII wide input leaves value untouched";
    };

    "hash_value"_test = [] {
        expect(
            std::hash<ulid26::ulid>{}(ulid26::min()) ==
            ulid26::ulid::hash_value(ulid26::min())
        ) << "std::hash agrees";
        expect(
            std::hash<ulid26::ulid>{}(*ulid26::try_parse(kExample)) ==
            ulid26::ulid::hash_value(*ulid26::try_parse(kExample))
        ) << "std::hash agrees on values";
        std::unordered_set<ulid26::ulid> seen;
        seen.insert(ulid26::min());
        seen.insert(ulid26::max());
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (example.has_value()) {
            seen.insert(example.value());
            expect(seen.size() == 3) << "distinct ids occupy distinct buckets";
            expect(seen.contains(example.value())) << "unordered lookup finds ids";
        }
    };

    "from_string"_test = [] {
        // Owning strings convert implicitly; values agree with try_parse.
        auto const example = ulid26::ulid::from_string(std::string{kExample});
        expect(example.has_value()) << "owning string parses";
        if (example.has_value()) {
            expect(example.value() == *ulid26::try_parse(kExample))
                << "from_string agrees with try_parse";
        }
        // The error_code sink reports the same conditions under ulid_category.
        auto const overflow = ulid26::ulid::from_string("80000000000000000000000000");
        expect(
            !overflow.has_value() &&
            overflow.error() == ulid26::make_error_code(ulid26::errc::overflow)
        ) << "overflow maps to error_code";
        auto const bad_char = ulid26::ulid::from_string("01ARZ3NDEKTSV4RRFFQ69G5FA*");
        expect(
            !bad_char.has_value() &&
            bad_char.error() == ulid26::make_error_code(ulid26::errc::invalid_character)
        ) << "invalid character maps to error_code";
        auto const bad_length = ulid26::ulid::from_string("short");
        expect(
            !bad_length.has_value() &&
            bad_length.error() == ulid26::make_error_code(ulid26::errc::invalid_length)
        ) << "short length maps to error_code";
        // The is_error_code_enum trait wires the implicit conversion.
        std::error_code const converted = ulid26::errc::invalid_character;
        expect(converted.category() == ulid26::ulid_category()) << "category pinned";
        expect(converted == ulid26::make_error_code(ulid26::errc::invalid_character))
            << "implicit conversion agrees";
    };

    "to_bytes vectors"_test = [] {
        expect(encode_bytes(ulid26::min()) == filled(0x00)) << "nil spells zeros";
        expect(encode_bytes(ulid26::max()) == filled(0xFF)) << "max spells ones";
        expect(encode_bytes(*ulid26::try_parse(kExample)) == kExampleBytes)
            << "example spells big-endian";
    };

    "to_bytes widths"_test = [] {
        auto const example = ulid26::try_parse(kExample);
        expect(example.has_value()) << "example parses";
        if (!example.has_value()) {
            return;
        }
        bool same = spells_bytes<char>(example.value(), kExampleBytes) &&
                    spells_bytes<signed char>(example.value(), kExampleBytes) &&
                    spells_bytes<unsigned char>(example.value(), kExampleBytes) &&
                    spells_bytes<std::int8_t>(example.value(), kExampleBytes) &&
                    spells_bytes<std::uint8_t>(example.value(), kExampleBytes) &&
                    spells_bytes<std::byte>(example.value(), kExampleBytes);
        expect(same) << "every byte type spells the same bits";
    };

    "from_bytes vectors"_test = [] {
        expect(
            ulid26::ulid::from_bytes(
                std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{filled(0x00)}
            ) == ulid26::min()
        ) << "zeros parse to nil";
        expect(
            ulid26::ulid::from_bytes(
                std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{filled(0xFF)}
            ) == ulid26::max()
        ) << "ones parse to max";
        expect(
            ulid26::ulid::from_bytes(
                std::span<std::uint8_t const, ulid26::ulid::kBinarySize>{kExampleBytes}
            ) == *ulid26::try_parse(kExample)
        ) << "example bytes parse to example";
    };

    "bytes round-trip"_test = [] {
        // Infallible decode, so the full 128-bit space is covered — patterns
        // no text spelling can name, high bytes included, in unsigned, signed
        // and std::byte buffers alike.
        constexpr std::size_t kCases = 2048;
        deterministic_rng     rng{0xFEEDFACECAFEBEEFULL};
        expect(bytes_hold<std::uint8_t>(rng, kCases)) << "uint8_t soup round-trips";
        expect(bytes_hold<std::int8_t>(rng, kCases)) << "int8_t soup round-trips";
        expect(bytes_hold<std::byte>(rng, kCases)) << "std::byte soup round-trips";
    };

    "byte order matches value order"_test = [] {
        // Canonical text soup, each id pushed twice: duplicate pairs pin ==,
        // cross pairs pin < and >. Array comparison is lexicographic, so this
        // proves byte order IS value order over interior bits too.
        constexpr std::string_view kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
        constexpr std::size_t      kCases    = 512;
        deterministic_rng          rng{0x0BADF00DDEADBEEFULL};
        std::vector<ulid26::ulid>  ids{};
        for (std::size_t trial = 0; trial < kCases; ++trial) {
            std::array<char, 26> soup{};
            for (char& slot : soup) {
                slot = kAlphabet.at(static_cast<std::size_t>(rng.next_byte()) & 31U);
            }
            std::string_view const view{soup.data(), soup.size()};
            auto const             parsed = ulid26::try_parse(view);
            if (parsed.has_value()) {
                ids.push_back(parsed.value());
                ids.push_back(parsed.value());
            }
        }
        bool ordered = ids.size() > 2;
        for (std::size_t pos = 0; pos + 1 < ids.size() && ordered; ++pos) {
            ordered = ordered &&
                      ((encode_bytes(ids.at(pos)) <=> encode_bytes(ids.at(pos + 1))) ==
                       (ids.at(pos) <=> ids.at(pos + 1)));
        }
        expect(ordered) << "byte order agrees with value order";
    };
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
