{
  description = "Logos Module Library - Qt plugin system abstraction layer";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";
  };

  outputs = { self, nixpkgs, logos-nix }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forAllSystems ({ pkgs }: 
        let
          # Common configuration
          common = import ./nix/default.nix { inherit pkgs; };
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
          
          # Additional aliases for the binary
          cli = bin;
          bin = bin;
          cmd = bin;

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

      checks = forAllSystems ({ pkgs }:
        let
          common = import ./nix/default.nix { inherit pkgs; };
          src = ./.;
        in {
          tests = import ./nix/all.nix { inherit pkgs common src; skipPluginTests = true; };
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
            pkgs.qt6.qtbase
            pkgs.gtest
          ];
          
          shellHook = ''
            echo "Logos Module development environment"
            echo "Build with tests: cmake -B build -DLOGOS_MODULE_BUILD_TESTS=ON && cmake --build build"
            echo "Run tests: cd build && ctest --output-on-failure"
          '';
        };
      });
    };
}
