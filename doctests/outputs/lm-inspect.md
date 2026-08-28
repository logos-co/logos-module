# Inspecting a Logos Module with lm

[`lm`](https://github.com/logos-co/logos-module) is the Logos **module inspector**. It
answers one question — *what is this module?* — about a thing you have on disk but did
not build.

It takes two shapes of path, and the difference is the point of this doc-test:

- a **plugin file**, the compiled `.so` / `.dylib` a module builds to. Everything `lm`
  can say about it is baked into the file: the metadata block the build stamped in, and
  the methods and events its class declares.
- an **installed module directory**, the directory a package manager extracted a package
  into. Here the module is not one file but a *tree* — a `manifest.json` naming the
  plugin, the payload beside it, a `variant` file recording which platform's build was
  installed, and (if the publisher signed it) a `manifest.sig`. `lm` reads the manifest,
  finds the plugin it names, and then tells you everything it would have told you about
  that plugin anyway.

This doc-test builds **this** `lm` commit, builds a real Logos module plugin from source
so every load is native on the machine running it, and then walks both shapes — ending
with the four ways a directory can fail to name a plugin, and the one package type for
which naming no plugin at all is perfectly correct.

**What you'll build:** A `greeter` Qt plugin compiled from source, installed into a module directory, plus a QML-only UI package and four deliberately broken directories to read `lm`'s diagnostics off.

**What you'll learn:**

- What `lm metadata` / `methods` / `events` read out of a compiled plugin, and where each comes from
- What an installed module directory is on disk, and how `lm` resolves its `manifest.json` `main` for this platform
- Why a QML-only `ui_qml` package having no plugin is a complete package, not a broken one
- How to tell the four "no plugin here" failures apart from `lm`'s message, because each is a different repair

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- A Linux or macOS machine.

---

## Step 1: Build the lm CLI

`lm` is a small Qt-based C++ CLI. Build it from the flake's `#lm` output and link the
result as `./lm`, so the binary lands at `./lm/bin/lm`.

> The `{release}` in the URL is what pins the build to a specific commit: the
> doc-test runner expands it to a concrete ref. Locally that is this checkout's
> `HEAD` (see `run.sh`); in CI it is the commit being tested. Developing against a
> local checkout? Replace the GitHub reference with `.`, e.g. `nix build '.#lm' -o lm`.

### 1.1 Build lm

```bash
# From inside the clone this is simply: nix build '.#lm' -o lm
nix build 'github:logos-co/logos-module/b55063d90156ff40395f70b16af8aeaaf88ac641#lm' -o lm
```

### 1.2 Confirm it runs

```bash
./lm/bin/lm --version
```

---

## Step 2: Build a module to inspect

`lm` reads modules, so we need one. A Logos module is a **Qt plugin**: a shared
library holding one `QObject` class, with two things that matter to an inspector.

- `Q_PLUGIN_METADATA(IID … FILE "greeter.json")` embeds that JSON file into the
  binary as a data section. That section — not the filename, not anything on disk
  beside it — is what `lm metadata` prints.
- every `Q_INVOKABLE` method is registered in Qt's meta-object, and that registry is
  what `lm methods` lists. It is also exactly the set of methods other modules can
  call over the Logos runtime, which is why "what does `lm methods` say" is the same
  question as "what is this module's API".

We compile it inside this repository's own dev shell, so the Qt it links is the Qt
`lm` was built against, and the plugin is native to whichever machine is running
this — no pre-compiled binary that only loads on one of them.

### 2.1 The embedded metadata

`greeter.json` is the module's identity card. Note the two shapes a dependency
takes: a bare name declares the edge and constrains nothing, while the object
form additionally says which versions will do.

```json
{
  "name": "greeter",
  "display_name": "Greeter",
  "version": "1.2.0",
  "description": "A minimal Logos module, built to be inspected.",
  "author": "Logos",
  "type": "core",
  "logos_protocol_version": "0.6.0",
  "dependencies": [
    "storage_module",
    { "name": "chat_module", "version": ">=2.0.0" }
  ]
}
```

### 2.2 The plugin class

Two `Q_INVOKABLE` methods, which is the whole public API of this module.

```cpp
#pragma once
#include <QObject>
#include <QtPlugin>
#include <QString>

class GreeterPlugin : public QObject
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.example.PluginInterface" FILE "greeter.json")

public:
    Q_INVOKABLE QString name() const { return QStringLiteral("greeter"); }

    Q_INVOKABLE QString greet(const QString& who) const {
        return QStringLiteral("hello, ") + who;
    }
};
```

### 2.3 The build

`MODULE` is CMake's word for a library that is only ever `dlopen`ed, never linked
against — which is what a plugin is. `SUFFIX` names it the way this platform
names shared libraries, `.so` on Linux and `.dylib` on macOS, because a real
module's `manifest.json` will name it that way too.

```
cmake_minimum_required(VERSION 3.16)
project(greeter LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(greeter_plugin MODULE greeter_plugin.h)
set_target_properties(greeter_plugin PROPERTIES
    AUTOMOC ON
    PREFIX ""
    SUFFIX ".${LOGOS_MODULE_EXT}")
target_link_libraries(greeter_plugin PRIVATE Qt6::Core)
```

### 2.4 Compile the plugin

`nix develop` on this same repository puts CMake and the matching Qt on `PATH`.
The assertion is on a sentence the shell prints, not on the file name, since that
name differs per platform.

```bash
# {ext} is `so` on Linux, `dylib` on macOS.
nix develop 'github:logos-co/logos-module/b55063d90156ff40395f70b16af8aeaaf88ac641' --command bash -c \
  'cmake -S . -B build -DLOGOS_MODULE_EXT=so >/dev/null && cmake --build build >/dev/null'
```

---

## Step 3: Read a plugin file

This is `lm`'s oldest job: point it at a compiled plugin and it tells you what the
plugin says about itself. Nothing else is consulted — no manifest, no package, no
runtime.

### 3.1 lm metadata — the embedded identity card

Every line here came out of `greeter.json` as it was compiled in. `Protocol:` is
the logos-protocol version the module was built against, the one number that
governs whether a host can load and call it; a module from a pre-protocol builder
has no stamp and says so. Both dependency forms are listed by name — the object
form is not a different edge, just a constrained one.

```bash
lm metadata build/greeter_plugin.so
```

### 3.2 lm methods — the module's API

Read out of Qt's meta-object, so it is the real callable surface rather than
anything a document claims. `Signature:` is the normalised form the runtime
dispatches on.

```bash
lm methods build/greeter_plugin.so
```

### 3.3 lm events — and why this one has none

Events are the fire-and-forget half of a module's interface, declared by
universal modules in their `logos_events:` section and rendered with a `void`
return. A hand-written Qt plugin like ours declares none, and `lm` says so rather
than failing — "no events" is an answer, not an error.

```bash
lm events build/greeter_plugin.so
```

### 3.4 The JSON shape, and what is missing from it

`--json` gives the same facts to a script. The interesting part is the key that
is *not* there: `module_directory` describes an install, and a bare plugin file
has no manifest, no signature and no variant to describe. The key is absent
rather than empty, so a parser can never mistake one for the other.

```bash
lm metadata build/greeter_plugin.so --json > meta.json
grep -q module_directory meta.json && echo "install report present" \
                                   || echo "no install report: a plugin file has no manifest to report"
```

---

## Step 4: Install it into a module directory

Now the other shape. When a package manager installs a module it does not leave you a
single file — it leaves a **directory**, and that directory *is* the module:

```
modules/greeter/
  manifest.json          # who this is, and which file is the plugin
  greeter_plugin.so      # the payload, .dylib on macOS
  variant                # which platform's build was installed here
```

`main` in the manifest is a map keyed by **variant** — one package can carry a build
for every platform, and only one of them was extracted here. The `variant` file
records which, so `lm` knows which key to read. Point `lm` at the directory and it
resolves that chain for you.

### 4.1 Assemble the directory

Detect this machine's variant, then write the manifest and the `variant` file
around the plugin we compiled.

```bash
mkdir -p modules/greeter
cp build/greeter_plugin.so modules/greeter/
cat > modules/greeter/manifest.json <<'EOF'
{
  "author": "Logos",
  "category": "",
  "dependencies": [],
  "description": "A minimal Logos module, built to be inspected.",
  "icon": "",
  "main": { "linux-x86_64": "greeter_plugin.so" },
  "manifestVersion": "0.5.0",
  "name": "greeter",
  "type": "core",
  "version": "1.2.0"
}
EOF
echo linux-x86_64 > modules/greeter/variant
```

### 4.2 Read the directory

One report, then the same three sections a plugin file would have given —
because after resolving `main`, that is exactly what `lm` is looking at. Note
that `Main:` shows *which* variant key answered, so a package installed for the
wrong platform is visible at a glance.

```bash
lm modules/greeter
```

### 4.3 The install in JSON

For a directory, the `module_directory` object is added to the JSON that a plugin
file's `--json` did not have. Two of its fields are worth knowing about:
`plugin` is `lm`'s verdict on whether a plugin was expected at all, and
`name_agreement` compares the name in `manifest.json` against the name compiled
into the plugin — a directory that ships one module under another module's name
is exactly the shape you want a tool to notice for you.

```bash
lm modules/greeter --json > install.json
grep '"plugin"\|"name_agreement"' install.json
```

---

## Step 5: A UI package with no plugin at all

A `ui_qml` package is a module directory too, and `lm` reads it the same way — but it
may legitimately contain no compiled code whatsoever. A view-only UI is QML plus its
assets; there is nothing to `dlopen`, and a `manifest.json` with no `main` is
therefore **complete**, not broken, as long as it says what to render instead.

This is the one distinction worth carrying away from this doc-test: *the same missing
field means different things depending on the package's type.* `lm` reports the
absence and exits `0`, so a script that installs UI packages does not have to treat
every one of them as a failure.

### 5.1 Assemble a QML-only ui_qml package

```bash
V="$(cat variant.txt)"
mkdir -p plugins/greeter_ui/qml plugins/greeter_ui/assets
printf 'import QtQuick\nItem {}\n' > plugins/greeter_ui/qml/Main.qml
printf 'not really a png\n' > plugins/greeter_ui/assets/icon.png
cat > plugins/greeter_ui/manifest.json <<'EOF'
{
  "author": "Logos",
  "category": "",
  "dependencies": [],
  "description": "A ui_qml package that is QML only.",
  "icon": "assets/icon.png",
  "manifestVersion": "0.5.0",
  "name": "greeter_ui",
  "type": "ui_qml",
  "version": "1.2.0",
  "view": "qml/Main.qml"
}
EOF
echo "$V" > plugins/greeter_ui/variant
echo "assembled greeter_ui"

```

### 5.2 lm reports the absence and succeeds

`View:` and `Icon:` appear because the manifest declares them, and `lm` checks
that both actually resolve inside the directory — a `view` naming a file that was
never extracted is reported as `MISSING` right on that line.

```bash
lm plugins/greeter_ui
```

### 5.3 The plugin commands succeed too

`methods` on a package with no plugin returns an empty list, not an error, and
still a bare JSON array — a parser reading `lm methods --json` never has to
special-case the one package type with nothing to list. `plugin: not_expected` in
the directory report is the field that distinguishes this from a module whose
plugin is genuinely missing.

```bash
lm plugins/greeter_ui --json | grep '"plugin"'
lm methods plugins/greeter_ui --json
```

---

## Step 6: When a directory cannot name a plugin

Four different things can go wrong between "there is a directory" and "here is the
plugin", and they need four different repairs. `lm` names the actual fault in each
case, because *not found* would tell whoever hit it nothing they could act on. Each
one exits `1`.

### 6.1 1. No manifest.json

A module directory is *identified* by its manifest. Without one there is nothing
to say the directory is a module at all, so `lm` points you back at the file.

```bash
mkdir -p broken/no_manifest
cp build/greeter_plugin.so broken/no_manifest/
lm broken/no_manifest
```

### 6.2 2. A manifest with no "main"

Here is the contrast with the QML-only package above: the same field is absent,
but this manifest says `"type": "core"`, and a core module is *only* its plugin.
Nothing in the directory is named as the thing to load, so there is nothing to
run — and `lm` says that rather than pointing at `view`.

```bash
# manifest.json with "type": "core" and no "main"
lm broken/no_main
```

### 6.3 2b. The same omission in a ui_qml package

Change one field — the type — and the verdict changes with it. A `ui_qml` package
is allowed to have no `main`, but only when it declares a `view` to render
instead; with neither, there is genuinely nothing here to run, and the message
names the field that would make it whole.

```bash
# the same manifest as above, but "type": "ui_qml"
lm broken/ui_no_view
```

### 6.4 3. A "main" naming a file that is not there

The manifest is fine and the variant matched; the file it points at simply is not
in the directory. That is an installation fault rather than a packaging one, so
the message names the two ways it happens.

```bash
# the good manifest, copied into a directory whose payload was never extracted
lm broken/missing_file
```

### 6.5 4. A "main" with no entry for this variant

The package is intact — it just does not carry a build for the platform it was
installed on. `lm` prints both sides of the mismatch: the variants `main` offers,
and the ones it tried.

```bash
# "main": { "aix-ppc64": "greeter_plugin.a" }, installed on Linux or macOS
lm broken/wrong_variant
```

### 6.6 4b. …and with no variant file to go on

Delete the `variant` file and `lm` has nothing to match against at all. Rather
than guessing this machine's platform and reporting a mismatch it never actually
tested, it says the list it tried was empty and names the flag that fills it in.

```bash
rm broken/wrong_variant/variant
lm broken/wrong_variant
```

---

## Step 7: Choosing the variant yourself

`--variant` supplies what the `variant` file would have. It is the answer to the
previous failure, and it is also how you inspect a package for a platform you are not
on. It only makes sense for a directory, so `lm` refuses it on a plugin file rather
than silently ignoring it.

### 7.1 A directory with no variant file, recovered

```bash
lm metadata broken/no_variant_file                          # fails: nothing to match
lm metadata broken/no_variant_file --variant linux-x86_64   # resolves
```

### 7.2 Refused on a plugin file

There is no manifest in a `.so` to select a `main` out of, so the flag has no
meaning here and `lm` says which of the two shapes it was handed.

```bash
lm metadata build/greeter_plugin.so --variant linux-x86_64
```

---

## Step 8: What a hand-made directory cannot prove

Everything above was about *reading*. `lm verify` asks the other question — is this
install intact, and who signed it? — and our directory cannot answer it: we wrote the
manifest ourselves, so it carries no content hashes for anything to be checked
against. `lm` reports that as *not proved* rather than as a pass, and fails.

Real hashes come from the packaging step. The companion
[verification doc-test](lm-verify.md) builds a genuine signed `.lgx`, installs it,
and takes `verify` through integrity, signer pinning and tampering.

### 8.1 Verify the hand-made install

```bash
lm verify modules/greeter
```

---

## Recap

You built `lm` from this commit, compiled a real Logos module plugin, and read it
both ways — as a file and as the directory it gets installed into:

| Command | On a plugin file | On a module directory |
|---|---|---|
| `lm <path>` | metadata + methods + events | the install report, then the same three |
| `lm metadata <path>` | the embedded identity card | plus `module_directory` under `--json` |
| `lm methods <path>` | the `Q_INVOKABLE` surface | the same, for the plugin `main` names |
| `lm events <path>` | declared events, or none | the same |
| `lm verify <path>` | refused — a file has no manifest | integrity + signature ([see the verify doc-test](lm-verify.md)) |
| `--variant <name>` | refused — nothing to select from | supplies what the `variant` file would have |

And the four ways a directory can fail to name a plugin, each with its own repair:

| `lm` says | What to fix |
|---|---|
| `no manifest.json in module directory` | you passed a directory that is not an install |
| `manifest.json declares no "main"` | the package names nothing to load (or, for `ui_qml`, no `view` either) |
| `names a main that is not in the module directory` | extraction was partial, or was for another variant |
| `declares no main for this variant` | the package carries no build for this platform |

The one that is *not* a failure: a `ui_qml` package with a `view` and no `main` is
QML only, and `lm` reports the absent plugin and exits `0`.
