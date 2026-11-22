#ifndef ULID26_GENERATORS_HPP
#define ULID26_GENERATORS_HPP
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
// ulid26/generators.hpp — entropy engines, one-shot generation, the two
// monotonic generator flavors, and reservations.
//
// The generation layer over <ulid26/value.hpp>: anything that mints ids lives
// here. Pulls <mutex>, <random> and <chrono>; translation units that only name,
// parse or format ids include <ulid26/value.hpp> directly to keep their compile
// times down. Engines are standard uniform random bit generators
// (std::mt19937_64 plugs in with zero glue); the two in-house engines are
// default_rng (ChaCha20, seeded once per thread from the OS) and system_rng
// (std::random_device per draw).
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <limits>
#include <mutex>
#include <random>
#include <ranges>
#include <span>
#include <type_traits>
#include <ulid26/value.hpp>
#include <utility>

#if defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
#include <intrin.h> // _InterlockedCompareExchange128 + _mm_pause
#endif

namespace ulid26 {

    // A standard uniform random bit generator, structurally. Standard engines
    // (std::mt19937_64, std::philox4x32 under C++26) satisfy it with zero glue.
    template <typename R>
    concept entropy_engine = std::default_initializable<R> && std::invocable<R&> &&
                             std::unsigned_integral<std::invoke_result_t<R&>> && requires {
                                 { R::min() } -> std::same_as<std::invoke_result_t<R&>>;
                                 { R::max() } -> std::same_as<std::invoke_result_t<R&>>;
                                 requires R::min() < R::max();
                             };

    namespace detail {
        constexpr inline std::uint64_t kTimestampModulus = std::uint64_t{1} << kTimestampBits;
        constexpr inline std::uint64_t kMaxTimestamp     = kTimestampModulus - 1;
        constexpr inline std::uint64_t kMaxUint64        = ~std::uint64_t{0};
        constexpr inline std::uint64_t kMaxReservationCount =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
        constexpr inline std::size_t kChaChaBlockBytes = 64;
        constexpr inline std::size_t kStateWords       = 16;
        constexpr inline std::size_t kDoubleRounds     = 10;
        constexpr inline std::size_t kKeyWords         = 8;
        constexpr inline std::size_t kNonceWords       = 3;
        constexpr inline std::size_t kKeyOffset        = 4;
        constexpr inline std::size_t kCounterIndex     = 12;
        constexpr inline std::size_t kNonceOffset      = 13;
        constexpr inline std::size_t kBytesPerWord     = 4;
        constexpr inline std::size_t kDeviceWordBits   = 32;
        constexpr inline int         kRot16            = 16;
        constexpr inline int         kRot12            = 12;
        constexpr inline int         kRot8             = 8;
        constexpr inline int         kRot7             = 7;

        // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
        // ChaCha20 sigma constants, "expand 32-byte k" little-endian (RFC 8439 §2.3).
        constexpr inline std::array<std::uint32_t, 4> kSigma = {
            0x61707865U, 0x3320646EU, 0x79622D32U, 0x6B206574U
        };

        // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

        // The ChaCha20 block function from RFC 8439 section 2.3. The input is
        // constants | key | counter | nonce; the result already includes the
        // final addition of the input state.
        [[nodiscard]]
        constexpr std::array<std::uint32_t, kStateWords>
        chacha20_block(std::array<std::uint32_t, kStateWords> const& input) noexcept {
            auto       working = input;
            auto&      s00     = working.at(0);
            auto&      s01     = working.at(1);
            auto&      s02     = working.at(2);
            auto&      s03     = working.at(3);
            auto&      s04     = working.at(4);
            auto&      s05     = working.at(5);
            auto&      s06     = working.at(6);
            auto&      s07     = working.at(7);
            auto&      s08     = working.at(8);
            auto&      s09     = working.at(9);
            auto&      s10     = working.at(10);
            auto&      s11     = working.at(11);
            auto&      s12     = working.at(12);
            auto&      s13     = working.at(13);
            auto&      s14     = working.at(14);
            auto&      s15     = working.at(15);
            auto const quarter = [](std::uint32_t& first, std::uint32_t& second,
                                    std::uint32_t& third, std::uint32_t& fourth) {
                first  += second;
                fourth ^= first;
                fourth  = std::rotl(fourth, kRot16);
                third  += fourth;
                second ^= third;
                second  = std::rotl(second, kRot12);
                first  += second;
                fourth ^= first;
                fourth  = std::rotl(fourth, kRot8);
                third  += fourth;
                second ^= third;
                second  = std::rotl(second, kRot7);
            };
            for (std::size_t round = 0; round < kDoubleRounds; ++round) {
                quarter(s00, s04, s08, s12);
                quarter(s01, s05, s09, s13);
                quarter(s02, s06, s10, s14);
                quarter(s03, s07, s11, s15);
                quarter(s00, s05, s10, s15);
                quarter(s01, s06, s11, s12);
                quarter(s02, s07, s08, s13);
                quarter(s03, s04, s09, s14);
            }
            for (std::size_t word = 0; word < kStateWords; ++word) {
                working.at(word) += input.at(word);
            }
            return working;
        }

