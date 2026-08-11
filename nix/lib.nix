# Builds the logos-module library
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = common.pname;
  version = common.version;
  
  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta;
  
  # Don't wrap Qt apps since this is a library
  dontWrapQtApps = true;
  
  configurePhase = ''
    runHook preConfigure
    
    # $cmakeFlags / cmakeFlagsArray carry everything nixpkgs computed for this
    # platform. Dropping them is invisible natively but fatal when cross
    # compiling: without -DCMAKE_SYSTEM_NAME=Windows (and the cross compiler
    # settings) CMake believes it is targeting the build host, so find_package
    # (Threads) takes the POSIX branch, finds no pthread -- mingw here uses
    # mcfgthread -- and Qt6Config then fails with the misleading "Qt6 could not
    # be found because dependency Threads could not be found".
    cmake -S . -B build \
      -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      -DCMAKE_INSTALL_PREFIX=$out \
      $cmakeFlags "''${cmakeFlagsArray[@]}"
    
    runHook postConfigure
  '';
  
  buildPhase = ''
    runHook preBuild
    
    cmake --build build
    
    runHook postBuild
  '';
  
  installPhase = ''
    runHook preInstall
    
    cmake --install build
    
    runHook postInstall
  '';
}
