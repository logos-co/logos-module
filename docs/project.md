# Logos Module — Project Description

## Overview

`logos-module` is a small C++17 / Qt 6 abstraction layer over Qt's plugin
system, shipped as a static library (`liblogos_module.a`) plus the `lm`
command-line inspector. Its single job is to let the rest of the Logos
platform **load and introspect modules** — Qt plugins (`.so` / `.dylib` /
`.dll`) carrying embedded JSON metadata — without touching `QPluginLoader`,
`QMetaObject`, or `QMetaMethod` directly. The umbrella header `module_lib.h`
states the intent explicitly: provide an abstraction that could in principle
be re-backed by a different plugin technology while keeping consumer code
unchanged.

It lives in the platform's `core` group (alongside `logos-cpp-sdk`,
`logos-liblogos`, and `logos-module-builder`) and is a foundational, leaf-level
dependency — it depends only on Qt 6 Core (via the `logos-nix` pin) and is
consumed upward by several repos:

```
                         nixpkgs / Qt 6 (pinned via logos-nix)
                                      |
                                      v
                               logos-module
                          (liblogos_module.a + lm)
            ┌──────────────┬───────────┴───────────┬───────────────┐
            v              v                        v               v
     logos-liblogos  logos-module-builder    logos-plugin-qt   logos-basecamp
   (reads a module's        (scaffolds /        (Qt plugin-build   (GUI shell;
    name / deps /            builds modules)      backend the        module mgmt)
    metadata at the                               builder delegates
    protocol gate)                                to)
```

Concretely, `logos-liblogos`'s core protocol gate and the package-management
tooling rely on being able to read a module's embedded metadata (name, version,
type, dependencies, `logos_protocol_version`) and its callable interface
without fully loading the plugin. The library exposes that capability through
two value/handle types and one persistence utility; the `lm` CLI is a thin
front-end over them.

The library has three pieces:

- **`ModuleLib::ModuleMetadata`** extracts the JSON metadata Qt embeds in a
  plugin binary (the inner `MetaData` object) without fully loading the
  plugin, surfacing name / version / description / author / type /
  dependencies plus the full raw JSON — as both a `QJsonObject` and a Qt-free
  `std::string` (`rawMetadataJson`) for liblogos's protocol gate.
- **`ModuleLib::LogosModule`** is a move-only RAII handle around a loaded
  plugin. It owns the `QPluginLoader`, supports type-safe casting via
  `as<T>()`, and introspects methods and events. Introspection has two code
  paths: legacy Qt plugins are walked via `QMetaObject` / `QMetaMethod`, while
  new-API plugins implementing `LogosProviderPlugin` are queried through their
  `createProviderObject()->getMethods()`, which returns the full interface
  (methods AND events, each tagged with a `type` of `"method"` or `"event"`);
  the library splits these by type. A broken provider (null provider object)
  cleanly falls back to the `QMetaObject` path.
- **`ModuleLib::InstancePersistence::resolveInstance`** assigns each module
  instance a 12-hex-char ID and a dedicated on-disk directory
  `{basePath}/{moduleName}/{instanceId}`, with three resolution modes and
  path-traversal protection.

## Project Structure