        [[nodiscard]]
        constexpr std::uint64_t byte_value(std::byte octet) noexcept {
            return static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(octet));
        }

        // The two 64-bit halves of the value. hi holds the 48-bit timestamp in
        // its high bits and the high 16 entropy bits in its low bits; lo holds
        // the low 64 entropy bits.
        struct parts {
            std::uint64_t hi = 0;
            std::uint64_t lo = 0;

            friend constexpr auto operator<=>(parts const&, parts const&) = default;
        };

        [[nodiscard]]
        constexpr ulid assemble_ulid(parts value) noexcept {
            std::array<std::byte, ulid::kBinarySize> bytes{};
            for (std::size_t i = 0; i < kHalfLength; ++i) {
                bytes[i] = static_cast<std::byte>(
                    (value.hi >> (kTopByteShift - (i * kBitsPerByte))) & kByteMask
                );
                bytes[kHalfLength + i] = static_cast<std::byte>(
                    (value.lo >> (kTopByteShift - (i * kBitsPerByte))) & kByteMask
                );
            }
            return ulid::from_bytes(std::span<std::byte, ulid::kBinarySize>{bytes});
        }

        [[nodiscard]]
        constexpr parts split_ulid(ulid value) noexcept {
            std::array<std::byte, ulid::kBinarySize> bytes{};
            value.to_bytes(std::span<std::byte, ulid::kBinarySize>{bytes});
            parts out{};
            for (std::size_t i = 0; i < kHalfLength; ++i) {
                out.hi = (out.hi << kBitsPerByte) | byte_value(bytes[i]);
                out.lo = (out.lo << kBitsPerByte) | byte_value(bytes[kHalfLength + i]);
            }
            return out;
        }

        // Splits 10 entropy bytes into the halves: only the low 16 bits of hi
        // are used.
        [[nodiscard]]
        constexpr parts split_entropy(std::span<std::byte const, kEntropyBytes> bytes) noexcept {
            std::uint64_t hi = 0;
            for (std::byte const octet : bytes.first<kEntropyHiBytes>()) {
                hi = (hi << kBitsPerByte) | byte_value(octet);
            }
            std::uint64_t lo = 0;
            for (std::byte const octet : bytes.subspan<kEntropyHiBytes>()) {
                lo = (lo << kBitsPerByte) | byte_value(octet);
            }
            return parts{.hi = hi, .lo = lo};
        }

        [[nodiscard]]
        inline std::uint64_t clock_ms() noexcept {
            auto const elapsed = std::chrono::system_clock::now().time_since_epoch();
            auto const millis =
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            if (millis < 0) {
                // Pre-epoch clocks clamp; monotonic generators still fail closed.
                return 0;
            }
            return static_cast<std::uint64_t>(millis);
        }

        // Result of the monotonic decision: either fresh entropy must be drawn
        // (fresh == true, next unused) or next already holds the successor.
        struct advance {
            parts next{};
            bool  fresh = false;
        };

        // The decision is a pure function of the observed halves and the clock.
        [[nodiscard]]
        constexpr std::expected<advance, errc>
        decide_advance(parts current, std::uint64_t now) noexcept {
            if (now >= kTimestampModulus) {
                return std::unexpected(errc::overflow);
            }
            std::uint64_t const last_ts = current.hi >> kTimestampShift;
            if (now > last_ts) {
                return advance{.fresh = true};
            }
            if (now == last_ts) {
                if ((current.hi & kEntropyHighMask) == kEntropyHighMask &&
                    current.lo == kMaxUint64) {
                    return std::unexpected(errc::generation_overflow);
                }
                parts next{.hi = current.hi, .lo = current.lo + 1};
                if (next.lo == 0) {
                    ++next.hi;
                }
                return advance{.next = next};
            }
            return std::unexpected(errc::time_reversed);
        }

