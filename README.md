# Fluorine Manager

Fluorine Manager an attempt at porting MO2 to linux with FUSE as the VFS system.

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
docs/      Notes and tracking
```
