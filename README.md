# Fluorine Manager (C++ / Qt Core)

Fluorine Manager an attempt at porting MO2 to linux with FUSE as the VFS system.

## Current Status

- Core app builds and runs on Linux.
- NaK integration is wired for game/proton detection and dependency handling.
- Linux-native game plugins (`libgame_*.so`) are supported.
- Portable instances are supported via local `ModOrganizer.ini` detection.

## Runtime Dependencies (Linux)

- `icoutils` is required for Windows `.exe` icon extraction in the UI.
- Without `icoutils`, MO2 falls back to the default missing icon.

Install:

```bash
# Arch
sudo pacman -S icoutils
```

## FUSE Permissions

- Users only need to change `/etc/fuse.conf` when MO2 mounts with `allow_other` (or `allow_root`).
- If `allow_other` is used, uncomment `user_allow_other` in `/etc/fuse.conf` once (system-wide).

## Example

`#user_allow_other` to `user_allow_other` if its missing please add it.

## Build

```bash
cmake -B build
cmake --build build -j"$(nproc)"
```

## Known Limitations

- Some third-party MO2 plugins are Windows-only and will fail on Linux (for example DLL/ctypes `windll` assumptions).
- Themes are currently not working as intended.

## Project Layout

```text
libs/      MO2 sub-libraries
src/       Main organizer source
patches/   Linux-specific patches
docs/      Notes and tracking
```
