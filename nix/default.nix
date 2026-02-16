# Common build configuration shared across all packages
{ pkgs, qtbase ? pkgs.qt6.qtbase }:

{
  pname = "logos-module";
  version = "0.1.0";

  # Common native build inputs
  nativeBuildInputs = [
    pkgs.cmake
    pkgs.ninja
    pkgs.pkg-config
  ];

  # Common runtime dependencies
  buildInputs = [
    pkgs.gtest
  ];

  # Point CMake at the prebuilt Qt
  QTDIR = qtbase;

  # Common CMake flags
  cmakeFlags = [
    "-GNinja"
  ];

  # Metadata
  meta = with pkgs.lib; {
    description = "Logos Module Library - Qt plugin system abstraction layer";
    platforms = platforms.unix;
  };
}
