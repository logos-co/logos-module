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
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      $cmakeFlags "''${cmakeFlagsArray[@]}"
    
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
    # Probe both names and FAIL if neither is there. Naming only the unsuffixed
    # binary meant a mingw build -- which links build/bin/lm.exe -- died here
    # with a bare "cp: cannot stat 'build/bin/lm'" even though the link step had
    # just succeeded.
    _lm=""
    for _cand in build/bin/lm build/bin/lm.exe; do
      if [ -f "$_cand" ]; then _lm="$_cand"; break; fi
    done
    if [ -z "$_lm" ]; then
      echo "Error: the lm binary was not produced by the build" >&2
      ls -la build/bin 2>&1 >&2 || echo "  (no build/bin directory)" >&2
      exit 1
    fi
    cp "$_lm" $out/bin/
    
    runHook postInstall
  '';
}
