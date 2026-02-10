# Fluorine Manager

An attempt to port MO2 and all of its features over to Linux with a VFS replacement with the use of FUSE.

See [`docs/FAQ.md`](https://github.com/SulfurNitride/Fluorine-Manager/blob/main/docs/FAQ.md) for common questions and troubleshooting.

If you would like to help you can join the discord for the prereleases. [NaK Discord](https://discord.gg/9JWQzSeUWt) 

If you want to support the things I put out, I do have a [Ko-Fi](https://ko-fi.com/sulfurnitride) I will never charge money for any of my content.

## Status

Currently, FUSE VFS is working, Root Building is working as well. NaK for prefix generation and deps install. Conflicts system. NXM handling. 

What's not implemented FOMOD handling. BSA conflicts system. Mod info to show conflicting files.

What cannot be implemented: Drag and drop install mods (Slint UI limitation)

Whats planned: Improvements to the installer flow, BSA loose file loading to show conflicts. And others to come.

## Build

```bash
cargo build --release
```

## Run

```bash
cargo run -p mo2gui --bin fluorine-manager
```
