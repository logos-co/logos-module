# Builds logos-module library, CLI binary, and tests all together
# skipPluginTests: set to true in CI where pre-compiled plugins won't load
{ pkgs, common, src, skipPluginTests ? false }:

pkgs.stdenv.mkDerivation {
  pname = "${common.pname}-all";
  version = common.version;
  
  inherit src;
  inherit (common) nativeBuildInputs buildInputs meta;
  
  # Don't wrap Qt apps
  dontWrapQtApps = true;
  
  configurePhase = ''
    runHook preConfigure
    
    cmake -S . -B build \
      -GNinja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      -DCMAKE_INSTALL_PREFIX=$out \
      -DLOGOS_MODULE_BUILD_TESTS=ON
    
    runHook postConfigure
  '';
  
  buildPhase = ''
    runHook preBuild
    
    cmake --build build
    
    runHook postBuild
  '';
  
  installPhase = ''
    runHook preInstall
    
    # Install using CMake
    cmake --install build
    
    # Install CLI binary
    mkdir -p $out/bin
    cp build/bin/lm $out/bin/
    
    # Install test executable
    cp build/tests/logos_module_tests $out/bin/
    
    # Install test plugin files for post-install testing
    mkdir -p $out/share/logos-module/test-plugins
    cp tests/examples/*.so $out/share/logos-module/test-plugins/ 2>/dev/null || true
    cp tests/examples/*.dylib $out/share/logos-module/test-plugins/ 2>/dev/null || true
    
    runHook postInstall
  '';
  
  # Run tests during build to ensure they pass
  doCheck = true;
  checkPhase = let
    pluginExt = if pkgs.stdenv.isDarwin then "dylib" else "so";
    ctestCmd = if skipPluginTests
      then "ctest --output-on-failure --exclude-regex CLIPluginTest"
      else "ctest --output-on-failure";
  in ''
    runHook preCheck
    
    cd build
    export LM_BINARY="$(pwd)/bin/lm"
    export TEST_PLUGIN="$(pwd)/../tests/examples/package_manager_plugin.${pluginExt}"
    
    ${if skipPluginTests then ''
    echo "Skipping CLIPluginTest (pre-compiled plugins have incompatible Nix store paths)"
    '' else ""}
    ${ctestCmd}
    cd ..
    
    runHook postCheck
  '';
}
