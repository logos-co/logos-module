{
  description = "Logos Module Library - Qt plugin system abstraction layer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      systems = [ "aarch64-darwin" "x86_64-darwin" "aarch64-linux" "x86_64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f {
        pkgs = import nixpkgs { inherit system; };
      });
    in
    {
      packages = forAllSystems ({ pkgs }: {
        default = pkgs.stdenv.mkDerivation {
          pname = "logos-module";
          version = "0.1.0";
          
          src = ./.;
          
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          
          buildInputs = [
            pkgs.qt6.qtbase
          ];
          
          cmakeFlags = [
            "-GNinja"
          ];
          
          # Don't wrap Qt apps since this is a library
          dontWrapQtApps = true;
          
          meta = with pkgs.lib; {
            description = "Logos Module Library - Qt plugin system abstraction layer";
            platforms = platforms.unix;
          };
        };
      });

      devShells = forAllSystems ({ pkgs }: {
        default = pkgs.mkShell {
          nativeBuildInputs = [
            pkgs.cmake
            pkgs.ninja
            pkgs.pkg-config
          ];
          buildInputs = [
            pkgs.qt6.qtbase
          ];
        };
      });
    };
}
