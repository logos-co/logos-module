# Logos Module — Specification

## Overview

`logos-module` is the part of the Logos platform that answers a single question about any module on disk or in memory: **what is this thing, and what can it do?** It is both a small abstraction library and a command-line inspector (`lm`, the "Logos Module Inspector").

In the Logos platform, a module is an independently developed plugin that the runtime loads, isolates in its own process, and lets other modules call over RPC. Before any of that can happen, something has to read the module's embedded identity (its name, version, dependencies, and the protocol it was built against) and discover its callable surface (the methods it exposes and the events it emits). That "read identity, discover surface" capability is what `logos-module` provides, and it is depended on by the rest of the platform: the core runtime's protocol gate reads a module's metadata to decide whether it is compatible, the package manager reads a module's name and dependencies, and the operator or an agent reaches for `lm` to inspect a plugin from the shell.

The library deliberately hides *how* introspection is done. Today modules are Qt plugins and introspection rides on Qt's plugin loader and meta-object system, but consumers never see that — they ask `logos-module` for a module's metadata, methods, and events, and the library decides which mechanism to use. This is the stated design intent: an abstraction layer that could in principle be re-backed by a different plugin technology without changing any consumer.

```
            ┌──────────────────────────────────────────────┐
            │  Consumers                                     │
            │  • core runtime protocol gate (reads metadata) │
            │  • package manager (reads name + deps)         │
            │  • operators / agents (run `lm`)               │
            └───────────────────────┬────────────────────────┘
                                    │ "what is this, what can it do?"
                                    ▼
            ┌──────────────────────────────────────────────┐
            │  logos-module                                  │
            │  ┌───────────────┐   ┌──────────────────────┐ │
            │  │ Metadata      │   │ Introspection        │ │
            │  │ (identity)    │   │ (methods + events)   │ │
            │  └───────────────┘   └──────────────────────┘ │
            │  ┌───────────────┐   ┌──────────────────────┐ │
            │  │ Module handle │   │ Instance persistence │ │
            │  │ (load/cast)   │   │ (per-instance dirs)  │ │
            │  └───────────────┘   └──────────────────────┘ │
            └───────────────────────┬────────────────────────┘
                                    │ abstracts over
                                    ▼
            ┌──────────────────────────────────────────────┐
            │  Plugin technology (today: Qt plugin system)   │
            └──────────────────────────────────────────────┘
```

The library is consumed in C++; the `lm` CLI is a thin front-end over it that an operator or AI agent can drive from a shell to inspect a single plugin file.

---

## Domain Model & Concepts

| Term | Definition |
|------|------------|
| **Module / plugin** | A dynamically loadable plugin file (`.so`, `.dylib`, `.dll`) implementing a Logos interface and carrying embedded JSON metadata. The two words are used interchangeably here. |
| **Metadata** | The identity a module embeds in its own binary: name, version, description, author, type, dependency list, and the protocol version it was built against. Readable without running the module. |
| **Method** | An operation a module exposes to be called — a request that takes arguments and returns a value. The set of a module's methods is its callable API. |
| **Event** | A fire-and-forget signal a module emits. Unlike a method it has no return value, and only newer-style modules declare events. |
| **Interface** | The complete callable/observable surface of a module: its methods together with its events. |
| **Legacy module (old API)** | A module whose API is expressed as invokable methods on its plugin object, introspected through the host platform's meta-object facilities. Such a module declares no events. |
| **Provider module (new API)** | A module that exposes a *provider object* able to report its own full interface (methods and events together). Introspected by asking the module to describe itself, rather than by reflecting over it. |
| **Protocol version** | A version stamp in the metadata identifying the Logos protocol the module was compiled against. It governs load/call compatibility — modules sharing the same major version are compatible. A module from a pre-protocol build carries no stamp. |
| **Module handle** | A live, owned reference to a loaded module. It manages the module's lifetime (loaded while held, released on destruction) and is the object you introspect, cast, and call through. |
| **Static module** | A module compiled directly into the host program rather than loaded from a file. It is wrapped in a handle but never unloaded by the library. |
| **Instance** | A single running occurrence of a module, identified by a unique ID and given its own on-disk directory for persistent state. |
| **Persistence directory** | The on-disk location `{base}/{module name}/{instance ID}` reserved for one instance's state. |

### Module identity vs. module surface

These two concerns are kept distinct throughout the system:

- **Identity** is what a module *is* — answered from metadata alone, cheaply, without executing any module code. This is enough for the runtime to decide compatibility and for the package manager to resolve dependencies.
- **Surface** is what a module *can do* — answered by examining a loaded module's interface. This requires bringing the module into memory.

