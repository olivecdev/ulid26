# ulid26

> Universally Unique Lexicographically Sortable Identifiers for C++23.

A spec-exact, thread-safe, constexpr-friendly implementation of the 
[ULID specification](https://github.com/ulid/spec) — the 26-character sort key — with
`std::expected`/`std::error_code` error handling for fallible value and batch APIs, no dependencies,
header-only. Entropy engines may throw while acquiring OS entropy.

```
C++23 | GCC 14+ | header-only | zero deps | constexpr | benchmarked
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
| Compilers | GCC 14+ (primary, incl. CAS tier; CI runs GCC 16) · Clang 22+ (mutex tier) · MSVC 19.36+ / VS 2022 17.6+ (CAS tier on x64, mutex elsewhere; Windows CI) · MinGW-w64 GCC 14+ (CAS tier expected; not run here) |
| Standard | C++23 (`-std=c++23`; MSVC maps it to `/std:c++latest`) |
| Windows | `cmake -S . -B build/win -G "Visual Studio 17 2022" -A x64` (presets pin Ninja); `bench_reference` is excluded there — the reference needs `__int128` |
| Dependencies | none |
| Link model | header-only; single CMake target `olivec::ulid26`, all headers installed |

## Quick start

### CMake with FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(
    ulid26
    GIT_REPOSITORY https://github.com/olivecdev/ulid26
    GIT_TAG        v0.0.1
)
FetchContent_MakeAvailable(ulid26)

target_link_libraries(myapp PRIVATE olivec::ulid26)
```

### First ULID

```cpp
#include <ulid26/ulid26.hpp>
#include <print>

int main() {
    auto id = ulid26::generate();          // thread-safe one-shot
    std::println("{}", id);                        // std::formatter prints canonical text
    // alternatively, with the old iostreams API:
    auto s  = id.to_string();                      // std::array<char, 26>, purposefully NOT NULL-terminated
    std::cout << std::string_view{s.data(), s.size()} << '\n';
}
```

## The value type `ulid`

`ulid26::ulid` is a pure value type: 16 bytes, trivially copyable, standard layout, fully
`constexpr`. It holds a 48-bit millisecond timestamp and 80 bits of entropy, and compares
lexicographically exactly as its canonical string does — `operator<=>` is the spec's byte order.

```cpp
namespace ulid26 {
class ulid {
public:
    static constexpr std::size_t string_length = 26; // canonical text length
    static constexpr std::size_t binary_length = 16; // binary length

    ulid();                                        // nil: 00000000000000000000000000

    std::uint64_t timestamp() const;                 // ms since Unix epoch (no <chrono>, but easily convertible to it)

    static std::expected<ulid, std::error_code> from_string(std::string_view);
                                                     // Crockford base32, case-insensitive
    static std::expected<ulid, std::error_code> from_bytes(std::span<const std::byte, 16>);
                                                     // network byte order
    std::array<char, 26> to_string() const;
    void to_chars(std::span<char, 26> out) const;    // non-allocating write (see below)
    std::string to_std_string() const;               // one allocation, via resize_and_overwrite
    std::array<std::byte, 16> to_bytes() const;

    friend std::strong_ordering operator<=>(ulid, ulid) = default; // == canonical string order
    // std::hash<ulid> and hash_value() provided
    // std::formatter<ulid> provided: std::format("{}", id) is canonical text
    // (any format spec is a compile-time error)
};
}
```

Default construction produces the nil value — compare with `u == ulid26::ulid{}`.

The value type, scalar codec and batch validation helpers are `constexpr`: the library ships
`static_assert`-driven specification tests, including round-trip proofs and malformed-input
rejection, evaluated at compile time. The public `from_string`/`from_bytes` wrap the scalar core to
produce `std::error_code`, which is not constexpr-constructible, so the compile-time entries are
separate names: `is_valid` answers validity of a literal, and `parse` parses one into a value,
reporting the same precise `errc` the runtime API maps to `std::error_code` (`from_string` cannot
overload on return type alone).

```cpp
static_assert(ulid26::is_valid("7ZZZZZZZZZZZZZZZZZZZZZZZZZ"));
static_assert(!ulid26::is_valid("80000000000000000000000000"));
static_assert(!ulid26::is_valid("01ARZ3NDEKTSV4RRFFQ69G5FA*"));
static_assert(ulid26::parse("01ARZ3NDEKTSV4RRFFQ69G5FAV")->timestamp() == 1469922850259ULL);
static_assert(ulid26::parse("80000000000000000000000000").error() == ulid26::errc::overflow);
```

## Errors

Failures use the standard error-code machinery (`std::errc`-style):

- fallible value-producing APIs (sources) return `std::expected<T, std::error_code>` and;
- sinks that fill user-provided storage return `std::error_code` directly - falsy on success.

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
Entropy-source failures are construction or draw failures and are propagated as exceptions.

## Generation

```cpp
// One-shot: fresh timestamp + fresh entropy. Safe to call from any thread.
auto id = ulid26::generate();

// Same, with an explicit engine (see Custom entropy engines below).
auto oid = ulid26::generate<ulid26::system_rng>();

// Bulk convenience: a plain loop over generate() — one clock read and one
// fresh entropy draw per id, no ordering promise, never fails.
// Performance-sensitive clients that need ordered bulk should opt for the monotonic_shared::next(n)
// below (~1ns per id in 64-blocks, strictly increasing, fail-closed).
std::array<ulid26::ulid, 64> batch;
ulid26::generate_n(batch);
```

### Monotonic generation

When several ULIDs are produced within the same millisecond, the monotonic generators increment the
previous entropy by one instead of drawing fresh randomness, guaranteeing strictly increasing
output and much faster generation. The monotonic generators are stateful and have two flavors:

```cpp
// Per-thread: zero synchronization, strictly increasing per thread.
ulid26::monotonic_local m;
auto a = m();    // -> std::expected<ulid, std::error_code>
auto b = m();    // b > a whenever the clock confirms the same or later millisecond

// A local generator owns the engine passed at construction. A seeded engine
// makes the entropy stream deterministic.
ulid26::monotonic_local<ulid26::default_rng> seeded{
    ulid26::default_rng{42}};

// Shared: strictly increasing across ALL threads.
ulid26::monotonic_shared g;
auto c = g();
```

### Custom entropy engines

Generators take any standard uniform random bit generator as their template parameter —
`std::mt19937_64` plugs in with zero glue code:

```cpp
ulid26::monotonic_local<std::mt19937_64> gen{std::mt19937_64{42}};
```

The in-house engines are `default_rng` (`ChaCha20`, seeded once per thread from the OS; ~28ns per
draw) and `system_rng` (`std::random_device` per draw; ~5µs, but OS-maintained and fork-safe). Two
warnings travel with the freedom: non-cryptographic engines (`mt19937_64`, and Philox — available as
`std::philox4x32` only under C++26) must never back ids that need unguessability, and an engine with
a fixed default seed yields identical streams on every thread under `monotonic_shared` (uniqueness
still holds — the shared counter sequences collisions — but the entropy correlates).

`monotonic_local` is non-copyable. Moving transfers the sequence and retires the source object: the
moved-from generator is reset and starts a fresh, re-seeded sequence if used again.
`monotonic_shared` is neither copyable nor movable; share it by reference.

State machine (both flavors):

| condition | action |
|---|---|
| `now > last_ts` | fresh entropy draw |
| `now == last_ts` | increment previous entropy by 1 |
| `now < last_ts` | fail closed: `errc::time_reversed`, state preserved |
| entropy at maximum | `errc::generation_overflow`, state preserved |

### Thread-safety guarantees

| flavor | synchronization | guarantee |
|---|---|---|
| `generate()` / `generate_n()` | none (thread-local entropy) | safe from any thread; no ordering promise within a millisecond (per spec) |
| `monotonic_local` | none | strict per-thread order; cross-thread order not guaranteed |
| `monotonic_shared` | lock-free CAS (x86-64) / mutex fallback | strict **global** order: emission order == value order |

The shared flavor uses a `cmpxchg16b` CAS on a 16-byte aligned state on x86-64 (inline asm on GCC,
`_InterlockedCompareExchange128` on MSVC), and a mutex-guarded implementation elsewhere. Plain
standard C++ doesn't guarantee lock-free atomic operations on 16-byte structures. Both
implementations pass the identical test suite and are required to exhibit identical observable
behavior (differential testing); the mutex flavor additionally validates under ThreadSanitizer. The
CAS tier takes a CAS-shaped state snapshot before each attempt, so the decision and committing CAS
use the same state. This preserves strict global order and disjoint reservations even when several
threads first use a freshly constructed shared generator in the same millisecond.

### Bulk reservations

High-ingestion pipelines can reserve blocks of ids at once, reducing the committing synchronization
to one event per block. A successful reservation behaves exactly **as if** all its ids were created
within the same millisecond — strictly increasing under one timestamp:

```cpp
ulid26::monotonic_shared g;
auto r = g.next(64);   // bulk next(): 64 ids, all strictly after everything ever issued
// ids handed out locally; emission order may invert within the reserved window
```

The range never spans milliseconds and never partially fills: a count that
does not fit the remainder of the current millisecond fails closed with
`errc::generation_overflow`, leaving generator state untouched.

Even if two threads call `next(64)` in the same millisecond, we guarantee that the two blocks are
disjoint and strictly increasing across threads. The shared state stores the last ULID of the
reserved block, not the first: once a thread succeeds in reserving a block, the next thread sees a
strictly higher last ULID and cannot overlap. The atomic (or mutex-based) protocol ensures no thread
can observe a partial or outdated state.

### Resuming after restart

A monotonic sequence can continue from a persisted id:

```cpp
auto m = ulid26::monotonic_local::resume(last_id_from_disk);
```

## Text codec

### Single values

```cpp
auto u = ulid26::ulid::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV");
auto v = ulid26::ulid::from_string("01arz3ndektsv4rrffq69g5fav"); // case-insensitive
auto b = ulid26::ulid::from_bytes(std::span{raw16});              // network byte order
```

### `std::string` without the extra copy

`to_string()` returns a fixed `array<char, 26>` (not null-terminated) — copying that into a
`std::string` costs a second pass. For the optimal path, `to_chars` writes the canonical form
directly into caller-provided storage, so it composes with `std::string::resize_and_overwrite`
(C++23): exactly one allocation, no default-fill, no intermediate copy and null-termination
guaranteed by the standard library.

```cpp
ulid26::ulid id = ulid26::generate();

// Ready-made: one allocation, implemented with resize_and_overwrite.
std::string s = id.to_std_string();

// Client-controlled: same cost, your string, your allocator.
std::string t;
t.resize_and_overwrite(ulid26::ulid::string_length,
    [&](char* p, std::size_t n) -> std::size_t {
        id.to_chars(std::span<char, ulid26::ulid::string_length>{
            p, ulid26::ulid::string_length});
        return ulid26::ulid::string_length;
    });
```

The same shape scales to batches: `encode(ids, chars)` takes
`chars.size() == 26 * ids.size()`, so a whole block of ids materializes
with one `resize_and_overwrite(n * ulid::string_length, ...)` call.

### Batches — three layers

```cpp
// 1. Fixed size, fully compile-time checked (M deduced from the span extent).
//    M must be a multiple of 26 — or it does not compile.
std::array<char, 104> text = /* 4 ULIDs */;
auto ids = ulid26::decode(std::span{text});      // array<ulid, 4>

// 2. Fixed size into caller-provided storage; both extents checked at compile time.
ulid26::decode_into(std::span{text}, std::span{out4});

// 3. Dynamic sizes (runtime-checked: in.size() == 26 * out.size()).
ulid26::decode(std::span{buf.data(), buf.size()}, std::span{out.data(), n});
ulid26::encode(std::span{ids.data(), n},          std::span{chars.data(), 26 * n});
```

### Validation

```cpp
bool ok = ulid26::is_valid("01arz3ndektsv4rrffq69g5fav");
```

`is_valid` runs the scalar codec without materializing a value: malformed
input exits at the check, and the first-character overflow cap applies exactly
as in `decode`. Batch `encode` fills caller storage with the same direct-shift
loop as `to_chars` (see Performance).

## Performance

Measured with the `bench/` suite (Google Benchmark, GCC 16, x86-64 Release;
per-ULID times) on an Intel Core i5-8265U (Whiskey Lake, AVX2). Reproduce with
`cmake --preset ci && cmake --build --preset ci-build`, then
`./build/ci/bench/bench_batch`, `./build/ci/bench/bench_reference` and
`./build/ci/bench/bench_ranges`.
Timings are data-independent: the text kernels run the full pass with no
early exit, so valid and malformed input cost the same.

Batch sweep, ours (ns/ULID at 1k, 10k and 100k ULIDs — L1-, L2- and
LLC-resident; flat across sizes, so the bottleneck is instruction count, not
cache). Hand-rolled scalar loops measure identically — the batch API wrapper
costs nothing over calling the core per element:

| operation | 1k | 10k | 100k |
|---|---|---|---|
| batch `decode` | 22.8 | 22.9 | 23.0 |
| batch `encode` | 9.2 | 9.3 | 9.4 |
| `is_valid` | 18.7 | 18.8 | 19.2 |

Against the reference C++ library (`suyash/ulid`, pinned commit `3a46362`,
`bench_reference.cpp`, ns/ULID at 100k):

| operation | ours | reference |
|---|---|---|
| decode blocks | 27.5 | 15.2 (validates nothing, uppercase-only) |
| decode each | 36.6 | 16.6 (ditto) |
| encode blocks | 9.8 | 15.1 |
| validate | 20.2 | N/A (no such API) |
| compare | 1.1 | 0.9 |
| timestamp | 0.8 | 2.5 |
| to/from bytes | 1.7 / 5.5 | 5.7 / 8.6 |

Fairness notes: reference decode performs no validation and folds no case (lowercase input decodes
to silent garbage), so its decode lead is the price of our spec-mandated rejection — and our
`is_valid` (20.2ns) costs barely more than its *unchecked* decode (15.2ns). Everywhere else the
scalar core matches or beats it. The two implementations agree byte-exactly on 100k random values
(verified at benchmark setup).

Accepted loss: validated decode. The `decode blocks` / `decode each` gaps are a contract gap, not a
decoder-throughput gap: the reference decoder assumes 26 valid uppercase characters, while ours
validates length, alphabet, case, range, and materializes either values or structured errors. We
accept that cost rather than add an unchecked public decoder. The residual `compare` gap (~0.2ns,
stable across repetitions) is not contractual either — and not worth chasing: a scratch probe of
defaulted-`<=>` `<` versus an explicit short-circuit `<` on identical data measured the defaulted
shape far faster (branchless vs ~50% mispredicts on random data), so `operator<=> = default` already
is the fast shape.

| generation | ns/op |
|---|---|
| `generate()` | ~72 |
| `monotonic_local::next()` | ~20 |
| `monotonic_shared::next()` (uncontended) | ~32 |

### View-vs-bulk contention (`bench_ranges.cpp`, ns/ULID, 64-id takes)

| shape | 1 thread | 2 | 4 | 8 |
|---|---|---|---|---|
| local view (no sync) | ~28 | — | — | — |
| shared view (snapshot + commit CAS per id) | ~45 | ~128 | ~409 | ~1107 |
| shared bulk (snapshot + commit CAS per 64) | ~1.1 | ~3.0 | ~6.0 | ~15.6 |

The shared CAS tier takes a state-snapshot CAS and a committing CAS per successful attempt. Bulk
reservations amortize that synchronization over a block and degrade much more gracefully under
contention. The harness discards runs that catch a stepped-back clock (`time_reversed` fail-closed)
instead of undercounting; this fired live during multithreaded runs on this TSC-clocked box — a live
demonstration that the fail-closed path works, and why the single-threaded clock looks monotonic
while 8 cores reading TSCs can momentarily disagree across a millisecond boundary.

The batch codec is scalar throughout: direct shifts per 5-bit group on encode, table loop plus range
cap on decode. The scalar `constexpr` codec doubles as the reference for the batch API differential
tests.

## Spec conformance

- **Layout**: 16 octets, network byte order: 48-bit ms timestamp, then 80-bit entropy.
- **Canonical form**: 26 characters, `0123456789ABCDEFGHJKMNPQRSTVWXYZ`, left-most sorted first.
- **Case-insensitivity**: decode accepts any case; encode is uppercase (canonical).
- **Range**: values above `7ZZZZZZZZZZZZZZZZZZZZZZZZZ` are rejected (`errc::overflow`).
- **Aliasing**: `I L O U` (any case) are *not* part of the ULID alphabet and are rejected.
- **Monotonicity**: within the same millisecond the entropy is incremented with
  carrying; exhaustion fails explicitly. Across milliseconds sort order follows
  the timestamp, per spec.

## Design notes

- **Value purity**: the type is closed — no setters, no entropy access, no
  generation machinery. The monotonic state machine constructs values through a
  private tagged constructor; the arithmetic on entropy lives in the generators.
- **Representation**: private `uint64_t hi, lo` (`hi[63:16]` = timestamp,
  `hi[15:0] || lo` = entropy). Wire format is a serialization concern handled by
  `to_bytes`/`from_bytes`; no `__uint128_t` dependency (standard 128-bit
  integers are not in C++26 — P3140 withdrawn; `_BitInt` targets C++29).
- **Errors**: fallible value and batch surfaces return `std::expected` carrying
  a `std::error_code` in the `"ulid"` category, composable with any existing
  error-code codebase. Entropy acquisition may throw when the OS source fails.
- **Constexpr**: the value type, scalar codec, batch validation helpers and
  formatter are constexpr; generation is runtime-only by nature (clock +
  entropy).
- **Testing**: spec vectors, property round-trips, monotonic bursts and
  exhaustion, clock-regression fail-closed, concurrency stress (global
  sortedness and uniqueness), TSAN on the mutex flavor, ASAN/UBSAN everywhere,
  and batch-vs-scalar differential testing on valid and malformed inputs.

## Repository layout

```
ulid26/value.hpp             value layer: errors, scalar codec, ulid, parse, hash, formatter
ulid26/ulid26.hpp            generator umbrella over value.hpp: entropy engines, one-shot and
                             monotonic generation, bulk reservations
ulid26/batch.hpp             batch text API (fixed/dynamic decode, encode, validation)
ulid26/ranges.hpp            iterator adapter: generators as input_ranges (target olivec::ulid26::ranges)
src/                             (nothing — header-only)
test/                            constexpr, spec vectors, property, stress, batch differential
bench/                           Google Benchmark suites (sweep + reference comparison)
LESSONS.md                       SIMD arc: theory, measurements, removal
```

A header only library purposefully composed of multiple headers for a good reason: include-what-you-use:

- `<ulid26/value.hpp>` for value, parsing, formatting, hashing, comparison and error codes (the minimal layer);
- `<ulid26/batch.hpp>` for bulk text APIs (decode, encode, validation, pulls `<ulid26/value.hpp>`);
- `<ulid26/generators.hpp>` for the monotonic generators and entropy engines (pulls `< mutex>`, `<atomic>`, `<random>`, `<chrono>` and `<ulid26/value.hpp>`); and
- `<ulid26/ranges.hpp>` for iterator adapters over generators (pulls `<ulid26/generators.hpp>`)
- `<ulid26/ulid26.hpp>` for the umbrella that includes all of the above, for convenience.

## Ranges API

The iterator adapter exposes any monotonic generator as a single-pass `std::ranges::input_range` of
`ulid` values, so generation composes with the standard pipeline. The view borrows the generator; on
failure the range ends and the error is parked in the generator (`last_error()`) — always
check it after a loop that can end on its own:

```cpp
#include <ulid26/ranges.hpp>

for (auto u : ulid26::ranges::values(g) | std::views::take(10))
    consume(u);
if (g.last_error()) /* ... */;
```

Contention contract: each increment is one `next()`. Pinned threads drain a `monotonic_local` view
(zero sync); migrating threads a `monotonic_shared` view. Concurrent multi-threaded draining of one
shared generator is correct but **expensive**: it costs a state snapshot plus a commit CAS per id —
high-ingestion pipelines should reserve `next(n)` blocks (one commit per block) and drain locally.
See `bench/bench_ranges.cpp` for the view-vs-bulk curves at 1–8 threads.

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
