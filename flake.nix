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
      in
      {
        devShells.default = pkgs.mkShell {
          # Development time executables
          nativeBuildInputs = with pkgs; [
            llvmPackages_22.clang-tools
            llvmPackages_22.clang
            cmake
            cppcheck
            gcovr
            gdb
            gcc16
            git
            ninja
            nixfmt
            perf
          ];

          # Development and runtime libraries
          buildInputs = with pkgs; [
            # none for now.
          ];

          shellHook = ''
            export CPM_USE_LOCAL_PACKAGES=1
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

          shellHook = ''
            export CPM_USE_LOCAL_PACKAGES=1
          '';
        };
      }
    );
}
