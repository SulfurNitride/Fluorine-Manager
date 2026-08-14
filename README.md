# Fluorine Manager

Fluorine Manager is a Linux port of [MO2 (Mod Organizer 2)](https://github.com/ModOrganizer2/modorganizer) with FUSE and experimental USVFS virtual-filesystem backends. A video guide for a quick setup can be found [here](https://youtu.be/yIZFUweb7v8).

[NEXUS RELEASE](https://www.nexusmods.com/site/mods/1997)

NOTE: This is primarily for my personal use but I will see about fixing issues if I can. I use Claude/Codex, if you don't like AI please don't use this application. I'm looking for feedback not hate.

 ![](https://github.com/user-attachments/assets/0b70e889-f472-451b-bf72-9b0e3e52321d)

## Current Status

- Core app builds and runs on Linux.
- Built-in game/Proton detection and dependency setup are available.
- Linux-native game plugins (`libgame_*.so`) are supported.
- Portable instances are supported via local `ModOrganizer.ini` detection.

## FUSE Permissions

Fluorine's native VFS does not request `allow_other` or `allow_root`, so it does
not require the system-wide `user_allow_other` setting in `/etc/fuse.conf`. The
user running Fluorine must be able to access `/dev/fuse`, and the host must
provide the `fusermount3` helper (normally installed by the distribution's
`fuse3` package).

## Virtual Filesystem Backends

FUSE remains the default. Each instance can optionally use the experimental
USVFS backend for Wine/Proton executables from **Settings > Wine/Proton > VFS**.
Native Linux launches still use FUSE, and games such as OpenMW that provide
their own VFS are unchanged. See the
[USVFS backend design and benchmarking guide](docs/usvfs-backend.md).

## Installing and Running

Download the Linux x86-64 application asset named
`fluorine-manager-<version>.tar.gz` from the
[Fluorine releases](https://github.com/SulfurNitride/Fluorine-Manager/releases).
Do not download GitHub's automatically generated source-code ZIP: it is source,
not a runnable application package.

Extract the archive into its own directory and run `./fluorine-manager`. The
launcher verifies the bundle, publishes the managed runtime to
`~/.local/share/fluorine/bin`, and runs that stable copy. The extraction can be
removed after a successful launch; deleting it does not uninstall Fluorine.

Before manually applying the first update from an older installation that did
not use the typed bundle manifest, close every running Fluorine Manager window.
The in-app updater waits for the old process automatically. Subsequent releases
serialize publication against the running application.

See [installation, updates, data locations, and removal](docs/installation.md)
and the [FAQ](docs/FAQ.md) for details.

You can find me in the [NaK Discord](https://discord.gg/9JWQzSeUWt)

If you want to support the things I put out, I do have a [Ko-Fi](https://ko-fi.com/sulfurnitride) I will never charge money for any of my content.

## Building

Fluorine Manager is built inside a Docker/Podman container — no host toolchain setup required.

**Prerequisites:** Docker or Podman

```bash
./build.sh              # Build the relocatable release directory
./build.sh installer    # Build the optional self-extracting installer
./build.sh all          # Build both outputs
./build.sh test         # Build and run the test suite
```

The default local output is the relocatable `build/fluorine-manager/` release
directory. CI wraps that directory
in the release `.tar.gz`; local builds deliberately avoid creating a second
multi-gigabyte archive.

Fluorine hashes changed catalog files concurrently using the available CPU
threads while retaining cached BLAKE3 digests for unchanged files. Uncached
BSA/BA2 member catalogs use at most four parsing workers to avoid excessive
storage contention. Warm reconciliation bulk-loads cached fingerprints,
writes only changed/deleted catalog rows, and reuses unchanged provider
Merkle roots. An unchanged immutable VFS Index generation is reused only
after full validation confirms the profile, resolved snapshot, provider
rows, consumer paths, and archive proof are identical. SQLite mutation and
new-generation publication remain serialized and crash-safe.

### Runtime Requirements (Mainly NixOS)

- Steam must be installed so that Proton is available.
- The following libraries are **not bundled** and must be available on your system:
  - `libEGL`
  - `libGL`
  - `libGLX`
  - `libfontconfig`
  - `libstdc++`
  - `libX11`
  - `libxkbcommon`
  - `wayland` (if using wayland)

On most distros these are already present or installable via your package manager.

**NixOS:** Use `nix-ld` to expose the unbundled libraries. Add them to `programs.nix-ld.libraries` in your `configuration.nix`:

```nix
programs.nix-ld.enable = true;
programs.nix-ld.libraries = with pkgs; [
  libGL
  libGLX
  fontconfig
  xorg.libX11
  libxkbcommon
  stdenv.cc.cc.lib  # libstdc++
];
```


## Known Limitations

- Some third-party MO2 plugins are Windows-only and will fail on Linux (for example DLL/ctypes `windll` assumptions).

## Project Layout

```text
libs/      MO2 sub-libraries
src/       Main organizer source
docs/      Notes and tracking
```
