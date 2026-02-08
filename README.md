# Fluorine Manager

An attempt to port MO2 and all of its features over to Linux with a VFS replacement with the use of FUSE.

If you would like to help you can join the discord for the prereleases. [NaK Discord](https://discord.gg/9JWQzSeUWt) 

## Status

Currently, FUSE VFS is working, Root Building is working as well. NaK for prefix generation and deps install. 

Whats not working as of right now: NXM handler, drag and drop install mods.

Whats planned: Improvements to the installer flow, BSA loose file loading to show conflicts. And others to come.

## Build

```bash
cargo build --release
```

## Run

```bash
cargo run -p mo2gui
```