        // Shared 128-bit generator state: hi holds the timestamp shifted left
        // with the high 16 entropy bits, lo holds the low 64 entropy bits.
        // 16-byte alignment is the cmpxchg16b contract.
        constexpr inline std::size_t kStateAlignment = 16;

        struct alignas(kStateAlignment) state {
            std::uint64_t ts_hi = 0;
            std::uint64_t lo    = 0;
        };

        [[nodiscard]]
        constexpr parts to_parts(state value) noexcept {
            return parts{.hi = value.ts_hi, .lo = value.lo};
        }

        [[nodiscard]]
        constexpr ulid assemble_ulid(state value) noexcept {
            return assemble_ulid(to_parts(value));
        }

        // Adds offset to the 128-bit pair. Callers establish that the result
        // stays inside one timestamp (the reservation bound).
        [[nodiscard]]
        constexpr state offset_state(parts start, std::uint64_t offset) noexcept {
            std::uint64_t const next_lo = start.lo + offset;
            std::uint64_t const carry = (next_lo < start.lo) ? std::uint64_t{1} : std::uint64_t{0};
            return state{.ts_hi = start.hi + carry, .lo = next_lo};
        }

        // How many of count ids fit after start. start is the first id after
        // the current state, so it is never beyond the entropy maximum.
        [[nodiscard]]
        constexpr std::uint64_t grant_from_start(parts start, std::uint64_t count) noexcept {
            if ((start.hi & kEntropyHighMask) != kEntropyHighMask) {
                return count; // at least 2^64 ids remain, and count <= 2^64 - 1
            }
            if (start.lo == 0) {
                return count; // exactly 2^64 ids remain
            }
            std::uint64_t const available = kMaxUint64 - start.lo + 1;
            return std::min(count, available);
        }

#if defined(ULID26_FORCE_MUTEX)
        // Compile-time escape hatch for ThreadSanitizer and mutex-tier validation.
        // Define ULID26_FORCE_MUTEX to omit the CAS tier entirely.
        constexpr inline bool kHasCas128 = false;

        [[nodiscard]]
        inline bool cpu_has_cas128() noexcept {
            return false;
        }

        inline void pause_hint() noexcept {}

        // Unreachable stub: discarded if-constexpr branches still name cas_state.
        inline bool cas_state(state volatile* ptr, state& expected, state desired) noexcept {
            static_cast<void>(ptr);
            static_cast<void>(expected);
            static_cast<void>(desired);
            std::abort();
        }
#elif defined(__x86_64__) && defined(__GNUC__) && !defined(__clang__) &&                           \
    defined(__GCC_ASM_FLAG_OUTPUTS__)
        constexpr inline bool kHasCas128 = true;

        // 128-bit compare-and-swap via cmpxchg16b. Returns true on success; on
        // failure updates expected with the observed state (like compare_exchange).
        [[gnu::target("cx16")]]
        inline bool cas_state(state volatile* ptr, state& expected, state desired) noexcept {
            std::uint64_t       exp_low  = expected.ts_hi;
            std::uint64_t       exp_high = expected.lo;
            std::uint64_t const des_low  = desired.ts_hi;
            std::uint64_t const des_high = desired.lo;
            bool                ok       = false;
            __asm__ __volatile__("lock cmpxchg16b (%[addr]);\n\tsete %[ok]"
                                 : [ok] "=q"(ok), "+m"(*ptr), "+a"(exp_low), "+d"(exp_high)
                                 : [addr] "r"(ptr), "b"(des_low), "c"(des_high)
                                 : "memory", "cc");
            if (!ok) {
                expected.ts_hi = exp_low;
                expected.lo    = exp_high;
            }
            return ok;
        }

        [[nodiscard]]
        inline bool cpu_has_cas128() {
            static bool const supported = [] {
                __builtin_cpu_init();
                return __builtin_cpu_supports("cmpxchg16b") != 0;
            }();
            return supported;
        }

