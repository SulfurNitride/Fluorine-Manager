# Fluorine Manager

Fluorine Manager an attempt at porting [MO2 (Mod Organizer 2)](https://github.com/ModOrganizer2/modorganizer) to linux with FUSE as the VFS system.

NOTE: This is primarily for my personal use but I will see about fixing issues if I can. I use Claude/Codex, if you don't like AI please don't use this application. I'm looking for feedback not hate.

<img width="4134" height="2453" alt="image" src="https://github.com/user-attachments/assets/887e042a-db74-43f8-a5aa-735d18d94cc9" />


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

## Installing and Running
Download the latest flatpak from the [releases](https://github.com/SulfurNitride/Fluorine-Manager/releases) and after you download it.

You are able to install it with this command: `flatpak install --user fluorine-manager.flatpak`

You can then get started with: `flatpak run com.fluorine.manager` or you should be able to find it in your apps drawer.

More information can be found in the [FAQ](https://github.com/SulfurNitride/Fluorine-Manager/blob/main/docs/FAQ.md).

You can find me in the [NaK Discord](https://discord.gg/9JWQzSeUWt)

If you want to support the things I put out, I do have a [Ko-Fi](https://ko-fi.com/sulfurnitride) I will never charge money for any of my content.

## Building

Both builds install to `~/.local/share/fluorine/` — the same location, so Flatpak and native share instances, plugins, and configs.

NOTE: Native builds are available but are not officially supported by me. Please do not make an issue relating to something not working on the native build if it's working on the flatpak.
### Flatpak (recommended for end users)

```bash
./build-flatpak.sh bundle
# Produces a .flatpak file you can install with:
# flatpak install --user fluorine-manager.flatpak
```

## Known Limitations

- Some third-party MO2 plugins are Windows-only and will fail on Linux (for example DLL/ctypes `windll` assumptions).
- Themes are currently not working as intended.
- Fluorine Manager requires Steam to be installed on the user's computer so that Proton is available to it.
- When using the native build, the following libraries are not bundled and must be made available to Fluorine Manager(on most systems, simply install the required packages. On NixOS, make the packages containing these libraries available under "programs.nix-ld.libraries = [];" in configuration.nix, solutions for NixOS like steam-run currently run into FUSE permission issues):
  - libEGL
  - libGL
  - libGLX
  - libstdc
  - libX11
  - libXkbcommon

## Project Layout

```text
libs/      MO2 sub-libraries
src/       Main organizer source
docs/      Notes and tracking
```
