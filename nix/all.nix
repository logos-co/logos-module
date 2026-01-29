# Builds logos-module library, CLI binary, and tests all together
{ pkgs, common, src }:

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
    
    runHook postInstall
  '';
  
  # Run tests during build to ensure they pass
  doCheck = true;
  checkPhase = let
    pluginExt = if pkgs.stdenv.isDarwin then "dylib" else "so";
  in ''
    runHook preCheck
    
    cd build
    # Set LM_BINARY environment variable for CLI tests (absolute path)
    export LM_BINARY="$(pwd)/bin/lm"
    # Set TEST_PLUGIN environment variable for real plugin tests (absolute path)
    export TEST_PLUGIN="$(pwd)/../tests/examples/package_manager_plugin.${pluginExt}"
    
    # Debug: verify the plugin exists
    if [ -f "$TEST_PLUGIN" ]; then
      echo "Test plugin found at: $TEST_PLUGIN"
    else
      echo "Warning: Test plugin not found at: $TEST_PLUGIN"
      ls -la ../tests/examples/ || echo "examples directory not found"
    fi
    
    ctest --output-on-failure
    cd ..
    
    runHook postCheck
  '';
}