The library lets a consumer ask for either without forcing the other: you can read a module's name and dependencies without ever loading it, and you can inspect a loaded module's methods and events without separately re-reading its metadata.

### Two introspection worlds, one answer

A defining responsibility of `logos-module` is to give the *same* answer about a module's surface regardless of which generation of module it is looking at:

```
                       ┌────────────────────────────┐
   "what methods       │      logos-module          │
    and events does    │   introspection            │
    this module        │                            │
    expose?"  ────────▶│  is it a provider module?  │
                       │      │            │         │
                       │     yes           no        │
                       │      ▼            ▼         │
                       │  ask the      reflect over  │
                       │  module to    the module's  │
                       │  describe     callable      │
                       │  its own      methods       │
                       │  interface    (no events)   │
                       │      │            │         │
                       │      └─────┬──────┘         │
                       │            ▼                │
                       │   methods (+ events)        │
                       └────────────────────────────┘
```

- For a **provider module**, the library asks the module to describe its own interface. The module returns a single combined description in which every entry is tagged as either a *method* or an *event*; the library splits that description by tag so a caller can request methods only, events only, or both.
- For a **legacy module**, the library reflects over the module's callable operations directly. Legacy modules report no events.
- If a module claims to be a provider but cannot actually produce a self-description, the library falls back to the legacy reflection path cleanly rather than failing. This guarantees that *some* answer is always produced for any loadable module.

An entry with no method/event tag is treated as a method, so a provider module built against an older, pre-events generation of the API still introspects correctly.

---

## Features & Functional Requirements

### Reading module identity (metadata)

The library can extract a module's metadata directly from its binary without loading or running it. The metadata model carries:

| Field | Meaning |
|-------|---------|
| name | Unique module identifier |
| version | The module's own version string |
| description | Human-readable description |
| author | Author or organization |
| type | Module type (e.g. `core`) |
| dependencies | Names of modules this one depends on |
| protocol version | The Logos protocol the module was built against (may be absent for pre-protocol builds) |

Requirements:

- Metadata extraction MUST NOT require executing module code.
- Metadata is considered **valid** only if it has a non-empty name; an unnamed module is not a usable identity.
- The raw metadata MUST be available in two forms: a rich structured form for normal consumers, and a plain compact text form so that consumers which cannot (or do not want to) depend on the platform's plugin machinery — notably the core runtime's protocol gate — can still parse it.
- Convenience reads MUST be offered for the two most common identity questions — "what is this module's name?" and "what does it depend on?" — each answerable without loading the module, and each returning an empty result on failure rather than erroring.

### Loading and holding a module

The library provides a module handle that represents an owned, loaded module:

- A handle either holds a valid loaded module or does not; a caller checks validity before use, and an error description is available when loading failed.
- A handle owns the module's lifetime: while the handle is held the module stays loaded, and when the handle is discarded the module is unloaded. Ownership is exclusive — a handle can be moved (transferring ownership) but not copied.
- A caller can **release** a handle, taking over responsibility for the module's lifetime, or **unload** it explicitly.
- A caller can obtain a typed view of the module by asking the handle to cast it to a specific interface; the result is empty if the module does not implement that interface.
- The library can wrap modules that are compiled into the host program rather than loaded from a file, and can enumerate all such built-in modules. Built-in modules are never unloaded by the library.

### Discovering a module's surface (introspection)

Given a loaded module, the library reports:

- **Methods** — each with a name, a full signature, a return type, whether it is invokable, an optional human description, and its parameters (each a name and type).
- **Events** — each with a name, a signature, parameters, and an optional description. Events have no return type because they are fire-and-forget. Only provider modules declare events; legacy modules report none.
- Either list can be requested on its own, or both together.
- The library can also report a loaded module's class name and answer whether it exposes a method of a given name.

Requirements:

- Introspection MUST transparently handle both legacy and provider modules and MUST fall back gracefully when a self-describing module fails to describe itself.
- Method and event **descriptions**, when the module's author documented them, MUST be surfaced so that a human or agent can learn not just the shape of an operation but its intent.
- When reflecting over a legacy module, inherited boilerplate operations from the underlying object system SHOULD be excludable so that only the module's own API is reported.

### Per-instance persistence

The library assigns each running instance of a module its own identity and storage:

- Each instance gets a unique 12-character hexadecimal ID and a dedicated directory at `{base}/{module name}/{instance ID}`, created on disk before being returned.
- Three resolution strategies are supported:

| Mode | Behavior |
|------|----------|
| Reuse-or-create (default) | Reuse the first existing instance directory for the module; if none exists, create a new one. |
| Always-create | Always mint a fresh instance ID, ignoring any existing directories. |
| Use-explicit | Use a caller-supplied instance ID. |

