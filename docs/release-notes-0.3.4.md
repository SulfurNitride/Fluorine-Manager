## Fluorine Manager 0.3.4

### Highlights

- Install Wabbajack modlists directly in Fluorine using the bundled CLF3 0.2.4 engine.
- Browse and filter the Wabbajack gallery, authorize Nexus downloads, and handle manual downloads from the installer.
- Resume interrupted installations and retry compatibility setup separately from modlist installation.
- Track FOMOD dependencies and review installer choices when relevant mods or plugins change.
- Improve VFS indexing, memory use, stale-mount recovery, and preservation of interrupted game writes.

### Wabbajack installation

- Added gallery search by title, author, and game; installed-game, official, availability, and NSFW filters; sorting by size, title, and archive count; and exact mod-name inclusion/exclusion filters.
- Added installation from gallery entries, URLs, and local `.wabbajack` files, with configurable instance, cache, and game paths.
- Added Nexus Premium authorization and an embedded authorization browser for regular accounts, with external-browser fallback and session controls.
- Added a manual-download queue with download-page and file-selection controls. CLF3 verifies supplied archives.
- Added folder and write-access checks, game-folder separation checks, and disk-space estimates including temporary extraction space.
- Added per-item progress, transfer speeds, pipeline counters, installation summaries, and log export with credential and signed-link redaction.
- Added persistent installation recovery, graceful cancellation, process failure handling, and a separate compatibility-setup retry.
- Fixed resume information failing to save by using an explicit Fluorine settings file and migrating previous saved jobs.
- Added completed-instance registration and an option to switch to the new instance.
- Bundled CLF3 improvements include failed archive/whole-file retries and correct extraction from Oblivion BSAs with out-of-order filename tables.

### Linux compatibility setup

- Viva New Vegas: deploy documented game-root files and apply the native Linux/Proton 4 GB patcher.
- Heartland Redux: deploy Game Folder Files and apply and verify the native Oblivion 4 GB patches and expected backups.
- Waters of Life: deploy required game-root files and report the remaining Anniversary Patcher step.
- Preserve authored game layouts and checkpoint root-file deployment so setup retries preserve already-patched executables.

### FOMOD and mod management

- Record supported FOMOD dependency states and selections, flag changed dependencies, and restore tracked selections during reinstalls.
- Added a FOMOD Reviews count/filter, warning indicators, and explanations of affected choices.
- Fixed recursive Overwrite moves and replacement handling while preserving the Overwrite root.
- Restore a download's installed status after reinstalling its mod.
- Clean up legacy invalid `*Overwrite` profile entries.
- Create portable launchers explicitly during instance creation, preserve existing launchers, and forward arguments correctly without modifying instances during import or inspection.

### VFS, runtime, and desktop fixes

- Reduced repeated archive indexing work and startup/VFS memory use; release splash resources earlier.
- Preserve filename display capitalization across overrides.
- Recover stale FUSE mounts through alternate paths before game validation, while protecting healthy mounts owned by another primary process.
- Recover non-conflicting staged writes after interruptions and preserve conflicting versions for reconciliation.
- Added optional memory and VFS audio-read diagnostics.
- Reworked Visual C++ and .NET installation around pinned, verified runtime packages.
- Install legacy DirectX through DXSETUP and replace the cached 32-bit 7-Zip helper.
- Fixed Unicode path decoding in Wine registry values and improved prefix-setup command lookup on NixOS.
- Respect the session's hard open-file limit when raising the launcher's soft limit.
- Fixed Wayland window-size and maximized-state restoration.
- Fixed compressed Skyrim save parsing and malformed-data handling.
- Enabled BethINI Pie for Oblivion.
- Removed filename-based OpenMW groundcover suggestions; explicit selections remain authoritative.

### Packaging and cleanup

- Removed the disabled Download Collection button and unused Nexus Collections implementation.
- Bundle CLF3, Qt WebEngine, and an archive-extraction helper; pin and record the CLF3 build revision.
- Reduce duplicate Python runtime code in the portable package.
- Refresh the version in cached build directories so incremental builds report the current release number.
- Added compatibility documentation, recipe/compiler tooling, and regression coverage.

### Notes

- Automatic post-install setup currently covers the specific actions listed above. Waters of Life still requires its Anniversary Patcher step; Morrowind75's interactive adapter and optional BSA decompression actions remain disabled.
- Curated-guide recipes and compiler tooling are retained in the repository, but the separate curated-guide installer interface is not enabled in the application build.

[Full changelog](https://github.com/SulfurNitride/Fluorine-Manager/compare/v0.3.3...v0.3.4)