        inline void pause_hint() noexcept { __builtin_ia32_pause(); }
#elif defined(_MSC_VER) && !defined(__clang__) && defined(_M_X64)
        constexpr inline bool kHasCas128 = true;

        inline bool cas_state(state volatile* ptr, state& expected, state desired) noexcept {
            alignas(kStateAlignment) __int64 comparand[2] = {
                static_cast<__int64>(expected.ts_hi), static_cast<__int64>(expected.lo)
            };
            bool const ok =
                _InterlockedCompareExchange128(
                    reinterpret_cast<__int64 volatile*>(&ptr->ts_hi),
                    static_cast<__int64>(desired.lo), static_cast<__int64>(desired.ts_hi), comparand
                ) != 0;
            if (!ok) {
                expected.ts_hi = static_cast<std::uint64_t>(comparand[0]);
                expected.lo    = static_cast<std::uint64_t>(comparand[1]);
            }
            return ok;
        }

        [[nodiscard]]
        inline bool cpu_has_cas128() {
            return true;
        }

        inline void pause_hint() noexcept { _mm_pause(); }
#else
        constexpr inline bool kHasCas128 = false;

        [[nodiscard]]
        inline bool cpu_has_cas128() {
            return false;
        }

        inline void pause_hint() noexcept {}

        // Unreachable stub: discarded if-constexpr branches still name cas_state.
        inline bool cas_state(state volatile* ptr, state& expected, state desired) noexcept {
            static_cast<void>(ptr);
            static_cast<void>(expected);
            static_cast<void>(desired);
            std::abort();
        }
#endif

        // Fans engine words out into 10 bytes (80 bits), consuming exactly the
        // engine's significant bits per draw, so a 31-bit engine never
        // contributes a stuck-zero top bit.
        template <entropy_engine R>
        void fill_entropy(R& source, std::span<std::byte, kEntropyBytes> out) {
            using word_t                       = std::invoke_result_t<R&>;
            constexpr std::size_t kSignificant = static_cast<std::size_t>(
                std::bit_width(static_cast<std::uint64_t>(R::max() - R::min()))
            );
            static_assert(kSignificant >= kBitsPerByte);
            word_t      pool      = 0;
            std::size_t available = 0;
            for (std::byte& slot : out) {
                if (available < kBitsPerByte) {
                    pool      = static_cast<word_t>(source() - R::min());
                    available = kSignificant;
                }
                slot        = static_cast<std::byte>(pool & kByteMask);
                pool      >>= kBitsPerByte;
                available  -= kBitsPerByte;
            }
        }

        template <entropy_engine R>
        [[nodiscard]]
        std::array<std::byte, kEntropyBytes> draw_entropy() {
            thread_local R                       source{};
            std::array<std::byte, kEntropyBytes> entropy{};
            fill_entropy(source, std::span<std::byte, kEntropyBytes>{entropy});
            return entropy;
        }

        // A fresh reservation starts at a random entropy point with room for
        // count ids. Since count <= 2^64 - 1, clamping the low half suffices:
        // the carry out of lo can never be needed.
        template <entropy_engine R>
        [[nodiscard]]
        state fresh_reservation_start(R& engine, std::uint64_t now, std::uint64_t count) {
            std::array<std::byte, kEntropyBytes> entropy{};
            fill_entropy(engine, std::span<std::byte, kEntropyBytes>{entropy});
            parts const         split = split_entropy(entropy);
            std::uint64_t const delta = count - 1;
            std::uint64_t const limit = kMaxUint64 - delta;
            std::uint64_t const lo    = std::min(split.lo, limit);
            return state{.ts_hi = (now << kTimestampShift) | split.hi, .lo = lo};
        }

        template <entropy_engine R>
        [[nodiscard]]
        state fresh_reservation_start(std::uint64_t now, std::uint64_t count) {
            thread_local R engine{};
            return fresh_reservation_start(engine, now, count);
        }

        template <entropy_engine R>
        struct shared_state {
            alignas(kStateAlignment) state data{};
            mutable std::mutex mutex{};
        };

