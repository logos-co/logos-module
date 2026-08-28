{
  description = "Logos Module Library - Qt plugin system abstraction layer";

  inputs = {
    logos-nix.url = "github:logos-co/logos-nix";
    nixpkgs.follows = "logos-nix/nixpkgs";

    # The installed-package checks are logos-package's, reached through its C
    # ABI. The direction is forced: logos-package is Qt-free and this library
    # is not, so only this edge can exist.
    logos-package.url = "github:logos-co/logos-package";
    logos-package.inputs.logos-nix.follows = "logos-nix";
  };

  outputs = { self, nixpkgs, logos-nix, logos-package }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      # "x86_64-windows" pseudo-system; realises on x86_64-linux.
      packages = logos-nix.lib.forAllTargets ({ pkgs, system, ... }: 
        let
          # Common configuration
          common = import ./nix/default.nix {
            inherit pkgs;
            logosPackage = logos-package.packages.${system}.lib;
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
          common = import ./nix/default.nix {
            inherit pkgs;
            logosPackage = logos-package.packages.${pkgs.system}.lib;
          };
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
            logos-package.packages.${pkgs.system}.lib
          ];

          LOGOS_PACKAGE_ROOT = "${logos-package.packages.${pkgs.system}.lib}";

          shellHook = ''
            echo "Logos Module development environment"
            echo "Build with tests: cmake -B build -DLOGOS_MODULE_BUILD_TESTS=ON && cmake --build build"
            echo "Run tests: cd build && ctest --output-on-failure"
          '';
        };
      });
    };
}
