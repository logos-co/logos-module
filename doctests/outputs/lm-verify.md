# Verifying an Installed Module Directory

Reading a module tells you what it *says* it is. `lm verify` asks the other question: is
this install still the one that was published, and **whose** is it?

A module reaches a machine as an `.lgx` package — a signed archive — and is unpacked into
a directory. The package's guarantees have to survive that unpacking, and two of them do:

- the manifest carries **content hashes** over the files it covers, so the payload on
  disk can be checked against what was published;
- a `manifest.sig` beside it carries an **Ed25519 signature** over the manifest's exact
  bytes.

`lm verify` runs those checks — they are
[logos-package](https://github.com/logos-co/logos-package)'s own, the same rules `lgx
verify` applies to the archive the directory came out of — and exits non-zero when one
fails, so it is safe to gate an installer on.

The subtle half is the signature. `lm` will **not** tell you "the signature is good"
unless you say whose it must be, because the DID inside `manifest.sig` is not evidence of
anything: whoever replaced the signature replaced that DID beside it. This doc-test ends
by relabelling a package to show exactly that — a directory that passes `verify` until
you name a signer, and fails the moment you do.

**What you'll build:** A signed `greeter.lgx`, installed into a module directory the way a package manager would, then verified — intact, tampered with, and relabelled.

**What you'll learn:**

- What survives packaging into an installed directory, and how a package manager lays one out
- How `lm verify` reports integrity, and what a modified payload file looks like
- Why `--did` is the only way to get a signature verdict, and what pinning the wrong DID says
- Why a relabelled package passes verification until you name the signer it should have

## Prerequisites

- **Nix** with flakes enabled. Install from [nixos.org](https://nixos.org/download.html), then enable flakes:

```bash
mkdir -p ~/.config/nix
echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
```

Verify: `nix flake --help >/dev/null 2>&1 && echo "Flakes enabled"`

- A Linux or macOS machine.

---

## Step 1: Build lm and lgx

Two tools: `lm` from this repository, and `lgx` — the package tool — from
logos-package, to produce something worth verifying. They are deliberately the two
ends of the same rules: `lgx` writes the hashes and the signature into a package, and
`lm verify` checks them again once that package has been unpacked onto a machine.

> The `{release}` in the `lm` URL is what pins the build to a specific commit: the
> doc-test runner expands it to a concrete ref — this checkout's `HEAD` locally, the
> commit under test in CI. `lgx` carries no `{release}` and so tracks logos-package's
> `master` on purpose: `lm verify` is a thin shell over logos-package's checks, so if
> those rules move, this doc-test *should* notice.

### 1.1 Build lm

```bash
# From inside the clone this is simply: nix build '.#lm' -o lm
nix build 'github:logos-co/logos-module/b55063d90156ff40395f70b16af8aeaaf88ac641#lm' -o lm
```

### 1.2 Build lgx

```bash
nix build 'github:logos-co/logos-package#lgx' -o lgx
```

### 1.3 Confirm both run

```bash
./lm/bin/lm --version
./lgx/bin/lgx --version

```

---

## Step 2: Package a module and sign it

A package carries one payload tree per **variant** — `linux-x86_64`, `darwin-arm64`
and so on. We build a single-variant package for this machine. The payload here is a
stub rather than a compiled plugin, because verification never loads anything: it
hashes bytes and checks a signature, which is exactly why it works the same on both
platforms. (Compiling and inspecting a real plugin is the
[inspection doc-test](lm-inspect.md).)

### 2.1 Detect this machine's variant

Stash the variant name and the shared-library extension in two files; every step
below re-reads them, so nothing further down has to know which platform it is on.

```bash
case "$(uname -s) $(uname -m)" in
  "Linux x86_64")   echo linux-x86_64  > variant.txt; echo so    > ext.txt ;;
  "Linux aarch64")  echo linux-arm64   > variant.txt; echo so    > ext.txt ;;
  "Darwin arm64")   echo darwin-arm64  > variant.txt; echo dylib > ext.txt ;;
  "Darwin x86_64")  echo darwin-x86_64 > variant.txt; echo dylib > ext.txt ;;
  *) echo "unsupported platform: $(uname -sm)" >&2; exit 1 ;;