        template <entropy_engine R>
        [[nodiscard]]
        shared_state<R>& global_shared_state() noexcept {
            static shared_state<R> instance{};
            return instance;
        }
    } // namespace detail

    // ChaCha20-based entropy engine (RFC 8439), one instance per thread. Seeded
    // once from std::random_device; throws only if the OS provides no entropy
    // at construction. The uint64_t overload is deterministic, for tests.
    class default_rng {
      public:
        using result_type = std::uint64_t;

        static constexpr result_type min() { return 0; }

        static constexpr result_type max() { return ~result_type{0}; }

        default_rng() {
            for (std::size_t word = 0; word < detail::kSigma.size(); ++word) {
                state_.at(word) = detail::kSigma.at(word);
            }
            std::random_device                           device{};
            std::uniform_int_distribution<std::uint32_t> dist{};
            for (std::size_t word = 0; word < detail::kKeyWords; ++word) {
                state_.at(detail::kKeyOffset + word) = dist(device);
            }
            state_.at(detail::kCounterIndex) = 0;
            for (std::size_t word = 0; word < detail::kNonceWords; ++word) {
                state_.at(detail::kNonceOffset + word) = dist(device);
            }
            refill();
        }

        explicit default_rng(std::uint64_t seed) {
            for (std::size_t word = 0; word < detail::kSigma.size(); ++word) {
                state_.at(word) = detail::kSigma.at(word);
            }
            std::seed_seq seq{
                static_cast<std::uint32_t>(seed),
                static_cast<std::uint32_t>(seed >> detail::kDeviceWordBits)
            };
            std::array<std::uint32_t, detail::kKeyWords + detail::kNonceWords> material{};
            seq.generate(material.begin(), material.end());
            for (std::size_t word = 0; word < detail::kKeyWords; ++word) {
                state_.at(detail::kKeyOffset + word) = material.at(word);
            }
            state_.at(detail::kCounterIndex) = 0;
            for (std::size_t word = 0; word < detail::kNonceWords; ++word) {
                state_.at(detail::kNonceOffset + word) = material.at(detail::kKeyWords + word);
            }
            refill();
        }

        result_type operator()() {
            if (position_ + sizeof(result_type) > detail::kChaChaBlockBytes) {
                refill();
            }
            std::size_t const word  = position_ / detail::kBytesPerWord;
            result_type const low   = keystream_.at(word);
            result_type const high  = keystream_.at(word + 1);
            position_              += sizeof(result_type);
            return (high << detail::kDeviceWordBits) | low;
        }

      private:
        void refill() {
            keystream_ = detail::chacha20_block(state_);
            ++state_.at(detail::kCounterIndex);
            position_ = 0;
        }

        std::array<std::uint32_t, detail::kStateWords> state_{};
        std::array<std::uint32_t, detail::kStateWords> keystream_{};
        std::size_t                                    position_ = 0;
    };

    // OS entropy per draw. Holds no state (the std::random_device lives in
    // thread-local storage), so it stays trivially copyable and movable.
    class system_rng {
      public:
        using result_type = std::uint32_t;

        static constexpr result_type min() { return 0; }

        static constexpr result_type max() { return ~result_type{0}; }

        result_type operator()() const {
            thread_local std::random_device              device{};
            std::uniform_int_distribution<std::uint32_t> dist{};
            return dist(device);
        }
    };

    static_assert(std::uniform_random_bit_generator<default_rng>);
    static_assert(std::uniform_random_bit_generator<system_rng>);

    namespace detail {
        struct reservation_access;
    }

    // An immutable, copyable, random-access range of ulid values. A reservation
    // fixes one millisecond timestamp and a contiguous entropy window; values
    // are computed on demand, so iteration is pure and repeatable. Copying or
    // iterating a reservation never mints new ids: call reserve() again for a
    // new block.
    class reservation : public std::ranges::view_interface<reservation> {
      public:
        class iterator {
          public:
            using iterator_concept  = std::random_access_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using value_type        = ulid;
            using difference_type   = std::int64_t;
            using pointer           = void;
            using reference         = ulid;

            constexpr iterator() = default;

            [[nodiscard]]
            constexpr ulid operator*() const noexcept {
                return detail::assemble_ulid(detail::offset_state(first_, index_));
            }

            constexpr iterator& operator++() noexcept {
                ++index_;
                return *this;
            }

            constexpr iterator operator++(int) noexcept {
                auto copy = *this;
                ++(*this);
                return copy;
            }

            constexpr iterator& operator--() noexcept {
                --index_;
                return *this;
            }

            constexpr iterator operator--(int) noexcept {
                auto copy = *this;
                --(*this);
                return copy;
            }

            constexpr iterator& operator+=(difference_type offset) noexcept {
                // Unsigned conversion is modulo 2^64, so a negative offset
                // subtracts without signed overflow.
                index_ += static_cast<std::uint64_t>(offset);
                return *this;
            }

            constexpr iterator& operator-=(difference_type offset) noexcept {
                index_ -= static_cast<std::uint64_t>(offset);
                return *this;
            }

            [[nodiscard]]
            constexpr ulid operator[](difference_type offset) const noexcept {
                return *(*this + offset);
            }

            friend constexpr auto operator<=>(iterator const&, iterator const&) = default;

            friend constexpr iterator operator+(iterator pos, difference_type offset) noexcept {
                pos += offset;
                return pos;
            }

            friend constexpr iterator operator+(difference_type offset, iterator pos) noexcept {
                pos += offset;
                return pos;
            }

            friend constexpr iterator operator-(iterator pos, difference_type offset) noexcept {
                pos -= offset;
                return pos;
            }

            friend constexpr difference_type
            operator-(iterator const& left, iterator const& right) noexcept {
                return static_cast<difference_type>(left.index_) -
                       static_cast<difference_type>(right.index_);
            }

          private:
            friend class reservation;

            constexpr iterator(detail::parts first, std::uint64_t index) noexcept :
                first_(first), index_(index) {}

            detail::parts first_{};
            std::uint64_t index_ = 0;
        };

        constexpr reservation() = default;

        [[nodiscard]]
        constexpr std::uint64_t size() const noexcept {
            return count_;
        }

        [[nodiscard]]
        constexpr bool empty() const noexcept {
            return count_ == 0;
        }

        [[nodiscard]]
        constexpr ulid first() const noexcept {
            return detail::assemble_ulid(first_);
        }

        [[nodiscard]]
        constexpr ulid last() const noexcept {
            if (count_ == 0) {
                return ulid{};
            }
            return detail::assemble_ulid(detail::offset_state(first_, count_ - 1));
        }

        [[nodiscard]]
        constexpr ulid operator[](std::uint64_t index) const noexcept {
            return detail::assemble_ulid(detail::offset_state(first_, index));
        }

        [[nodiscard]]
        constexpr iterator begin() const noexcept {
            return iterator{first_, 0};
        }

        [[nodiscard]]
        constexpr iterator end() const noexcept {
            return iterator{first_, count_};
        }

      private:
        friend struct detail::reservation_access;

        constexpr reservation(detail::parts first, std::uint64_t count) noexcept :
            first_(first), count_(count) {}

        detail::parts first_{};
        std::uint64_t count_ = 0;
    };

    namespace detail {
        struct reservation_access {
            [[nodiscard]]
            static constexpr reservation make(parts first, std::uint64_t count) noexcept {
                return reservation{first, count};
            }
        };
    } // namespace detail

    // One-shot generation with a fresh timestamp and fresh entropy. Safe to call
    // from any thread (entropy is thread-local). Throws only if the OS provides
    // no entropy on first use in a thread; timestamps outside the 48-bit range
    // saturate instead of failing. For more than one id, use a generator
    // reservation rather than calling this in a loop.
    template <entropy_engine R = default_rng>
    [[nodiscard]]
    ulid generate() {
        std::array<std::byte, detail::kEntropyBytes> const entropy = detail::draw_entropy<R>();
        detail::parts const                                split   = detail::split_entropy(entropy);
        std::uint64_t                                      timestamp = detail::clock_ms();
        if (timestamp >= detail::kTimestampModulus) {
            timestamp = detail::kMaxTimestamp;
        }
        return detail::assemble_ulid(
            detail::parts{
                .hi = (timestamp << detail::kTimestampShift) | split.hi,
                .lo = split.lo,
            }
        );
    }

    // Monotonic generator with per-object state: zero synchronization. Copying
    // is deleted (a copy would fork the sequence); moving transfers the sequence
    // and re-seeds the moved-from engine.
    template <entropy_engine R = default_rng>
    class unsynchronized_generator {
      public:
        unsynchronized_generator() = default;

        explicit unsynchronized_generator(R engine) : rng_(std::move(engine)) {}

        // Resume a sequence after a restart: continues strictly after previous.
        explicit unsynchronized_generator(ulid previous) {
            auto const parts = detail::split_ulid(previous);
            ts_hi_           = parts.hi;
            lo_              = parts.lo;
        }

        unsynchronized_generator(unsynchronized_generator const&)            = delete;
        unsynchronized_generator& operator=(unsynchronized_generator const&) = delete;

        unsynchronized_generator(unsynchronized_generator&& other) noexcept(
            std::is_nothrow_move_constructible_v<R> && std::is_nothrow_move_assignable_v<R> &&
            std::is_nothrow_default_constructible_v<R>
        ) :
            rng_(std::move(other.rng_)), ts_hi_(std::exchange(other.ts_hi_, 0)),
            lo_(std::exchange(other.lo_, 0)) {
            other.rng_ = R{};
        }

        unsynchronized_generator& operator=(unsynchronized_generator&& other) noexcept(
            std::is_nothrow_move_assignable_v<R> && std::is_nothrow_default_constructible_v<R>
        ) {
            if (this == &other) {
                return *this;
            }
            rng_       = std::move(other.rng_);
            ts_hi_     = std::exchange(other.ts_hi_, 0);
            lo_        = std::exchange(other.lo_, 0);
            other.rng_ = R{};
            return *this;
        }

        ~unsynchronized_generator() = default;

        // A single id. For more than one, reserve() rather than looping this.
        [[nodiscard]]
        std::expected<ulid, std::error_code> next() {
            auto block = reserve(1);
            if (!block) {
                return std::unexpected(block.error());
            }
            if (block->empty()) {
                return std::unexpected(make_error_code(errc::generation_overflow));
            }
            return block->first();
        }

        // Best-effort reservation: grants between 0 and count ids, all within
        // one millisecond. Fails only for time_reversed (or an unrepresentable
        // clock). A short or empty result is success: size() reports the grant.
        // count is capped at INT64_MAX because reservation iterators use a signed difference type.
        [[nodiscard]]
        std::expected<reservation, std::error_code> reserve(std::uint64_t count) {
            if (count == 0) {
                return reservation{};
            }
            count                   = std::min(count, detail::kMaxReservationCount);
            std::uint64_t const now = detail::clock_ms();
            auto const          decision =
                detail::decide_advance(detail::parts{.hi = ts_hi_, .lo = lo_}, now);
            if (!decision) {
                if (decision.error() == errc::generation_overflow) {
                    return reservation{};
                }
                return std::unexpected(make_error_code(decision.error()));
            }

            detail::parts first{};
            std::uint64_t granted = 0;
            if (decision->fresh) {
                first   = detail::to_parts(detail::fresh_reservation_start(rng_, now, count));
                granted = count;
            } else {
                first   = decision->next;
                granted = detail::grant_from_start(first, count);
            }

            auto const end = detail::offset_state(first, granted - 1);
            ts_hi_         = end.ts_hi;
            lo_            = end.lo;
            return detail::reservation_access::make(first, granted);
        }

      private:
        R             rng_{};
        std::uint64_t ts_hi_ = 0;
        std::uint64_t lo_    = 0;
    };

    // Monotonic generator over one global atomic state per engine type: every
    // instance is a handle to the same sequence, so it is strictly increasing
    // across ALL threads without passing a reference around. One commit per
    // reservation; use reserve() rather than next() for bulk work.
    template <entropy_engine R = default_rng>
    class shared_generator {
      public:
        // A single id. For more than one, reserve() rather than looping this.
        [[nodiscard]]
        std::expected<ulid, std::error_code> next() {
            auto& shared = detail::global_shared_state<R>();
            if constexpr (detail::kHasCas128) {
                if (use_cas_) {
                    return next_cas(shared);
                }
            }
            return next_locked(shared);
        }

        // Best-effort reservation: grants between 0 and count ids, all within
        // one millisecond, disjoint from every other reservation. Fails only
        // for time_reversed (or an unrepresentable clock).
        // count is capped at INT64_MAX because reservation iterators use a signed difference type.
        [[nodiscard]]
        std::expected<reservation, std::error_code> reserve(std::uint64_t count) {
            if (count == 0) {
                return reservation{};
            }
            count        = std::min(count, detail::kMaxReservationCount);
            auto& shared = detail::global_shared_state<R>();
            if constexpr (detail::kHasCas128) {
                if (use_cas_) {
                    return reserve_cas(shared, count);
                }
            }
            return reserve_locked(shared, count);
        }

      private:
        [[nodiscard]]
        static std::expected<ulid, std::error_code> next_cas(detail::shared_state<R>& shared) {
            std::uint64_t now = detail::clock_ms();
            detail::state observed{};
            detail::cas_state(&shared.data, observed, observed);
            for (;;) {
                auto decision = detail::decide_advance(detail::to_parts(observed), now);
                if (!decision) {
                    now      = detail::clock_ms();
                    decision = detail::decide_advance(detail::to_parts(observed), now);
                    if (!decision) {
                        return std::unexpected(make_error_code(decision.error()));
                    }
                }

                detail::state const first =
                    decision->fresh ?
                        detail::fresh_reservation_start<R>(now, 1) :
                        detail::state{.ts_hi = decision->next.hi, .lo = decision->next.lo};
                if (detail::cas_state(&shared.data, observed, first)) {
                    return detail::assemble_ulid(first);
                }
                detail::pause_hint();
            }
        }

        [[nodiscard]]
        static std::expected<ulid, std::error_code> next_locked(detail::shared_state<R>& shared) {
            std::scoped_lock const lock{shared.mutex};
            std::uint64_t const    now = detail::clock_ms();
            auto const decision        = detail::decide_advance(detail::to_parts(shared.data), now);
            if (!decision) {
                return std::unexpected(make_error_code(decision.error()));
            }

            detail::state const first =
                decision->fresh ?
                    detail::fresh_reservation_start<R>(now, 1) :
                    detail::state{.ts_hi = decision->next.hi, .lo = decision->next.lo};
            shared.data = first;
            return detail::assemble_ulid(first);
        }

        [[nodiscard]]
        static std::expected<reservation, std::error_code>
        reserve_cas(detail::shared_state<R>& shared, std::uint64_t count) {
            std::uint64_t now = detail::clock_ms();
            detail::state observed{};
            detail::cas_state(&shared.data, observed, observed);
            for (;;) {
                auto decision = detail::decide_advance(detail::to_parts(observed), now);
                if (!decision) {
                    now      = detail::clock_ms();
                    decision = detail::decide_advance(detail::to_parts(observed), now);
                    if (!decision) {
                        if (decision.error() == errc::generation_overflow) {
                            return reservation{};
                        }
                        return std::unexpected(make_error_code(decision.error()));
                    }
                }

                detail::parts first{};
                std::uint64_t granted = 0;
                if (decision->fresh) {
                    first   = detail::to_parts(detail::fresh_reservation_start<R>(now, count));
                    granted = count;
                } else {
                    first   = decision->next;
                    granted = detail::grant_from_start(first, count);
                }

                detail::state const desired = detail::offset_state(first, granted - 1);
                if (detail::cas_state(&shared.data, observed, desired)) {
                    return detail::reservation_access::make(first, granted);
                }
                detail::pause_hint();
            }
        }

        [[nodiscard]]
        static std::expected<reservation, std::error_code>
        reserve_locked(detail::shared_state<R>& shared, std::uint64_t count) {
            std::scoped_lock const lock{shared.mutex};
            std::uint64_t const    now = detail::clock_ms();
            auto const decision        = detail::decide_advance(detail::to_parts(shared.data), now);
            if (!decision) {
                if (decision.error() == errc::generation_overflow) {
                    return reservation{};
                }
                return std::unexpected(make_error_code(decision.error()));
            }

            detail::parts first{};
            std::uint64_t granted = 0;
            if (decision->fresh) {
                first   = detail::to_parts(detail::fresh_reservation_start<R>(now, count));
                granted = count;
            } else {
                first   = decision->next;
                granted = detail::grant_from_start(first, count);
            }

            shared.data = detail::offset_state(first, granted - 1);
            return detail::reservation_access::make(first, granted);
        }

        bool use_cas_ = detail::cpu_has_cas128();
    };

} // namespace ulid26
#endif // ULID26_GENERATORS_HPP