- The module name and any explicit instance ID are each treated as a single, untrusted path segment. They MUST be rejected if empty or if they contain path separators or `..`, and the finally resolved path MUST be verified to remain inside the base directory. This protects against a malicious or malformed name escaping its intended directory (a path-traversal attack), since the module name originates from untrusted module metadata.
- Any failure — invalid input, traversal attempt, or directory-creation failure — yields an empty result rather than a partial or unsafe one.

### The `lm` inspector

`lm` is a command-line front-end that lets an operator or agent inspect a single plugin file, or the directory a package manager installed a package into. It exposes the library's identity and surface capabilities as subcommands, each able to produce either human-readable or machine-readable (JSON) output.

---

## The `lm` Command Surface

`lm` takes a plugin file, or an installed module directory whose manifest names the plugin, and reports on it. The first argument is either a subcommand or — when it is not a recognized subcommand and not an option — the path itself, which selects the default (combined) view.

| Invocation | Conceptual behavior |
|------------|---------------------|
| `lm <plugin>` (no subcommand) | Show the module's **metadata, methods, and events** together. The default, full view. |
| `lm metadata <plugin>` | Show **only the metadata**: name, version, description, author, type, protocol version, and dependencies. Read without loading the module. |
| `lm methods <plugin>` | Show **only the methods** the module exposes. Requires loading the module. |
| `lm events <plugin>` | Show **only the events** the module emits. Provider modules only; legacy modules show none. Requires loading the module. |
| `lm verify <directory>` | Check an **installed directory** against its manifest: the manifest's own field rules, whether the installed files are still the ones its hashes cover, and whether `main`, `view` and the icon resolve. With a caller-supplied DID, also whether that party signed the manifest. |
| `lm --help` / `-h` / `<command> --help` | Show top-level usage, or per-subcommand help for `metadata` / `methods` / `events` / `verify`. |
| `lm --version` / `-v` | Show the inspector's version. |
| `lm` (no arguments) | Show usage. |

### Options

| Option | Meaning |
|--------|---------|
| `--json` | Emit structured JSON instead of the human-readable layout. The default view emits a combined object with `metadata`, `methods`, and `events`; each subcommand emits just its own section. |
| `--debug` | Show the diagnostic chatter a module may produce while being loaded. By default this output is suppressed so that the inspector's own output is clean. |
| `--variant` | The variant to resolve the manifest's `main` with, when the directory does not record one. Repeatable, most preferred first. Directories only. |
| `--did` | The DID whose key must have signed `manifest.json`. `verify` only. Without it, no signature question is asked and none is answered. |

### Installed directories, and UI plugins as one of them

An installed package is a directory, not a plugin with sidecars: a manifest, an
optional detached signature, a record of which variant was extracted, and the
payload. A UI plugin is the same directory with the same manifest — it merely
adds a QML entry point and an icon, and its manifest's `type` is what permits it
to carry no plugin binary at all. Everything below therefore applies to both,
and neither the inspector nor its callers branch on the type to read a
directory:

- **The type decides whether a missing plugin is a fault.** A `ui_qml` package
  that names a view and no main is complete; anything else with no main is
  broken. The two are reported as different states, so a caller is never
  silently permissive about the second.
- **What a UI plugin adds is reported like anything else.** The view and the
  icon are each reported as declared-and-present, declared-and-missing,
  refused for pointing outside the directory, or not declared — the last being
  the ordinary answer for a core module.
- **A QML-only plugin has no embedded plugin metadata**, and that absence is a
  normal state rather than an error.

### Checking an installed directory

The rules an installed package must satisfy are the package format's, so the
inspector does not restate them: it asks the package library, over the exact
bytes and files in the directory, and reports what comes back. It decides
nothing — refusing to load a module is the host's call, and the exit status is
the inspector's own.

Three answers are kept apart, because collapsing them is what makes a check
fail open:

| Answer | Meaning |
|--------|---------|
| The tree matches its declared hash | Verified. |
| It does not | Definitive: content was added, removed or altered. |
| No hash was declared, the input was unusable, or the check could not run | Nothing was proved **and** nothing was disproved. Never a pass. |

**Signatures are checked against a DID the caller names, never one read out of
the signature.** A signature document carries the signer's DID, and verifying
with the key that DID names proves only that the document agrees with itself:
whoever replaces the signature replaces the DID beside it and signs with a key
they own. The caller's DID — a pin, or one looked up in its own keyring — is
the only input that makes the answer mean anything, so it is a required
argument with no default and no overload that omits it. The DID the document
claims is still reported, plainly labelled as self-asserted, because it is what
a human needs in order to decide what to pin.