esac
echo "variant: $(cat variant.txt)  ext: $(cat ext.txt)"

```

### 2.2 Create the package

`lgx add` takes a whole payload directory and the `main` inside it, records the
entry point in the manifest's `main` map, and computes the content hashes over
everything it stored.

```bash
V="$(cat variant.txt)"; E="$(cat ext.txt)"
mkdir -p payload/lib
echo 'stub plugin' > "payload/greeter_plugin.$E"
echo 'stub helper' > "payload/lib/libhelper.$E"
lgx create greeter
lgx add greeter.lgx --variant "$V" --files payload --main "greeter_plugin.$E" -y
```

### 2.3 Sign it

`lgx keygen` writes an Ed25519 keypair and the `did:jwk:` DID that names its
public half; `lgx sign` writes a `manifest.sig` carrying that DID and a signature
over the manifest's bytes. We keep the DID in a file — it is the thing we will
pin with later, and the whole point is that it comes from *us*, not from the
package.

```bash
lgx keygen --name publisher --output-dir ./keys
cp ./keys/publisher.did publisher-did.txt   # every --did below re-reads this
lgx sign greeter.lgx --key publisher --keys-dir ./keys --name "Logos Demo"
```

---

## Step 3: Install it the way a package manager does

An `.lgx` is a gzipped tar, so we can do the install by hand and see exactly what one
is. A package manager takes three things out of the archive and drops them in a
directory:

```
modules/greeter/
  manifest.json          # from the archive root, byte for byte
  manifest.sig           # likewise — the signature is over those bytes
  greeter_plugin.so      # variants/<this variant>/… flattened to the root
  lib/libhelper.so
  variant                # written by the installer: which variant was chosen
```

The other variants are dropped on the floor. That is why the `variant` file has to be
written: nothing else in the directory records which of the package's platforms this
install actually is.

### 3.1 Look inside the archive

```bash
tar -tzf greeter.lgx
```

### 3.2 Unpack one variant into a module directory

```bash
V="$(cat variant.txt)"
mkdir -p stage modules/greeter
tar -xzf greeter.lgx -C stage
cp stage/manifest.json stage/manifest.sig modules/greeter/
cp -R "stage/variants/$V/." modules/greeter/
echo "$V" > modules/greeter/variant
```

---

## Step 4: Verify the install

`lm verify` reports three things and then a verdict.

**Integrity** re-hashes the installed files and compares them with the manifest's
hashes — not only the variant's own subtree, but the package-wide entries too, so a
package with root-level assets has to come out right on both.

**Signature** is `not checked` here, and says why: no DID was named. `lm` shows the
DID `manifest.sig` *claims*, so a human can decide what to pin, and labels it as a
claim.

**Findings** are the individual failures, and `RESULT` is the exit status in words.

### 4.1 Verify the untouched install

```bash
lm verify modules/greeter
```

---

## Step 5: Name the signer you expect

`--did` is how you ask the signature question, and it is deliberately the *only* way:
there is no flag that means "is the signature good?" without naming whose it must be.
A signature only ever proves that *this* key signed *these* bytes, and the key has to
come from somewhere you trust — a keyring, a policy, a `dependencies` entry — never
from the file being checked.

### 5.1 The right DID

```bash
lm verify modules/greeter --did "$(cat publisher-did.txt)"
```

### 5.2 Somebody else's DID

Generate a second, unrelated key and pin that instead. The package is untouched
and its integrity is still fine — it simply is not from the publisher we asked
for, and that is a failure.

```bash
lgx keygen --name someone-else --output-dir ./keys
lm verify modules/greeter --did "$(cat ./keys/someone-else.did)"
```

### 5.3 A DID that is not a key at all

A malformed `--did` is reported as a fault in the *question*, not an answer about
the package — you have not learned anything about this signature, so it does not
pass.

```bash
lm verify modules/greeter --did "did:jwk:not-a-key"
```

---

## Step 6: Tamper with the payload

Append a byte to an installed file — the sort of thing a bad patch, a partial
download or a hostile edit produces — and the hashes stop agreeing. Note which check
moves: **integrity** fails, while the **signature** is still perfectly valid, because
the signature is over `manifest.json` and the manifest is what was not touched. The
two checks answer different questions and the report keeps them apart.

### 6.1 Modify an installed file

```bash
cp -R modules/greeter modules/tampered
printf 'x' >> "modules/tampered/lib/libhelper.$(cat ext.txt)"
lm verify modules/tampered --did "$(cat publisher-did.txt)"
```

### 6.2 The same verdict for a script

`--json` replaces the `RESULT:` line but not the exit status, so a script gates on
the same verdict a human reads. Note `signature.state` here: this run named no
DID, so the signature was not checked — `"claimed_did"` is reported beside it and
labelled as a claim, never as a `signer`.

```bash
lm verify modules/tampered --json
```

---

## Step 7: Relabel the package

This is the case the `--did` design exists for. Edit `manifest.json` — bump the
version, rename the module, whatever a repackager would do — and leave `manifest.sig`
exactly where it is. Nobody touched the payload, so the content hashes still agree,
and `manifest.sig` still names the original publisher's DID.

Run `verify` with no `--did` and it **passes**. Everything it was asked about is
fine. The signature was never checked, because nothing said which key it had to be —
and taking that key from `manifest.sig` would prove nothing, since the DID sitting in
that file is written by whoever wrote the file.

Name the publisher's DID and the same directory fails immediately: those manifest
bytes are not the ones that key signed.

### 7.1 Rewrite the manifest, keep the signature

```bash
cp -R modules/greeter modules/relabelled
sed 's/"version": "0.0.1"/"version": "9.9.9"/' modules/greeter/manifest.json \
  > modules/relabelled/manifest.json
