# Logos Module Library

Library and Command line library to abstract & interact with logos modules.

## Building

### Library

```bash
nix build .#lib
# Output: result/lib/liblogos_module.a
# Headers: result/include/module_lib/
```

### CLI Binary

```bash
nix build .#lm
# Output: result/bin/lm
```

### Tests

```bash
nix build .#all
# Runs all tests during build
# Output: result/bin/logos_module_tests

# Run tests manually
./result/bin/logos_module_tests
```

## Using the `lm` Binary

Show both metadata and methods (default):
```bash
lm /path/to/plugin.dylib
lm /path/to/plugin.dylib --json
lm /path/to/plugin.dylib --debug
```

Show plugin metadata only:
```bash
lm metadata /path/to/plugin.dylib
lm metadata /path/to/plugin.dylib --json
```

Show plugin methods only:
```bash
lm methods /path/to/plugin.dylib
lm methods /path/to/plugin.dylib --json
```

Help:
```bash
lm --help
lm --version
```

## Instance Persistence

The library includes `ModuleLib::InstancePersistence`, a utility for assigning each module instance a unique ID and a dedicated persistence directory on disk.

### Directory structure

```
{basePath}/{moduleName}/{instanceId}
```

### API

```cpp
#include <instance_persistence.h>

using namespace ModuleLib::InstancePersistence;

// Three resolution modes:
//   ReuseOrCreate — pick the first existing instance dir, or create a new one
//   AlwaysCreate  — always generate a fresh instance ID
//   UseExplicit   — use a caller-provided instance ID

// Default: ReuseOrCreate
auto info = resolveInstance("/data", "my_module");

// Force a new instance
auto fresh = resolveInstance("/data", "my_module", ResolveMode::AlwaysCreate);

// Use a specific ID
auto exact = resolveInstance("/data", "my_module", ResolveMode::UseExplicit, "abc123");

// info.instanceId       — 12-char hex string (e.g. "a1b2c3d4e5f6")
// info.persistencePath  — full path: /data/my_module/a1b2c3d4e5f6
```

The directory is created on disk automatically. Returns empty strings on failure.

## Development

```bash
nix develop
```