### Observable behavior

- **Metadata view** prints each field; the protocol version is shown as an explicit "unstamped — pre-protocol build" marker when the module carries no protocol stamp, and dependencies are shown as "(none)" when empty. In JSON, the protocol version is included only when present.
- **Methods view** prints each method with its signature, whether it is invokable, and — when documented — its description (multi-line descriptions are preserved as indented lines). When the module exposes none, it says so.
- **Events view** renders each event as a void operation (it has no return value) with its signature and optional description. When the module declares none — including every legacy module — it says so.
- **Default view** is metadata, then methods, then events, in that order, with the same per-section behavior.

### Exit semantics

| Outcome | Exit code |
|---------|-----------|
| Successful inspection, help, or version | `0` |
| Plugin file not found | `1` |
| Metadata could not be extracted (metadata view) | `1` |
| Module failed to load, or loaded with no usable instance (methods/events/default views) | `1` |
| Unknown option, missing plugin path, or more than one plugin path given | `1` |
| A directory whose manifest, main, or (for a non-`ui_qml` package) missing plugin cannot be resolved | `1` |
| `verify`: a failed check, a signature that is not the named DID's, or a path that is not a directory | `1` |

`lm` is non-interactive and never prompts: it reads its arguments, inspects the one named plugin, prints the result, and exits. This makes it directly drivable by scripts and AI agents, which can request `--json` and branch on the exit code.

---

## Use Cases & Workflows

### Inspect a module from the shell

An operator or agent points `lm` at a plugin file:

```
lm /path/to/plugin.so                 # full report: metadata + methods + events
lm metadata /path/to/plugin.so        # identity only
lm methods /path/to/plugin.so --json  # callable surface as JSON
lm events /path/to/plugin.so          # emitted events
lm /path/to/plugin.so --debug         # include the module's load-time chatter

lm /path/to/modules/my_module         # an installed directory, core or UI
lm verify /path/to/plugins/my_ui      # check it against its manifest
lm verify /path/to/plugins/my_ui --did did:jwk:...   # and against a pinned signer
```

This is how a human confirms what a built artifact actually is, and how an agent discovers a module's available operations and their documented intent before deciding how to call them — without any external documentation.

### Read a module's identity at runtime, without loading it

The core runtime's protocol gate and the package manager need a module's name, dependencies, or full metadata before committing to load it. They ask the library for exactly that — name, dependency list, or the complete metadata — paying only the cost of reading the binary's embedded data, not the cost of running it. The protocol gate in particular consumes the plain-text form of the metadata so it need not depend on the platform's plugin machinery.

### Load a module and call into it

A consumer that intends to use a module loads it into a handle, checks that the handle is valid, obtains a typed view by casting to the interface it expects, and then invokes the module through that view. When finished, discarding the handle unloads the module.

### Discover a module's callable API programmatically

A consumer asks a loaded module's handle for its methods, its events, or both, and receives a uniform description regardless of whether the module is a legacy module or a self-describing provider — and even when a self-describing module fails to describe itself, in which case the library falls back to reflection and still returns a usable answer.

### Give a module instance its own storage

A runtime that supports multiple concurrent instances of a module asks the library to resolve an instance directory for a given base path and module name, choosing whether to reuse an existing instance, always create a fresh one, or use a specific instance ID. It receives back a unique instance ID and a created, traversal-safe directory in which that instance can keep its state.

---

## Guarantees & Boundaries

**Guarantees**

- Identity can always be read without executing module code, and a module without a name is never treated as a valid identity.
- A module handle owns its module's lifetime and cannot be accidentally copied into a second owner.
- Introspection yields a usable answer for any loadable module — legacy or provider, well-behaved or one that fails to self-describe.
- Documented method and event intent is preserved through to both human and machine output.
- Instance directories are confined to their base directory; untrusted names cannot escape it.
- `lm` is fully non-interactive with deterministic, category-based exit codes.

**Boundaries / non-goals**

- `logos-module` does not *run* modules, host them in processes, route RPC between them, or manage their dependency graph at scale — those are the runtime's concerns. This library only reads identity, discovers surface, casts, and assigns per-instance storage.
- The library's introspection of provider modules relies on the provider self-description contract matching the platform SDK's contract; keeping those two in agreement is a platform-level obligation, not something this library can enforce on its own.
- The default reuse-or-create instance strategy picks the first existing instance directory by name order; when several instances exist, the choice among them is effectively arbitrary, so a caller that needs a specific instance must name it explicitly.