```

### 7.2 It passes, until you ask

```bash
lm verify modules/relabelled
```

### 7.3 Pin the publisher and it fails

The DID `manifest.sig` claims has not changed — it is still the publisher's. Only
the bytes it was supposed to cover have. Had `lm` used the claimed DID as the key
to check against, a repackager would only have had to swap in their own signature
*and* their own DID, and this directory would have verified.

```bash
lm verify modules/relabelled --did "$(cat publisher-did.txt)"
```

---

## Step 8: What verify refuses

Both refusals keep the two shapes of path apart. `verify`'s checks are over
`manifest.json` and the files it covers, so there is nothing for it to do to a bare
plugin; and `--did` names whose signature to check, which is a question only `verify`
answers.

### 8.1 verify on a plugin file

```bash
lm verify "modules/greeter/greeter_plugin.$(cat ext.txt)"
```

### 8.2 --did on a command that is not verify

```bash
lm metadata modules/greeter --did "$(cat publisher-did.txt)"
```

---

## Recap

You packaged and signed a module with `lgx`, installed it by hand the way a package
manager would, and put `lm verify` through every verdict it has:

| What you ran | Integrity | Signature | Exit |
|---|---|---|---|
| untouched install | `ok` | `not checked` — no DID named | `0` |
| `--did` the publisher's | `ok` | `ok` | `0` |
| `--did` someone else's | `ok` | `MISMATCH` | `1` |
| `--did` a malformed DID | `ok` | `bad --did` | `1` |
| a modified payload file | `MISMATCH` | `ok` | `1` |
| a relabelled manifest, no `--did` | `ok` | `not checked` | `0` |
| a relabelled manifest, `--did` pinned | `ok` | `MISMATCH` | `1` |

The last two rows are the lesson. Integrity is self-contained — the manifest carries
the hashes and the payload either answers to them or does not. Authenticity is not:
it needs a key from outside the package, and the only place `lm` will take one from
is your `--did`.

Reading a module — its metadata, its API, and the four ways an install can fail to
name a plugin — is the companion [inspection doc-test](lm-inspect.md).
