# ulid26

> Universally Unique Lexicographically Sortable Identifiers for C++23.

A spec-exact, thread-safe, constexpr-friendly implementation of the 
[ULID specification](https://github.com/ulid/spec) — the 26-character sort key — with
`std::expected`/`std::error_code` error handling for fallible value APIs, no dependencies,
header-only. Entropy engines may throw while acquiring OS entropy.

```
C++23 |  header-only | zero deps | constexpr
```

## Principles

- Header-only, no dependencies
- Easy to use, hard to misuse: convenience should not shadow correctness or performance.
- Thread-safe, lock-free where possible
- Performance is a first-class concern, but correctness and spec conformance are paramount
- Portable: tested on Linux and Windows, but should work anywhere a C++23 compiler is available
- CMake exemplar: declarative, modern, easy to import into any CMake project or package manager
- Sensible compilation design: separate headers for include-what-you-use, or a single umbrella
  header for convenience.

## Rules

- clang-format and clang-tidy are the gatekeepers: all code must pass them
- Readability means less indentation: early returns are a must and the happy path should use less
  indentation than the error path.
- Exception transparency: Never throw an exception, but do not catch any either.
- Do not pessimize readability nor performance.
- Prefer parameter structs when adjacent parameters have the same types or the function has more
  than 4 parameters.
- Preprocessor macros only allowable for conditional compilation and include guards, never for
  constants or inline code.
- Comments discipline: by default do not add any comments. They most often encourage or justify
  workarounds due bad desing. When urging to comment a chunk of code, challenge yourself for whether
  a missing named function is latent there or whether your design is simply not good enough. The
  only allowed exceptions are:
    1. SPDX/license and branding headers.
    2. Tool directives: header guards, `NOLINT*`, `clang-format off/on` - sparingly.
    3. Public API contract comments (`///`), concise, only for behavior users must know.
       Full context lives at the declaration, not in a parallel API page.
    4. `// constraints:` markers: state compiler/library/version and the removal condition (`Remove when
       ...` on one line). No comment may touch a constraints block.
    5. `/*identifier*/` unnamed-parameter tags and `// namespace X` closers.
    6. Short `//` implementation notes, for non-obvious code. Never narration, never history.

Forbidden:

- Issue/session/PR IDs.
- "old", "former", "used to", "as before", "historical" narratives.
- Comments that restate facts clearly readable in the adjacent code.
- Test assertion narration; rename the test instead.
- Description of a code block smelling like a latent helper function that needs a name.
- Performance claims or documentation cost warnings without current benchmarks and stated
  methodology.
- `//` comments in C++ sources under `test/` and `fuzz/` (intent lives in names, split cases, helpers,
  and assertion structure; mechanical `// namespace` closers from clang-format are exempt). `//`
  inside a string literal is data, not a comment.

## Engineering

- **Portability is proven.** Compile public changes with GCC, Clang, and MSVC
  `/W4 /WX /permissive- /std:c++latest`. Online compiler checks are preflight only; the actual
  Windows CI job is authoritative.
- **Layout changes include portability.** Timestamp/entropy layout, iterator, and range changes
  should consider 32-bit targets and host-endian assumptions, not only x86-64 Linux.
  32-bit and MinGW-w64 are best-effort with no CI gate.
- **Warnings are a test-only gate.** Strict flags belong on test, example, and fuzz executables and never
  leak through the installed `INTERFACE` target. Avoid `-Weverything`; maintain an explicit set.
- **Configuration is compile-time.** Library toggles use `ULID26_*` macros exposed through
  documented CMake options and propagated as `INTERFACE` usage requirements. Toggles default off,
  every preprocessor branch must compile, and `getenv` is forbidden.
- **Sanitizer claims are conditional.** Say validated under TSan/ASan only when the command ran.
  If a sanitizer runtime fails, prove the toolchain issue with a minimal `std::atomic` repro and
  keep production code unchanged. TSan builds use `ULID26_FORCE_MUTEX`.
- **Platform workarounds stay narrow.** A known stdlib/runtime failure may justify a named,
  narrowly scoped skip; never gate a whole suite or hide a general failure behind `#ifdef`.
- **Documentation is executable.** Every behavioral claim maps to code or a named test; examples
  compile as written; AsciiDoc renders warning-clean; retired macros and paths are removed.
- **Performance claims expire.** Cost warnings and comparisons require current Release benchmarks
  with stated methodology; stale, Debug, or unpinned numbers are not evidence.
- **Tests are behavior, registered everywhere.** Avoid tautologies and implementation coupling;
  use checked `expected` accessors; cover every public `errc`; register new targets in CMake, CI,
  coverage, fuzz, and package checks.
- **Clock-sensitive tests are deterministic.** Use bounded retries and explicit observed
  conditions rather than assuming a fixed sleep proves ordering.
- **Renames sweep the repository.** Search for old paths, macros, targets, archive lists, docs,
  and scripts; update them in the same change.

## Development

CI workflows run through flake apps — the command below is exactly what CI runs, so
local and remote never drift — except `baseline` (apt toolchain) and the coverage
merge (pip gcovr), which need no Nix. Run from the repo root:

| workflow | command | what it does |
|---|---|---|
| `ci` | `nix run .#ci` | clang-format and clang-tidy gates, then Release configure, build, test, package |
| `san` | `nix run .#san` | ASan/UBSan suite |
| `tsan` | `nix run .#tsan` | TSan suite |
| `cov` | `nix run .#cov` | gcov suite plus the Linux tracefile |
| `fuzz` | `nix run .#fuzz` | libFuzzer harness plus the deterministic seed-corpus gate |
| `bench` | `nix run .#bench` | Release benchmark matrix (ten repetitions, aggregates only; extra args forward to every suite; `ULID26_BENCH_CPU` pins CPUs, default 2, empty means unpinned) |

The `ci` gate formats all sources, then runs clang-tidy over the test translation units (which
include every library header); benchmark, example, and fuzz sources stay out of clang-tidy.

CI job count is a budget: merge steps into existing jobs when Nix setup overhead exceeds the work,
but keep local and CI commands on the same flake apps.

`nix develop` drops into the interactive shell carrying the same toolchain.

The test suite needs boost-ext/ut: `nix develop` provides it (nixpkgs `ut`); Debian trixie+ ships
`libboost-ext-ut-dev`, Conan has `boost-ext-ut/2.3.1`. Otherwise the build tries three mirrors for
the pinned v2.3.1 header (network required) and skips the tests with a warning if all are
unreachable; `-DULID26_REQUIRE_UT=ON` (set by Windows CI) turns that skip into a configure error.
Packager builds must work offline: the dependency
is optional and externally supplyable, and the FetchContent fallback must not become mandatory.

Benchmarks run Release builds only, with deterministic inputs, a pinned CPU, the `performance`
governor, Turbo disabled, ten repetitions, and aggregate output. Publish numbers only from such a
run.

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
  sortedness and uniqueness), the RFC 8439 ChaCha20 block vector, TSAN on the mutex flavor, and
  ASAN/UBSAN everywhere.

