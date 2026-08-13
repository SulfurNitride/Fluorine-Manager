# Fluorine Manager

Fluorine Manager an attempt at porting [MO2 (Mod Organizer 2)](https://github.com/ModOrganizer2/modorganizer) to linux with FUSE as the VFS system. A video guide for a quick setup can be found [here](https://youtu.be/yIZFUweb7v8). 

[NEXUS RELEASE](https://www.nexusmods.com/site/mods/1997)

NOTE: This is primarily for my personal use but I will see about fixing issues if I can. I use Claude/Codex, if you don't like AI please don't use this application. I'm looking for feedback not hate.

 ![](https://github.com/user-attachments/assets/0b70e889-f472-451b-bf72-9b0e3e52321d)

## Current Status

- Core app builds and runs on Linux.
- NaK integration is wired for game/proton detection and dependency handling.
- Linux-native game plugins (`libgame_*.so`) are supported.
- Portable instances are supported via local `ModOrganizer.ini` detection.

## FUSE Permissions

- Users only need to change `/etc/fuse.conf` when MO2 mounts with `allow_other` (or `allow_root`).
- If `allow_other` is used, uncomment `user_allow_other` in `/etc/fuse.conf` once (system-wide).

## Example

`#user_allow_other` to `user_allow_other` if its missing please add it.

## Virtual Filesystem Backends

FUSE remains the default. Each instance can optionally use the experimental
USVFS backend for Wine/Proton executables from **Settings > Wine/Proton > VFS**.
Native Linux launches still use FUSE, and games such as OpenMW that provide
their own VFS are unchanged. See the
[USVFS backend design and benchmarking guide](docs/usvfs-backend.md).

## Installing and Running
Download the latest zip from the [releases](https://github.com/SulfurNitride/Fluorine-Manager/releases) and after you download it.

You are able to run it with this command: `./fluorine-manager` or by double-clicking it.

Before manually applying the first update from an older installation that did
not use the typed bundle manifest, close every running Fluorine Manager window.
The in-app updater waits for the old process automatically. Subsequent releases
serialize publication against the running application.

More information can be found in the [FAQ](https://github.com/SulfurNitride/Fluorine-Manager/blob/main/docs/FAQ.md).

You can find me in the [NaK Discord](https://discord.gg/9JWQzSeUWt)

If you want to support the things I put out, I do have a [Ko-Fi](https://ko-fi.com/sulfurnitride) I will never charge money for any of my content.

## Building

Fluorine Manager is built inside a Docker/Podman container — no host toolchain setup required.

**Prerequisites:** Docker or Podman

```bash
./build.sh              # Build portable .tar.gz
```

The default output is `build/fluorine-manager.tar.gz` — extract anywhere and run `./fluorine-manager`.

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
- Themes are currently not working as intended.

## Project Layout

```text
libs/      MO2 sub-libraries
src/       Main organizer source
docs/      Notes and tracking
```
