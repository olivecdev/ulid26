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
          ];

          # Development and runtime libraries
          buildInputs = with pkgs; [
	    # none for now.
          ];

          shellHook = ''
            export CPM_USE_LOCAL_PACKAGES=1
            echo "Welcome to the reopts development shell!"
          '';
        };
      }
    );
}
