{
  description = "Logos Module Library - Qt plugin system abstraction layer";

  inputs = {
    # Follow the same nixpkgs as logos-cpp-sdk to ensure compatibility
    nixpkgs.follows = "logos-cpp-sdk/nixpkgs";
    logos-cpp-sdk.url = "github:logos-co/logos-cpp-sdk";
  };

  outputs = { self, nixpkgs, logos-cpp-sdk }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
      # Capture QTDIR from host environment (requires --impure for nix build).
      # Import the path into the Nix store so it's accessible inside the sandbox.
      hostQtdir = builtins.getEnv "QTDIR";
      qtdirPath = if hostQtdir != ""
        then builtins.path { path = hostQtdir; name = "qtbase-external"; }
        else null;
    in
    {
      packages = forAllSystems ({ pkgs }:
        let
          common = import ./nix/default.nix {
            inherit pkgs;
            qtbase = if qtdirPath != null then qtdirPath else pkgs.qt6.qtbase;
          };
          src = ./.;

          # Binary package (lm CLI)
          bin = import ./nix/bin.nix { inherit pkgs common src; };

          # Library package
          lib = import ./nix/lib.nix { inherit pkgs common src; };

          # All-in-one package (library, CLI, and tests)
          all = import ./nix/all.nix { inherit pkgs common src; };

          # CI package: skips plugin tests (pre-compiled plugins have incompatible Nix store paths)
          ci = import ./nix/all.nix { inherit pkgs common src; skipPluginTests = true; };
        in
        {
          # lm binary package
          lm = bin;

          # logos-module library
          lib = lib;

          # All-in-one package with tests
          all = all;

          # CI package (skips plugin tests)
          ci = ci;

          # Default package (library)
          default = lib;
        }
      );

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.gtest
          ];

          shellHook = ''
            echo "Logos Module development environment"
            echo "QTDIR=''${QTDIR:-not set}"
            echo "Build with tests: cmake -B build -DLOGOS_MODULE_BUILD_TESTS=ON && cmake --build build"
            echo "Run tests: cd build && ctest --output-on-failure"
          '';
        };
      });
    };
}