## Spec conformance

- **Layout**: 16 octets, network byte order: 48-bit ms timestamp, then 80-bit entropy.
- **Canonical form**: 26 characters, `0123456789ABCDEFGHJKMNPQRSTVWXYZ`, left-most sorted first.
- **Case-insensitivity**: decode accepts any case; encode is uppercase (canonical).
- **Range**: values above `7ZZZZZZZZZZZZZZZZZZZZZZZZZ` are rejected (`errc::overflow`).
- **Aliasing**: `I L O U` (any case) are *not* part of the ULID alphabet and are rejected.
- **Monotonicity**: within the same millisecond the entropy is incremented with
  carrying; exhaustion fails explicitly. Across milliseconds sort order follows
  the timestamp, per spec.

## Repository layout

```
ulid26/value.hpp             value layer: errors, scalar codec, ulid, try_parse, hash, formatter
ulid26/generators.hpp        generation layer: entropy engines, one-shot and monotonic generation,
                             reservations
ulid26/ulid26.hpp            include-all umbrella over the two layers
src/                             (nothing — header-only)
test/                            constexpr, spec vectors, property, stress, generation/reservations
examples/                        complete end-to-end programs, compiled and run by CI
bench/                           Google Benchmark suites for the value codec and generation
```

The library is deliberately composed of two headers for include-what-you-use:

- `<ulid26/value.hpp>` for value, parsing, formatting, hashing, comparison and error codes (the minimal layer);
- `<ulid26/generators.hpp>` for generation, entropy engines and reservations (pulls `<mutex>`,
  `<random>`, `<chrono>` and `<ulid26/value.hpp>`); and
- `<ulid26/ulid26.hpp>` for the umbrella that includes both layers, for convenience.

Generation has no separate iterator adapter: a reservation *is* the range. Drain it with a
range-for, compose it with `std::views`, or take a new reservation when the block is used up.
