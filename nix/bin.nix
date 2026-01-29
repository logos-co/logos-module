# Builds the lm binary
{ pkgs, common, src }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-cli";
  version = common.version;
  
  inherit src;
  inherit (common) nativeBuildInputs buildInputs cmakeFlags meta;
  
  # Don't wrap Qt apps - this is a CLI tool that needs Qt for plugin loading
  dontWrapQtApps = true;
  
  configurePhase = ''
    runHook preConfigure
    
    cmake -S . -B build \
      -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
    
    runHook postConfigure
  '';
  
  buildPhase = ''
    runHook preBuild
    
    cmake --build build --target lm
    
    runHook postBuild
  '';
  
  installPhase = ''
    runHook preInstall
    
    mkdir -p $out/bin
    cp build/bin/lm $out/bin/
    
    runHook postInstall
  '';
}
