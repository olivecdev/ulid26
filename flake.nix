{
  description = "C++ environment for ulid26 with Nix";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      utils,
    }:
    utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };

        # Toolchain shared by every workflow app below (and by the interactive
        # devShell): CMake + Ninja drive the presets, GCC builds/tests/covers/
        # sanitizes/benchmarks, Clang builds the libFuzzer harness,
        # clang-format gates style, gcovr covers the suite, git serves
        # FetchContent's git fetches, sed normalizes the Windows tracefile,
        # taskset pins benchmark CPUs. The raw compiler package rides last:
        # the gcc wrapper provides cc/c++/gcc/g++ but no gcov, and gcovr
        # invokes plain `gcov` — without this it falls through to the host's
        # system gcov and chokes on the notes version. Last position keeps
        # the wrapper winning the compiler names.
        workflowTools = with pkgs; [
          llvmPackages_22.clang-tools
          llvmPackages_22.clang
          cmake
          gcovr
          gcc16
          git
          gnused
          ninja
          util-linux
          (lib.getBin pkgs.gcc16.cc)
        ];

        # One named workflow per CI job that runs under nix, plus bench:
        #   nix run .#ci | .#san | .#tsan | .#cov | .#fuzz | .#bench
        # Bodies are the exact commands CI used to spell out, so the local
        # invocation and the remote one are identical by construction — no
        # drift, no longhand. Apps execute in the invoking directory (the repo
        # root, locally and in CI) and carry only workflowTools: no
        # interactive shell, no extra parameters.
        workflows = {
          ci = ''
            ${ccEnv}
            ${cmakePackageEnv}
            # Style gate first: fail fast before compiling.
            set -euo pipefail
            clang-format --dry-run --Werror ulid26/ulid26.hpp ulid26/value.hpp ulid26/generators.hpp ulid26/batch.hpp ulid26/ranges.hpp test/value_test.cpp bench/bench_bytes.cpp bench/bench_is_valid.cpp bench/bench_streams.cpp bench/bench_to_chars.cpp fuzz/fuzz_text.cpp
            # Release configure, build, test, package: mirrors production.
            cmake --workflow --preset ci
          '';
          san = ''
            ${ccEnv}
            ${cmakePackageEnv}
            # Debug + ASan/UBSan suite (GCC/Clang only).
            set -euo pipefail
            cmake --workflow --preset san
          '';
          tsan = ''
            ${ccEnv}
            ${cmakePackageEnv}
            # Debug + TSan suite (GCC/Clang only).
            set -euo pipefail
            cmake --workflow --preset tsan
          '';
          cov = ''
            ${ccEnv}
            ${cmakePackageEnv}
            # Debug + gcov suite, then the Linux tracefile for the merge.
            set -euo pipefail
            cmake --workflow --preset cov
            gcovr --root . --object-directory build/cov --filter 'ulid26/.*' --json -o coverage-linux.json
          '';

          fuzz = ''
            # Clang + libFuzzer harness over the text codec, then the
            # deterministic seed-corpus gate: fixed seed, bounded runs,
            # per-unit timeout. Nonzero exit on crash, leak, timeout or OOM.
            set -euo pipefail
            cmake --workflow --preset fuzz
            ./build/fuzz/fuzz/fuzz_text fuzz/corpus -runs=1000000 -seed=42 -timeout=30
          '';
          bench = ''
            ${ccEnv}
            # Release benchmark matrix: configure + build through the bench
            # preset, then the four suites in session order with the blessed
            # defaults (ten repetitions, aggregates only). Extra arguments
            # forward to every binary, so filters and repetition overrides
            # apply matrix-wide; benchmark flags are last-wins. CPU pinning
            # via ULID26_BENCH_CPU (default 2); empty means unpinned.
            set -euo pipefail
            cmake --workflow --preset bench
            cpu="''${ULID26_BENCH_CPU:-2}"
            pin=()
            if [ -n "$cpu" ]; then
              pin=(taskset -c "$cpu")
            fi
            flags=(--benchmark_repetitions=10 --benchmark_report_aggregates_only=true "$@")
            "''${pin[@]}" ./build/bench/bench/bench_is_valid "''${flags[@]}"
            "''${pin[@]}" ./build/bench/bench/bench_to_chars "''${flags[@]}"
            "''${pin[@]}" ./build/bench/bench/bench_bytes "''${flags[@]}"
            "''${pin[@]}" ./build/bench/bench/bench_streams "''${flags[@]}"
          '';
        };

        # Deterministic compiler choice: plain `nix run` (unlike `nix develop`)
        # leaves CC/CXX unset, so a fresh CMake configure takes whichever
        # compiler sorts first on PATH — here clang++, via the wrapper's
        # generic cc/c++ names. That silently retargets GCC-tuned builds and
        # breaks gcov (LLVM notes under GNU gcov). Pin GCC for every app that
        # compiles, so fresh configures resolve identically everywhere. fuzz
        # pins clang++ in its preset instead.
        ccEnv = ''
          export CC=gcc CXX=g++
        '';

        # CMake packages from Nix (ut) are libraries, not tools: apps only
        # put runtimeInputs on PATH, which find_package never searches. Export
        # the prefix explicitly so `nix run` resolves system packages exactly
        # like `nix develop` does — otherwise shell and apps would validate
        # against different ut copies.
        cmakePackageEnv = ''
          export CMAKE_PREFIX_PATH="${pkgs.ut}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
        '';

        mkWorkflow =
          name: text:
          pkgs.writeShellApplication {
            name = "ulid26-${name}";
            runtimeInputs = workflowTools;
            inherit text;
          };
      in
      {
        apps = builtins.mapAttrs (name: text: {
          type = "app";
          program = "${mkWorkflow name text}/bin/ulid26-${name}";
        }) workflows;

        devShells.default = pkgs.mkShell {
          # Interactive development gets everything the workflows carry, plus
          # debug and hygiene tools that CI never invokes.
          nativeBuildInputs =
            workflowTools
            ++ (with pkgs; [
              cppcheck
              gdb
              nixfmt
              perf
            ]);

          # Development and runtime libraries: ut provides Boost::ut for the
          # test suite via find_package, so nix builds test with no download.
          buildInputs = with pkgs; [
            ut
          ];

          # mkShell aggregates no CMAKE_PREFIX_PATH from inputs (verified
          # empty), so export Nix-provided CMake packages explicitly — same
          # as cmakePackageEnv does for the apps.
          shellHook = ''
            export CMAKE_PREFIX_PATH="${pkgs.ut}''${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
            echo "Welcome to the ulid26 development shell!"
          '';
        };

        # 32-bit portability gate: a real i686 ABI (32-bit size_t) running
        # on the 64-bit kernel, so the same -Werror warning set that guards
        # the 64-bit builds also rejects any 64-bit assumption in the
        # headers. Tests only: they include every header, and benchmarks
        # would only re-prove codegen.
        devShells.i686 = pkgs.pkgsi686Linux.mkShell {
          nativeBuildInputs = with pkgs.pkgsi686Linux; [
            cmake
            gcc
            git
            ninja
          ];
        };
      }
    );
}