```
logos-module/
├── CMakeLists.txt                    # Root build: logos_module STATIC lib,
│                                     #   install rules, adds cmd/, gates tests
├── README.md                         # Build + lm usage + InstancePersistence docs
├── flake.nix                         # Flake: packages lib/lm/all/ci, checks, devShell
├── flake.lock
├── LICENSE-APACHE-v2 / LICENSE-MIT   # Dual license
├── src/
│   ├── module_lib.h                  # Umbrella header (includes the two below)
│   ├── module_metadata.h / .cpp      # ModuleMetadata + fromPath/fromJson/
│   │                                 #   fromCustomMetadata parsers
│   ├── logos_module.h / .cpp         # LogosModule RAII handle, ParameterInfo,
│   │                                 #   MethodInfo, method + event introspection
│   ├── logos_provider_plugin.h       # Header-only mirror of the cpp-sdk
│   │                                 #   LogosProviderObject / LogosProviderPlugin
│   │                                 #   vtable (new-API detection)
│   ├── interface.h                   # Legacy PluginInterface base class
│   │                                 #   (Q_DECLARE_INTERFACE; #include "logos_api.h")
│   └── instance_persistence.h / .cpp # resolveInstance() + ResolveMode +
│                                     #   InstanceInfo / StdInstanceInfo
├── cmd/
│   ├── main.cpp                      # The `lm` CLI (arg parsing, subcommand
│   │                                 #   dispatch, JSON/human rendering, Qt
│   │                                 #   message suppression)
│   └── CMakeLists.txt                # `lm` executable target
├── nix/
│   ├── default.nix                   # Common config (pname/version, deps, -GNinja)
│   ├── lib.nix                       # Static library + headers derivation
│   ├── bin.nix                       # `lm` binary only (target lm, dontWrapQtApps)
│   └── all.nix                       # lib + CLI + tests, runs ctest in checkPhase
│                                     #   (skipPluginTests toggle for CI)
├── tests/
│   ├── CMakeLists.txt                # logos_module_tests gtest target
│   ├── test_metadata.cpp             # Metadata parsing + getModuleName/Deps +
│   │                                 #   std::string overloads
│   ├── test_introspection.cpp        # Legacy QMetaObject + new-API provider
│   │                                 #   introspection (methods, events, fallback)
│   ├── test_instance_persistence.cpp # All ResolveMode behaviors + path-traversal
│   ├── test_cli.cpp                  # End-to-end CLI tests (CLITest / CLIPluginTest)
│   └── examples/
│       ├── package_manager_plugin.so    # Prebuilt test plugin (name=package_manager,
│       └── package_manager_plugin.dylib #   version=1.0.0, type=core, author=Logos Core Team)
├── docs/
│   ├── index.md
│   └── project.md                    # This document
└── .github/workflows/ci.yml          # nix build .#ci, then run gtest excluding CLIPluginTest.*
```

## Technology Stack

| Component | Type | Purpose |
|-----------|------|---------|
| **C++17** | Language | Implementation language |
| **Qt 6 Core** (`pkgs.qt6.qtbase`) | Framework | `QPluginLoader` (plugin loading + embedded metadata), `QMetaObject` / `QMetaMethod` (introspection), `QJson*`, `QUuid` (instance IDs), `QDir` / `QFileInfo` (persistence). Linked `PUBLIC` into `logos_module` |
| **CMake** (≥ 3.16) | Build system | `AUTOMOC` on; Ninja generator (`-GNinja`) |
| **GoogleTest** (`pkgs.gtest`) | Test framework | Unit + integration tests; found via `find_package(GTest QUIET)`, falls back to `FetchContent` (tag `v1.14.0`) |
| **Nix flakes** | Build / packaging | Reproducible builds; flake outputs `lib` / `lm` / `all` / `ci` |
| **pkg-config** | Build tool | Native build input |

### Dependencies

