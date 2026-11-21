# ulid26

> Universally Unique Lexicographically Sortable Identifiers for C++23.

A spec-exact, thread-safe, constexpr-friendly implementation of the 
[ULID specification](https://github.com/ulid/spec) — the 26-character sort key — with
`std::expected`/`std::error_code` error handling for fallible value APIs, no dependencies,
header-only. Entropy engines may throw while acquiring OS entropy.

```
C++23 |  header-only | zero deps | constexpr
```

```cpp
ulid26::ulid id = ulid26::generate();
// 01ARZ3NDEKTSV4RRFFQ69G5FAV
```

## Why ULID

UUIDs are suboptimal in many contexts: UUID v4 carries no time information and fragments indexes;
UUID v1/v2 depend on MAC addresses; UUIDv3/v5 scatter randomly. ULID instead packs a **48-bit
millisecond timestamp** and **80 bits of entropy** into 128 bits, canonically encoded as a
**26-character** Crockford base32 string that sorts lexicographically by creation time.

```
 01AN4Z07BY      79KA1307SR9X4MV3

|----------|    |----------------|
 Timestamp          Randomness
   48 bits            80 bits
```

- 128-bit compatibility with UUID
- 1.21e+24 unique ULIDs per millisecond
- Lexicographically sortable — the string *is* the sort key
- 26 characters vs 36 for UUID
- Crockford base32: no `I L O U`, URL-safe, case-insensitive on decode
- Monotonic generation within the same millisecond

## Requirements

| | |
|---|---|
| Compilers | GCC 14+ (primary, incl. CAS tier; CI runs GCC 16) · Clang 22+ (mutex tier) · MSVC 19.36+ / VS 2022 17.6+ (CAS tier on x64, mutex elsewhere; Windows CI) · MinGW-w64 GCC 14+ (CAS tier expected; not run here) · floor validated by the `ci` workflow on bare ubuntu-24.04 + GCC 14 (`baseline` job) |
| Standard | C++23 (`-std=c++23`; MSVC maps it to `/std:c++latest`) |
| Windows | `cmake -S . -B build/win -G "Visual Studio 17 2022" -A x64` (presets pin Ninja); reference comparisons are excluded there — suyash/ulid needs `__int128` |
| Dependencies | none |
| Link model | header-only; single CMake target `ulid26::ulid26`, all headers installed |

## Quick start

### CMake with FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    ulid26
    GIT_REPOSITORY https://github.com/olivecdev/ulid26
    GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(ulid26)

target_link_libraries(myapp PRIVATE ulid26::ulid26)
```

### First ULID

```cpp
#include <ulid26/ulid26.hpp>
#include <print>

int main() {
    auto id = ulid26::generate();          // thread-safe one-shot
    std::println("{}", id);                // std::formatter prints canonical text
}
```

## The value type `ulid`

`ulid26::ulid` is a pure value type: 16 bytes, trivially copyable, standard layout, and
`constexpr` for value operations and the scalar codec. It holds a 48-bit millisecond timestamp and
80 bits of entropy, and compares lexicographically exactly as its canonical string does —
`operator<=>` is the spec's byte order.

```cpp
namespace ulid26 {
class ulid {
public:
    static constexpr std::size_t kTextSize   = 26; // canonical text length
    static constexpr std::size_t kBinarySize = 16; // binary length

    ulid();                                        // nil: 00000000000000000000000000

    std::uint64_t timestamp() const;                 // ms since Unix epoch (no <chrono>, but easily convertible to it)

    static std::expected<ulid, std::error_code> from_string(std::string_view);
                                                     // Crockford base32, case-insensitive
    static ulid from_bytes(std::span<const std::byte, kBinarySize>) noexcept;
                                                     // network byte order, infallible
    void to_chars(std::span<char, kTextSize> out) const; // non-allocating write (see below)
    std::string to_string() const;                   // one allocation, via resize_and_overwrite
    void to_bytes(std::span<std::byte, kBinarySize> out) const noexcept;

    friend std::strong_ordering operator<=>(ulid, ulid) = default; // == canonical string order
    // std::hash<ulid> and hash_value() provided
    // std::formatter<ulid> provided: std::format("{}", id) is canonical text
    // (non-empty format specs are rejected)
};
}
```

Default construction produces the nil value — compare with `u == ulid26::ulid{}`.

The value type and scalar codec are `constexpr`: the library ships `static_assert`-driven
specification tests, including round-trip proofs and malformed-input rejection, evaluated at compile
time. The public `from_string` wraps the constexpr text parser and maps its `errc` to
`std::error_code`; `from_bytes` is an infallible binary conversion. The compile-time entries are
separate names: `is_valid` answers validity of a literal, and `try_parse` parses one into a value,
reporting the same precise `errc` the runtime API maps to `std::error_code` (`from_string` cannot
overload on return type alone).

```cpp
static_assert(ulid26::is_valid("7ZZZZZZZZZZZZZZZZZZZZZZZZZ"));
static_assert(!ulid26::is_valid("80000000000000000000000000"));
static_assert(!ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FA*"));
static_assert(ulid26::try_parse("01ARZ3NDEKTSV4RRFFQ69G5FAV")->timestamp() == 1469922850259ULL);
static_assert(ulid26::try_parse("80000000000000000000000000").error() == ulid26::errc::overflow);
```

## Errors

Failures use the standard error-code machinery (`std::errc`-style):

- fallible operations return `std::expected<T, std::error_code>`;
- reservations are best-effort: a short or empty `reservation` is success and reports its capacity
  through `size()`, not through an error.

The library itself doesn't throw exceptions, but stdlib APIs might do it under our feet. Also,
entropy acquisition may throw if the operating system cannot provide entropy.

```cpp
enum class errc {
    invalid_length = 1,   // input is not 26 characters
    invalid_character,    // byte outside the Crockford alphabet (I/L/O/U rejected)
    overflow,             // value exceeds 7ZZZZZZZZZZZZZZZZZZZZZZZZZ
    time_reversed,        // monotonic: system clock stepped backwards (fail closed)
    generation_overflow,  // monotonic: entropy exhausted within one millisecond
};

template<> struct std::is_error_code_enum<ulid26::errc> : std::true_type {};
// ulid26::make_error_code(errc) -> std::error_code, category name "ulid"
```

Errors are recoverable by design: on generation failure, generator state is untouched.
Entropy-source failures are construction or draw failures and are propagated as exceptions by the
standard library, we do not interfere with that.

## Generation

`generate()` mints exactly one id: one fresh clock read and one fresh entropy draw, with no state.
It is safe from any thread. It's a heavy convenience API: it instantiates a generator discarded in
the end of the scope, and draws one ID from it. Use it for one-off generation only. Do not call it
in a loop; for more than one id, take a reservation from a generator.


```cpp
ulid26::ulid id = ulid26::generate();

// Same, with an explicit engine (see Custom entropy engines below).
ulid26::ulid other = ulid26::generate<ulid26::system_rng>();
```

### Monotonic generators

The ULID spec allows a great performance optimization for bulk generation: when several ULIDs are
produced within the same millisecond, monotonic generation may increment the previous entropy by one
instead of drawing fresh randomness, guaranteeing strictly increasing output.
This library provides two flavors of monotonic generators:

```cpp
// Per-object state: zero synchronization, strictly increasing per instance. Use it when generation
// happens via a single object never shared across threads.
ulid26::unsynchronized_generator m;
auto a = m.next();    // -> std::expected<ulid, std::error_code>
auto b = m.next();    // b > a whenever the clock confirms the same or later millisecond

// A generator owns the engine passed at construction. A seeded engine makes the entropy stream
// deterministic.
ulid26::unsynchronized_generator<ulid26::default_rng> seeded{ulid26::default_rng{42}};

// Shared: all instances refer to the same global atomic state per engine type, so the sequence is
// strictly increasing across ALL threads. Use it when you're unsure which thread generates next.
ulid26::shared_generator g;
auto c = g.next();
```

`shared_generator` is a handle to the global sequence, not a per-thread stream. Every instance for
the same engine type — including one constructed independently in each thread — advances the same
sequence, so per-thread instances are safe and do not fork or partition it. You can equally share a
single instance by reference. Fresh entropy draws stay thread-local; only the ordering state is
shared. For independent sequences when concurrency is not needed, use `unsynchronized_generator`.

`unsynchronized_generator` is non-copyable. Moving transfers the sequence and retires the source
object: the moved-from generator is reset and starts a fresh, re-seeded sequence if used again.
`shared_generator` is a lightweight handle to the global state. Its only per-object state is a
cached CAS-tier capability bit, so all instances are interchangeable.

State machine (both flavors):

| condition | action |
|---|---|
| `now > last_ts` | fresh entropy draw |
| `now == last_ts` | increment previous entropy by 1 |
| `now < last_ts` | fail closed: `errc::time_reversed`, state preserved |
| entropy at maximum | `next()`: `errc::generation_overflow`; `reserve()`: empty reservation |

`unsynchronized_generator` is stateful; keep it around and reuse it for as long as you need more
ids. `shared_generator` handles are cheap and all address the same global sequence.

Both flavors expose `next()` for a single id and `reserve(count)` for bulk. Prefer `reserve()` for
anything beyond one id: it reduces the committing synchronization to one event per block and gives
you a local range to drain without further calls.

### Reservations

A reservation is an immutable, copyable, random-access range of `ulid`. It fixes one millisecond
timestamp and a contiguous entropy window; iteration computes values on demand, so it is pure and
repeatable. It models a `std::ranges::view`, so it composes with the standard pipeline:

```cpp
ulid26::shared_generator g;
auto r = g.reserve(64);    // -> std::expected<reservation, std::error_code>
if (!r)
    return r.error();

for (auto id : *r)                              // range-for
    consume(id);

for (auto id : *r | std::views::take(10))       // views pipeline
    consume(id);

auto ids = *r | std::views::transform(...) | std::ranges::to<std::vector>();
```

A reservation never spans milliseconds and is always exactly the size reported by `size()`.
`reserve(count)` is best-effort: it grants between 0 and `count` ids from the remainder of the
current millisecond, and a short or empty result is success, not an error. Capacity is *reported*
by the range, never by an oversized error; `generation_overflow` belongs to single-value `next()`
only. Copying or iterating a reservation never mints new ids: call `reserve()` again for a new
block.

Even if two threads reserve in the same millisecond, the blocks are disjoint and ordered across
threads. The shared state stores the last ULID of the reserved block, not the first: once a thread
succeeds in reserving a block, the next thread sees a strictly higher last ULID and cannot overlap.
The atomic (or mutex-based) protocol ensures no thread can observe a partial or outdated state.

### Custom entropy engines

Generators take any standard uniform random bit generator as their template parameter —
`std::mt19937_64` plugs in with zero glue code:

```cpp
ulid26::unsynchronized_generator<std::mt19937_64> gen{std::mt19937_64{42}};
```

The in-house engines are `default_rng` (`ChaCha20`, seeded once per thread from the OS; ~28ns per
draw) and `system_rng` (`std::random_device` per draw; ~5µs, but OS-maintained and fork-safe). Two
warnings travel with the freedom: non-cryptographic engines (`mt19937_64`, and Philox — available as
`std::philox4x32` only under C++26) must never back ids that need unguessability, and an engine with
a fixed default seed yields identical streams on every thread under `shared_generator` (uniqueness
still holds — the shared counter sequences collisions — but the entropy correlates).


### Thread-safety guarantees

| flavor | synchronization | guarantee |
|---|---|---|
| `generate()` | none (thread-local entropy) | safe from any thread; no ordering promise within a millisecond (per spec) |
| `unsynchronized_generator` | none | strict per-instance order; cross-thread order not guaranteed |
| `shared_generator` | lock-free CAS (GCC x86-64 / MSVC x64) / mutex fallback | strict global **commit** order: commit order == value order |
| `reservation` | none | iterating an already-reserved block is pure and repeatable |

The shared flavor uses a `cmpxchg16b` CAS on a 16-byte aligned state on GCC x86-64 and MSVC x64
(inline asm on GCC, `_InterlockedCompareExchange128` on MSVC), and a mutex-guarded implementation
elsewhere. Plain standard C++ doesn't guarantee lock-free atomic operations on 16-byte structures.
Both implementations pass the identical test suite and are required to exhibit identical observable
behavior (differential testing); the mutex flavor additionally validates under ThreadSanitizer.

The CAS tier takes one CAS-shaped state snapshot before its first attempt. A failed committing CAS
reports the current state in its expected operand; the retry reuses that observation instead of
taking another snapshot. If the state has advanced to a newer millisecond than the cached clock, the
decision fails and the clock is read again. This keeps the decision and commit on one validated
state, preserves strict global commit order and disjoint reservations, and avoids a redundant
snapshot CAS on every contention retry.

Reservation blocks are granted in commit order and are disjoint across threads, but the caller
controls when it drains them. Application-level emission can therefore lag or interleave even though
the commit order is the value order.

### Resuming after restart

A monotonic sequence can continue from a persisted id:

```cpp
ulid26::unsynchronized_generator m{last_id_from_disk};
```

## Text codec

### Single values

```cpp
auto u = ulid26::ulid::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV");
auto v = ulid26::ulid::from_string("01arz3ndektsv4rrffq69g5fav"); // case-insensitive
auto b = ulid26::ulid::from_bytes(std::span{raw16});              // network byte order
```

### `std::string` without the extra copy

`to_string()` is the owning conversion: it allocates exactly once via C++23
`std::string::resize_and_overwrite`, with no default-fill pass and no intermediate copy. For the
zero-allocation path, `to_chars` writes the canonical form directly into caller-provided storage.

```cpp
ulid26::ulid id = ulid26::generate();

// Ready-made: one allocation, implemented with resize_and_overwrite.
std::string s = id.to_string();

// Client-controlled: your string, your allocator, no hidden extra copy.
std::string t;
t.resize_and_overwrite(ulid26::ulid::kTextSize,
    [&](char* p, std::size_t n) -> std::size_t {
        id.to_chars(std::span<char, ulid26::ulid::kTextSize>{
            p, ulid26::ulid::kTextSize});
        return ulid26::ulid::kTextSize;
    });
```

The same shape scales to a block: one `resize_and_overwrite(n * ulid::kTextSize, ...)` call
materializes any number of ids with no per-id allocation.

### Validation

```cpp
bool ok = ulid26::is_valid("01arz3ndektsv4rrffq69g5fav");
```

`is_valid` runs the scalar codec without materializing a value: malformed
input exits at the check, and the first-character overflow cap applies exactly
as in `decode`.

## Performance

Measured with the `bench/` suite (Google Benchmark, GCC 16, x86-64 Release;
per-operation times) on an Intel Core i5-8265U (Whiskey Lake, AVX2), pinned to
one logical CPU with the `performance` governor and Turbo disabled (1.6 GHz).
Reproduce with `nix run .#bench` (release matrix over `bench_generation`,
`bench_is_valid`, `bench_to_chars`, `bench_bytes` and `bench_streams`) using
the same CPU settings. Timings are data-independent: the text kernels run the
full pass with no early exit, so valid and malformed input cost the same.

Codec numbers (ns/ULID at 100k):

| operation | ours | reference |
|---|---|---|
| decode (validated) | 45.0 | 34.7 (validates nothing, uppercase-only) |
| encode | 23.5 | 34.7 |
| validate | 29.6 | N/A (no such API) |
| to/from bytes | 3.4 / 2.3 | 13.0 / 19.8 |

Fairness notes: reference decode performs no validation and folds no case (lowercase input decodes
to silent garbage), so its decode lead is the price of our spec-mandated rejection — and our
`is_valid` (29.6ns) is faster than its *unchecked* decode. Everywhere else the scalar core beats it.
The two implementations agree byte-exactly on 100k random values (verified at benchmark setup).

Accepted loss: validated decode. The gap is a contract gap, not a decoder-throughput gap: the
reference decoder assumes 26 valid uppercase characters, while ours validates length, alphabet,
case, range, and materializes either values or structured errors. We accept that cost rather than
add an unchecked public decoder.

| generation | ns/op |
|---|---|
| `generate()` | 133 |
| `unsynchronized_generator::next()` | 42.4 |
| `shared_generator::next()` (uncontended) | 98.7 |

Reservation + drain, ns/ULID:

| block | `unsynchronized_generator` | `shared_generator` |
|---|---|---|
| 1 | 46.0 | 101 |
| 16 | 4.0 | 7.4 |
| 64 | 2.0 | 2.7 |
| 256 | 1.5 | 1.7 |
| 1024 | 1.3 | 1.4 |

Reservations reduce the committing synchronization to one event per block; draining the returned
range is local and needs no further calls. The text codec is scalar throughout: direct shifts per
5-bit group on encode, table loop plus range cap on decode. The scalar `constexpr` codec doubles as
the reference for the codec differential tests.

## Spec conformance

- **Layout**: 16 octets, network byte order: 48-bit ms timestamp, then 80-bit entropy.
- **Canonical form**: 26 characters, `0123456789ABCDEFGHJKMNPQRSTVWXYZ`, left-most sorted first.
- **Case-insensitivity**: decode accepts any case; encode is uppercase (canonical).
- **Range**: values above `7ZZZZZZZZZZZZZZZZZZZZZZZZZ` are rejected (`errc::overflow`).
- **Aliasing**: `I L O U` (any case) are *not* part of the ULID alphabet and are rejected.
- **Monotonicity**: within the same millisecond the entropy is incremented with
  carrying; exhaustion fails explicitly. Across milliseconds sort order follows
  the timestamp, per spec.

## Development

CI workflows run through flake apps — the command below is exactly what CI runs, so
local and remote never drift — except `baseline` (apt toolchain) and the coverage
merge (pip gcovr), which need no Nix. Run from the repo root:

| workflow | command | what it does |
|---|---|---|
| `ci` | `nix run .#ci` | clang-format gate, then Release configure, build, test, package |
| `san` | `nix run .#san` | ASan/UBSan suite |
| `tsan` | `nix run .#tsan` | TSan suite |
| `cov` | `nix run .#cov` | gcov suite plus the Linux tracefile |
| `fuzz` | `nix run .#fuzz` | libFuzzer harness plus the deterministic seed-corpus gate |
| `bench` | `nix run .#bench` | Release benchmark matrix (ten repetitions, aggregates only; extra args forward to every suite; `ULID26_BENCH_CPU` pins CPUs, default 2, empty means unpinned) |

`nix develop` drops into the interactive shell carrying the same toolchain.

The test suite needs boost-ext/ut: `nix develop` provides it (nixpkgs `ut`);
Debian trixie+ ships `libboost-ext-ut-dev`, Conan has `boost-ext-ut/2.3.1`.
Otherwise the build downloads the pinned v2.3.1 header automatically
(network required) — or pass `-DULID26_BUILD_TESTS=OFF` to skip the suite.

## Design notes

- **Value purity**: the type is closed — no setters, no entropy access, no
  generation machinery. The monotonic state machine constructs values through a
  private tagged constructor; the arithmetic on entropy lives in the generators.
- **Representation**: private `uint64_t hi, lo` (`hi[63:16]` = timestamp,
  `hi[15:0] || lo` = entropy). Wire format is a serialization concern handled by
  `to_bytes`/`from_bytes`; no `__uint128_t` dependency (standard 128-bit
  integers are not in C++26 — P3140 withdrawn; `_BitInt` targets C++29).
- **Errors**: fallible surfaces return `std::expected` carrying a `std::error_code` in the `"ulid"`
  category, composable with any existing error-code codebase. Reservations instead report capacity
  through `reservation::size()`: a short block is success. Entropy acquisition may throw when the
  OS source fails.
- **Constexpr**: the value type and scalar text codec are constexpr; the formatter reuses the
  constexpr encode core. Generation and owning/string conversions are runtime-only by nature or by
  their standard-library dependencies.
- **Testing**: spec vectors, property round-trips, monotonic bursts and exhaustion,
  clock-regression fail-closed, reservation range/size contract, concurrency stress (global
  sortedness and uniqueness), TSAN on the mutex flavor, and ASAN/UBSAN everywhere.

## Repository layout

```
ulid26/value.hpp             value layer: errors, scalar codec, ulid, try_parse, hash, formatter
ulid26/generators.hpp        generation layer: entropy engines, one-shot and monotonic generation,
                             reservations
ulid26/ulid26.hpp            include-all umbrella over the two layers
src/                             (nothing — header-only)
test/                            constexpr, spec vectors, property, stress, generation/reservations
bench/                           Google Benchmark suites for the value codec and generation
```

The library is deliberately composed of two headers for include-what-you-use:

- `<ulid26/value.hpp>` for value, parsing, formatting, hashing, comparison and error codes (the minimal layer);
- `<ulid26/generators.hpp>` for generation, entropy engines and reservations (pulls `<mutex>`,
  `<random>`, `<chrono>` and `<ulid26/value.hpp>`); and
- `<ulid26/ulid26.hpp>` for the umbrella that includes both layers, for convenience.

Generation has no separate iterator adapter: a reservation *is* the range. Drain it with a
range-for, compose it with `std::views`, or take a new reservation when the block is used up.

## Fun fact

This library is named `ulid26` because it was supposed to require C++26: it started out of an
assumption that using `std::simd` would improve speed of some operations. That assumption didn't
survive the benchmarks. The name aligned well with the fact that the spec's canonical form is 26
characters long, so the name stuck, even though it only requires C++23.

## License

MIT — see [LICENSE](LICENSE); same terms as the reference implementations this library respectfully
derives and deviates from:
  * [suyash/ulid](https://github.com/suyash/ulid)
  * [ulid/javascript](https://github.com/ulid/javascript)
