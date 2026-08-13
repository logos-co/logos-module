# Inspecting Logos Module Plugins with lm

[`lm`](https://github.com/logos-co/logos-module) is the Logos **module inspector** — a
small CLI that introspects a compiled Qt-plugin Logos module and prints its metadata,
its `Q_INVOKABLE` methods, and its events. It is the offline counterpart to loading a
module in a running daemon: point it at a `.so`/`.dylib` and it tells you what the
module *is* and *exposes*.

This doc-test builds **this** `lm` commit and a real Logos module to inspect —
`capability_module`, built fresh from its flake so it is compiled against the same
nixpkgs/Qt as `lm` (so `lm` can both read its metadata and load it) — then drives `lm`
across its whole surface:

1. `lm metadata` reads the plugin's embedded metadata section *without* loading the
   binary, and `--json` emits the same machine-readably.
2. `lm`'s error path: pointed at a missing file it exits non-zero with a clear message.
3. `lm methods` and `lm events` *load* the plugin to introspect its live `Q_INVOKABLE`
   surface and declared events.

Every command is the real binary built from the commit under test, so a green run is
evidence that this change keeps `lm` inspecting modules correctly.

**What you'll build:** This commit of the `lm` inspector, used to read the metadata, methods, and events of a freshly-built `capability_module` plugin.

**What you'll learn:**

- How `lm metadata` reads a plugin's embedded metadata without loading the binary
- How to get machine-readable output with `--json`
- How `lm`'s error paths behave (missing file → non-zero exit + message)
- How `lm methods` / `lm events` load a plugin to introspect its live `Q_INVOKABLE` surface

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- A Linux or macOS machine. `lm` is offline and headless; the module it inspects builds from cache.

---

## Step 1: Build the lm CLI

`lm` ships as a Qt/C++ CLI. Build it from this repo's flake `#lm` output and link
it as `./lm`, so the binary lands at `./lm/bin/lm`.

> The `` in the URL pins the build to the commit under test: the doc-test
> runner expands it to a concrete ref (locally this checkout's `HEAD` — see
> `run.sh`; in CI the commit being tested). With no pin it falls back to the latest
> `master`. Developing against a local checkout? Replace the GitHub reference with
> `.`, e.g. `nix build '.#lm' -o lm`.

### 1.1 Build lm

```bash
nix build 'github:logos-co/logos-module#lm' -o lm
```

### 1.2 Confirm it runs

```bash
lm --version
```

### 1.3 Show the usage

```bash
lm --help
```

---

## Step 2: Build a module to inspect

We inspect a real Logos module: `capability_module`. Building it from its flake
yields a plugin compiled against the same nixpkgs/Qt as `lm`, so `lm` can read its
metadata *and* load it for live introspection. The plugin's filename isn't
hard-coded here, so we locate it with `find` and stash the path.

### 2.1 Build capability_module and locate its plugin

```bash
nix build 'github:logos-co/logos-capability-module#default' -o capmod
# locate the built plugin (.so on Linux, .dylib on macOS)
find -L capmod \( -name '*_plugin.so' -o -name '*_plugin.dylib' \) | head -n1
```

`capability_module` is a `logos-module-builder` C++ Qt module; its
`packages.default` is the compiled plugin.

---

## Step 3: Read the plugin's metadata (no load)

`lm metadata` reads the plugin's embedded metadata via Qt's `QPluginLoader`
*without* dlopen-ing it — so it works on any compiled plugin regardless of ABI.

### 3.1 Human-readable metadata

```bash
lm metadata capability_module_plugin.so
```

### 3.2 Same metadata as JSON

```bash
lm metadata capability_module_plugin.so --json
```

---

## Step 4: Error handling

Pointed at a path that doesn't exist, `lm` exits non-zero with a clear message. We
add `|| true` so the doc-test can still assert on the output of a deliberately
failing command.

### 4.1 Missing plugin file

```bash
lm metadata /nonexistent/path/plugin.so
```

---

## Step 5: Introspect the live methods and events

`lm methods` and `lm events` *load* the plugin to read its live `Q_INVOKABLE`
surface — which is why we built the module against the same Qt as `lm`.

### 5.1 List the live methods

```bash
lm methods capability_module_plugin.so
```

### 5.2 List the methods as JSON

```bash
lm methods capability_module_plugin.so --json
```

### 5.3 Inspect the events

```bash
lm events capability_module_plugin.so
```

---

## Recap

You built this commit of `lm` and used it across its whole surface against a real
`capability_module` plugin:

| Command | Loads the plugin? | What it shows |
|---|---|---|
| `lm metadata <plugin>` | no (reads embedded section) | name, version, type, dependencies |
| `lm metadata <plugin> --json` | no | the same, machine-readable |
| `lm methods <plugin>` | yes | the live `Q_INVOKABLE` methods |
| `lm events <plugin>` | yes | the events the plugin declares |

Because `lm` itself is built from the commit under test, a green run proves the
inspector still works end-to-end — static metadata and live introspection alike.