| Dependency | Type | Purpose |
|------------|------|---------|
| **[logos-nix](https://github.com/logos-co/logos-nix)** | Flake input | Supplies the pinned nixpkgs — the flake declares `nixpkgs.follows = "logos-nix/nixpkgs"` |
| **nixpkgs** (follows `logos-nix`) | Flake input | Package set for cmake / ninja / pkg-config / qt6 / gtest |
| **`logos_api.h`** | External header (not vendored) | Included by `src/interface.h` for the legacy `LogosAPI*` pointer; supplied by `logos-liblogos` / `logos-cpp-sdk` at the consumer's build time. `interface.h` is not part of `logos_module`'s compiled sources, so the library builds standalone |
| **`logos-cpp-sdk` `logos_provider_object.h`** | ABI contract (not a build dep) | `src/logos_provider_plugin.h` must mirror its vtable layout exactly so `lm` can introspect new-API modules |

## Components

### ModuleMetadata

**Files:** `src/module_metadata.h`, `src/module_metadata.cpp`

A value type (`struct`) holding a module's parsed metadata, plus the static
parsers that read Qt's embedded plugin metadata. `isValid()` returns true iff
`name` is non-empty.

**Fields:**

| Field | Type | Notes |
|-------|------|-------|
| `name`, `version`, `description`, `author`, `type` | `QString` | Core metadata fields |
| `dependencies` | `QStringList` | Declared module dependency names |
| `rawMetadata` | `QJsonObject` | The full inner `MetaData` object (e.g. for reading `logos_protocol_version`) |
| `rawMetadataJson` | `std::string` | Same metadata as a compact JSON string so Qt-free consumers parse it without QJson |

**API:**

| Method | Description |
|--------|-------------|
| `bool isValid() const` | True if the metadata has at least a non-empty `name` |
| `static std::optional<ModuleMetadata> fromPath(const QString&)` *(+ `std::string` overload)* | Read embedded plugin metadata via `QPluginLoader::metaData()` **without loading** the plugin. `nullopt` on failure |
| `static std::optional<ModuleMetadata> fromJson(const QJsonObject&)` | Parse the full Qt plugin metadata object (expects an inner `MetaData` object). `nullopt` if no `MetaData` section or invalid |
| `static ModuleMetadata fromCustomMetadata(const QJsonObject&)` | Parse the inner `MetaData` object directly (also captures `rawMetadata` + `rawMetadataJson`). May be invalid if `name` is missing |

### LogosModule

**Files:** `src/logos_module.h`, `src/logos_module.cpp`

A move-only RAII handle that owns a loaded plugin (a `QPluginLoader`, the
`QObject` instance, and its parsed `ModuleMetadata`). Non-copyable; the
destructor calls `unload()`, which destroys the loader unless the module was
marked static (`wrapExisting` / `getStaticModules` / after `release()`).

**Supporting types** (same header):

- `struct ParameterInfo { QString name; QString type; QJsonObject toJson() const; }`
- `struct MethodInfo { QString name, signature, returnType, description; bool isInvokable = false; std::vector<ParameterInfo> parameters; QJsonObject toJson() const; }`
  — `toJson()` omits an empty `description` and an empty `parameters` array.

**Loading / lifecycle:**

| Method | Description |
|--------|-------------|
| `static LogosModule loadFromPath(const QString& path, QString* err = nullptr)` *(+ `std::string` / `std::string*` overload)* | Load a plugin from a file path; check `isValid()`. On failure sets `err`, logs a warning, and deletes the loader |
| `static std::vector<LogosModule> getStaticModules()` | Wrap all statically linked (`Q_IMPORT_PLUGIN`) plugins from `QPluginLoader::staticInstances()` |
| `static LogosModule wrapExisting(QObject* obj, const ModuleMetadata& = ModuleMetadata())` | Wrap an existing `QObject` plugin instance (marked static, so `unload()` won't delete it) |
| `bool isValid() const` | True iff there is a non-null instance |
| `QObject* instance() const` | The raw `QObject` plugin instance |
| `const ModuleMetadata& metadata() const` | The parsed metadata |
| `QString errorString() const` | Last error message from a failed load |
| `template<typename T> T* as() const` | Type-safe `qobject_cast` to interface `T`; `nullptr` if the cast fails or there is no instance |
| `void unload()` | Destroy the loader (unless static) and clear the instance |
| `QObject* release()` | Relinquish ownership and return the instance (marks the handle static so the destructor won't touch it) |

**Metadata helpers (no load):**

| Method | Description |
|--------|-------------|
| `static std::optional<ModuleMetadata> extractMetadata(const QString&)` *(+ `std::string` overload)* | Extract metadata without loading (delegates to `ModuleMetadata::fromPath`) |
| `static std::string getModuleName(const std::string&)` | Just the module name without loading; empty string on failure |
| `static std::vector<std::string> getModuleDependencies(const std::string&)` | The dependency-name list without loading; empty on failure |

**Introspection:**

| Method | Description |
|--------|-------------|
| `std::vector<MethodInfo> getMethods(bool excludeBaseClass = true) const` *(+ static `getMethods(QObject*, bool)`)* | Introspect methods. For a `LogosProviderPlugin` it uses `provider->getMethods()` (skipping `"event"`-tagged entries); otherwise it walks the `QMetaObject`. `excludeBaseClass` drops inherited `QObject` methods (no effect on the provider path) |
| `QJsonArray getMethodsAsJson(bool excludeBaseClass = true) const` *(+ static)* | Same as `getMethods` but returns a `QJsonArray`; for providers returns the `"method"`-typed entries of `getMethods()` |
| `QJsonArray getEventsAsJson() const` *(+ static)* | Returns the `"event"`-tagged entries from a new-API provider's `getMethods()`; **empty array** for legacy / non-provider plugins |
| `QString getClassName() const` *(+ static)* | The meta-object class name of the instance |
| `bool hasMethod(const QString&) const` *(+ `std::string` overload, + static `hasMethod(QObject*, const QString&)`)* | True if the plugin exposes a method with the given name (uses `getMethods` including the base class) |

A provider's `getMethods()` returns the module's full interface; the private
`filterInterfaceByType()` helper splits it by the `"type"` tag. An entry with
no `"type"` is treated as a method, so plugins built against the pre-events SDK
degrade cleanly. When `createProviderObject()` returns null, all paths fall
through to the `QMetaObject` walk.

### LogosProviderObject / LogosProviderPlugin (new-API mirror)

**File:** `src/logos_provider_plugin.h` (header-only)

An abstract mirror of the cpp-sdk provider object. The file's leading comment
is unambiguous: *"Vtable layout MUST match
`logos-cpp-sdk/cpp/logos_provider_object.h`."* `LogosModule` `qobject_cast`s a
loaded plugin to `LogosProviderPlugin` to decide whether to take the new-API
introspection path.

```cpp
class LogosProviderObject {
public:
    virtual ~LogosProviderObject() = default;
    using EventCallback = std::function<void(const QString&, const QVariantList&)>;

    virtual QVariant callMethod(const QString& methodName, const QVariantList& args) = 0;
    virtual bool     informModuleToken(const QString& moduleName, const QString& token) = 0;
    virtual QJsonArray getMethods() = 0;   // carries methods AND events (each tagged "type")
    virtual void     setEventListener(EventCallback callback) = 0;
    virtual void     init(void* apiInstance) = 0;
    virtual QString  providerName() const = 0;
    virtual QString  providerVersion() const = 0;
};

class LogosProviderPlugin {
public:
    virtual ~LogosProviderPlugin() = default;
    virtual LogosProviderObject* createProviderObject() = 0;
};

#define LogosProviderPlugin_iid "org.logos.LogosProviderPlugin"
Q_DECLARE_INTERFACE(LogosProviderPlugin, LogosProviderPlugin_iid)
```

There is **deliberately no `getEvents()` vtable slot** — events ride inside
`getMethods()`, which is what lets `lm` introspect both old and new modules
without a vtable mismatch.

### PluginInterface (legacy)

**File:** `src/interface.h` (header-only)

The common base interface all old-API modules implement, declared with
`Q_DECLARE_INTERFACE`:

```cpp
class PluginInterface {
public:
    virtual ~PluginInterface() {}
    virtual QString name() const = 0;
    virtual QString version() const = 0;
    LogosAPI* logosAPI = nullptr;
};
#define PluginInterface_iid "com.example.PluginInterface"
Q_DECLARE_INTERFACE(PluginInterface, PluginInterface_iid)
```

It `#include`s `logos_api.h` (supplied by liblogos / the SDK at consumer build
time, not vendored here). See [Known Limitations](#known-limitations) for the
placeholder IID and the commented-out `initLogos` TODO.

### InstancePersistence

**Files:** `src/instance_persistence.h`, `src/instance_persistence.cpp`

Assigns each module instance a unique ID and a dedicated directory
`{basePath}/{moduleName}/{instanceId}`, creating it with `QDir::mkpath`.

```cpp
enum class ResolveMode { ReuseOrCreate, AlwaysCreate, UseExplicit };

struct InstanceInfo    { QString     instanceId; QString     persistencePath; };
struct StdInstanceInfo { std::string instanceId; std::string persistencePath; };

InstanceInfo resolveInstance(const QString& basePath,
                             const QString& moduleName,
                             ResolveMode mode = ResolveMode::ReuseOrCreate,
                             const QString& explicitInstanceId = QString());
// + std::string overload returning StdInstanceInfo
```

| `ResolveMode` | Behavior |
|---------------|----------|
| `ReuseOrCreate` (default) | Use the first existing instance directory found (by `QDir::Name` ordering); if none exist, generate a fresh ID |
| `AlwaysCreate` | Always generate a fresh 12-hex-char ID, ignoring existing directories |
| `UseExplicit` | Use the caller-supplied `explicitInstanceId` (must be non-empty and a valid path segment) |

IDs are generated as `QUuid::createUuid().toString(QUuid::Id128).left(12)`
(12 hex chars). The function **fails closed**, returning empty strings, when:
`basePath` or `moduleName` is empty; `moduleName` or `explicitInstanceId`
fails the `isValidPathSegment` check (contains `/` or `\`, or is exactly `..`
or `.`); `UseExplicit` is given an empty ID; the resolved path escapes
`basePath` (verified against the canonical base + `/`); or `mkpath` fails.

### `lm` CLI

**Files:** `cmd/main.cpp`, `cmd/CMakeLists.txt`

"Logos Module Inspector" — a thin front-end over the library. It installs a
custom Qt message handler that suppresses `QtDebugMsg` / `QtInfoMsg` unless
`--debug` is passed, and for the `methods` / `events` / default commands it
redirects stdout+stderr to `/dev/null` while the plugin loads (to hide plugin
constructor chatter) unless `--debug` is given. See the
[`lm` Command Reference](#lm-command-reference) below.

## Building and Testing

### Workspace (preferred)

```bash
export PATH="/workspace/scripts:$PATH"

ws build logos-module                # build via nix (default output = the library)
ws build logos-module --auto-local   # build with local overrides of dirty deps
ws test  logos-module                # run the repo's nix checks (gtest, CLIPluginTest skipped)
ws test  logos-module --auto-local
ws watch test logos-module           # re-run tests on file change
ws develop logos-module              # enter the repo dev shell
```

`lm` is also available directly (auto-builds / auto-rebuilds from source) or as
`ws lm`.

### Raw Nix

```bash
nix build .#lib   # static library -> result/lib/liblogos_module.a, headers result/include/module_lib/
nix build .#lm    # CLI -> result/bin/lm   (aliases: .#cli  .#bin  .#cmd)
nix build .#all   # library + CLI + tests; runs ctest in checkPhase
nix build .#ci    # like .#all but ctest --exclude-regex CLIPluginTest
nix develop       # dev shell: cmake, ninja, pkg-config, qt6.qtbase, gtest
```

`nix build` with no attribute builds the default package — the library
(`flake.nix` sets `default = lib`). `nix build .#all` and `.#ci` set
`doCheck = true` and run `ctest --output-on-failure` during the build. The
checkPhase exports `LM_BINARY` and `TEST_PLUGIN` so the CLI tests can find the
binary and the platform-specific test plugin.

### Raw CMake

```bash
# Build the library + lm only (tests off by default)
cmake -B build -GNinja && cmake --build build

# Build with the test suite
cmake -B build -GNinja -DLOGOS_MODULE_BUILD_TESTS=ON && cmake --build build
cd build && ctest --output-on-failure
```

`LOGOS_MODULE_BUILD_TESTS` defaults to `OFF`. The library lands in `build/`,
the `lm` binary in `build/bin/`, and the test binary at
`build/tests/logos_module_tests`.

### Running the tests directly

```bash
# Run the whole gtest binary
./result/bin/logos_module_tests

# CI invocation: point at the binary + test plugin, skip the precompiled-plugin CLI tests
LM_BINARY=$(pwd)/result/bin/lm \
TEST_PLUGIN=$(pwd)/tests/examples/package_manager_plugin.so \
  ./result/bin/logos_module_tests --gtest_filter=-CLIPluginTest.*
```

### Test suites

| Test file | Coverage |
|-----------|----------|
| `test_metadata.cpp` | `fromCustomMetadata` / `fromJson` parsing, `isValid`, `getModuleName` / `getModuleDependencies`, and the `std::string` overloads of `fromPath` / `loadFromPath` / `extractMetadata` (`MetadataTest`, `GetModuleNameTest`, `GetModuleDependenciesTest`, `*StdStringTest`) |
| `test_introspection.cpp` | Legacy `QMetaObject` introspection (`IntrospectionTest`) and new-API provider introspection — methods, events, the documented/undocumented event split, and the null-provider fallback (`ProviderPluginIntrospectionTest`), using `MockPlugin` / `MockNewApiPlugin` / `MockBrokenNewApiPlugin` |
| `test_instance_persistence.cpp` | All `ResolveMode` behaviors, error handling, path-traversal protection, and ID format |
| `test_cli.cpp` | End-to-end CLI: version / help / no-args, error handling (unknown option, missing path, nonexistent file), and — against the prebuilt `package_manager_plugin` — metadata + method introspection (`CLITest`, `CLIPluginTest`). `CLIPluginTest` is excluded in CI because the precompiled plugin's Nix-store / architecture paths are incompatible |

### Continuous Integration

`.github/workflows/ci.yml` runs on push / PR to `master` or `main`:

1. Checkout, install Nix with flakes enabled.
2. `nix build .#ci`.
3. Run `./result/bin/logos_module_tests --gtest_filter=-CLIPluginTest.*`
   with `LM_BINARY` and `TEST_PLUGIN` exported.

## `lm` Command Reference

```
Usage: lm [command] [options] <plugin-path>

Commands:
  (default)   Show metadata, methods, and events (when no command specified)
  metadata    Show plugin metadata (name, version, description, etc.)
  methods     Show plugin methods and signatures
  events      Show plugin events and signatures

Options:
  --json         Output in JSON format
  --debug        Show debug output from plugin loading
  --help, -h     Show help information
  --version, -v  Show version information
```

| Invocation | Behavior | Exit |
|------------|----------|------|
| `lm <plugin-path>` | Show metadata + methods + events. Human by default; `--json` emits a combined `{metadata, methods, events}` object | 0; 1 on missing file / load failure / null instance |
| `lm metadata <plugin-path>` | Print only metadata: `Name`, `Version`, `Description`, `Author`, `Type`, `Protocol` (`logos_protocol_version`, or `(unstamped — pre-protocol build)` when absent), `Dependencies` (or `(none)`). Extracts metadata **without loading** the plugin | 0; 1 if file not found or extraction fails |
| `lm methods <plugin-path>` | Print only methods (loads the plugin): for each, the rendered `<returnType> <name>(<type> <param>, …)`, `Signature`, `Invokable`, and an optional indented `Description` | 0; 1 if file not found / load fails / null instance |
| `lm events <plugin-path>` | Print only events (new-API providers only), rendered as `void <name>(<args>)` plus `Signature` and optional `Description`. Legacy plugins report `(no events found)` | 0; 1 if file not found / load fails / null instance |
| `lm --help` / `-h`, `lm <command> --help` | Top-level usage, or per-command help for `metadata` / `methods` / `events` | 0 |
| `lm --version` / `-v` | Prints `lm (Logos Module) version 0.1.0` | 0 |
| `lm` (no args) | Prints usage | 0 |

An unknown option (`lm metadata --bogus plugin.so`) or a first argument that
starts with `-` and isn't `-h`/`-v` prints `Error: Unknown option …` and exits
1. A first argument that is neither a command nor an option is treated as a
plugin path (default mode).

**Note on `--json` field coverage:** `lm metadata --json` includes
`logos_protocol_version` when the plugin carries it. The combined default-mode
`--json` object's `metadata` block carries only `name` / `version` /
`description` / `author` / `type` / `dependencies` (no protocol field) — it is
built separately in `cmdInfo`.

## Examples

### Inspecting a module from the shell

```bash
# Everything (metadata + methods + events)
lm /path/to/plugin.dylib
lm /path/to/plugin.dylib --json

# One section at a time
lm metadata /path/to/plugin.so
lm methods  /path/to/plugin.so --json --debug
lm events   /path/to/plugin.so

lm --version    # lm (Logos Module) version 0.1.0
```

### Reading a module's name / deps / metadata without loading it

```cpp
#include <logos_module.h>   // headers install to include/module_lib/
using namespace ModuleLib;

std::string name = LogosModule::getModuleName("/path/to/plugin.so");
std::vector<std::string> deps = LogosModule::getModuleDependencies("/path/to/plugin.so");
auto meta = LogosModule::extractMetadata("/path/to/plugin.so");   // std::optional<ModuleMetadata>
```

This no-load path is what `logos-liblogos`'s protocol gate and the package
manager use to read a module's `name` / `dependencies` / `logos_protocol_version`
before deciding to load it.

### Loading a module and calling into it

```cpp
QString error;
LogosModule plugin = LogosModule::loadFromPath("/path/to/plugin.so", &error);
if (!plugin.isValid()) {
    qCritical() << "Failed to load:" << error;
    return;
}
qDebug() << "Name:" << plugin.metadata().name
         << "Version:" << plugin.metadata().version;

auto* iface = plugin.as<PluginInterface>();   // type-safe qobject_cast
if (iface) { /* use the typed interface ... */ }
```

### Discovering a module's callable API programmatically

```cpp
QJsonArray methods = plugin.getMethodsAsJson();   // legacy QMetaObject OR new-API provider
QJsonArray events  = plugin.getEventsAsJson();    // new-API providers only; [] for legacy
QString    cls     = plugin.getClassName();
bool       can     = plugin.hasMethod("doSomething");
```

### Giving a module instance its own persistent storage

```cpp
#include <instance_persistence.h>
using namespace ModuleLib::InstancePersistence;

// Default: reuse an existing instance dir, else create one
auto info  = resolveInstance("/data", "my_module");
//   info.instanceId      -> e.g. "a1b2c3d4e5f6"
//   info.persistencePath -> "/data/my_module/a1b2c3d4e5f6"

auto fresh = resolveInstance("/data", "my_module", ResolveMode::AlwaysCreate);
auto exact = resolveInstance("/data", "my_module", ResolveMode::UseExplicit, "abc123");
```

## Known Limitations

- **`interface.h` needs an external header.** `src/interface.h` `#include`s
  `logos_api.h`, which is **not** present in this repo (it is provided by
  `logos-liblogos` / `logos-cpp-sdk` at the consumer's build time). The library
  compiles standalone only because `interface.h` is a header that is not part
  of the built sources.
- **Placeholder interface IID.** `PluginInterface_iid` is still the placeholder
  `"com.example.PluginInterface"`.
- **Commented-out lifecycle TODO.** `interface.h` carries a TODO to move
  `initLogos(LogosAPI*)` into the base interface and remove it from modules; it
  is currently commented out.
- **Hand-maintained vtable contract.** `src/logos_provider_plugin.h` must be
  kept vtable-compatible by hand with
  `logos-cpp-sdk/cpp/logos_provider_object.h`; a mismatch would silently break
  introspection of new-API modules.
- **Precompiled test plugins are environment-specific.** `tests/examples/*.so`
  / `*.dylib` have Nix-store / architecture path incompatibilities, so
  `CLIPluginTest` is excluded both in CI and via the `skipPluginTests` flag in
  `nix/all.nix`.
- **Brittle binary/plugin discovery in tests.** `test_cli.cpp` carries a
  `// TODO: this is dumb, fix` around the hard-coded `lm` binary / test-plugin
  path search heuristics (used only when `LM_BINARY` / `TEST_PLUGIN` are unset).
- **Arbitrary instance selection in `ReuseOrCreate`.** `resolveInstance` picks
  the first instance directory by `QDir::Name` ordering, so with multiple
  existing instances the choice is effectively alphabetical / arbitrary.
- **`logos_protocol_version` is metadata-only here.** `lm` surfaces and
  displays the stamp but does not enforce compatibility; load/call
  compatibility decisions live in the consumers (e.g. liblogos's protocol gate).
