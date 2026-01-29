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

Show plugin metadata:
```bash
lm metadata /path/to/plugin.dylib
lm metadata /path/to/plugin.dylib --json
```

Show plugin methods:
```bash
lm methods /path/to/plugin.dylib
lm methods /path/to/plugin.dylib --json
```

Help:
```bash
lm --help
lm --version
```

## Development

```bash
nix develop
```
