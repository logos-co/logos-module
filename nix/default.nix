# Common build configuration shared across all packages
#
# logosPackage is the lgx shared library + headers. Required: the
# installed-directory checks are logos-package's, and a build that could not
# reach them would have to answer "unknown" to every one of them.
{ pkgs, logosPackage }:

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
    pkgs.qt6.qtbase
    pkgs.gtest
    logosPackage
  ];
  
  # Common CMake flags
  cmakeFlags = (pkgs.logosQtCrossCmakeFlags or [ ]) ++ [ 
    "-GNinja"
    "-DLOGOS_PACKAGE_ROOT=${logosPackage}"
  ];
  
  # Metadata
  meta = with pkgs.lib; {
    description = "Logos Module Library - Qt plugin system abstraction layer";
    platforms = platforms.unix ++ platforms.windows;
  };
}
