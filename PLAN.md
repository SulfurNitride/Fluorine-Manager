# MO2 Linux Slint Port -- Implementation Plan

## Context

The MO2 Linux project (`/home/luke/Documents/MO2 Linux/MO2Linux/`) is a Rust/Slint rewrite of Mod Organizer 2 for Linux. The core infrastructure (config parsing, instance management, FUSE VFS) is solid with 168 passing tests across 3 crates (mo2core: 140, mo2fuse: 28, mo2gui: 0). The Slint GUI is at ~3500 lines with working Mod List, Plugins, Archives, Data, Downloads, Saves tabs, filter panel, menu bar, settings dialog, and Wine/Proton integration via NaK + Fluorine Manager.

## Reference Codebases
- Original MO2 Qt/C++ source: `/home/luke/Documents/MO2 Linux/modorganizer-master/`
- NaK (game detection, Proton, prefix management): `/home/luke/Documents/NaK-main/` (git: github.com/SulfurNitride/NaK)
- BSA/BA2 Handler: `/home/luke/Documents/Rust BSA BA2 Packer And Unpacker/` (crate: `bsa-ba2-tool`)
- CLF3 (Wabbajack installer, file utils): `/home/luke/Documents/Wabbajack Rust Update/clf3/` (has reflink-copy, paths, game_finder)
- Root Builder reference: https://github.com/Kezyma/ModOrganizer-Plugins (archived at `/tmp/kezyma-plugins/`)
- Portable MO2 with plugins: `/home/luke/Downloads/Mod.Organizer-2.5.2/`

## Architecture Notes

### How Windows MO2 VFS Works (USVFS)
- `OrganizerCore::fileMapping()` builds source→dest mappings using `game->getModMappings()`
- Default mapping: `"" -> [dataDirectory()]` — root of each mod maps to game's Data/
- `UsvfsConnector::updateMapping()` installs mappings before launch
- `spawn.cpp`: `usvfsCreateProcessHooked()` injects DLL to intercept Windows file I/O
- Game sees virtual files at real paths; mod files appear to be in Data/

### Linux FUSE Equivalent — Full-Game VFS
- **While MO2 is open**: VfsTree in memory serves Data tab, conflict detection, file browsers — NO mount needed
- **During launch only**: FUSE mount at `<instance>/VFS_FUSE/` serving the **entire game** merged with mods
- Real game directory is NEVER modified — VFS_FUSE/ is a read-only merged view
- Base layer: entire game dir (all files including exe, Data/, DLLs, configs)
- Mod layer: regular mod files placed under `Data/` prefix; `Root/` folder contents placed at VFS root (alongside exe)
- Overwrite layer: mirrors full VFS structure (highest priority)
- Launch from VFS_FUSE/ instead of real game path
- Root Builder file-copying is UNNECESSARY — Root/ files are served as VFS layers directly

### Stock Game Folder Support
- Instance config stores `gamePath` — can point anywhere, not just Steam paths
- All VFS/Root Builder operations use configured game path, not hardcoded locations
- Wizard/settings allow user to set custom game directory

### File Copy Strategy (Linux-only, no Windows/Mac)
- **FUSE VFS eliminates most file copying** — mods are served directly from their source locations
- **Reflink copy** (CoW) available via `reflink-copy` crate for staging flush and other copy needs
- **Rename** (`fs::rename()`) when moving files on same filesystem (instant)
- COW writes during gameplay go to staging dir, flushed to overwrite/ on unmount

### All Executables Route Through VFS
- FUSE mount is filesystem-level, not process-level (unlike Windows USVFS)
- **Any process** reading from VFS_FUSE/ sees the complete merged game view automatically
- This means xEdit, LOOT, BodySlide, DynDOLOD — all work without per-process hooking
- ALL launches (game AND tools) go through the same mount lifecycle:
  FUSE mount → launch executable from VFS_FUSE/ → wait → unmount → flush staging
- Root/ files (SKSE, ENB) are VFS layers, not file copies — zero deployment overhead
- This is an **advantage** over Windows MO2 — no DLL injection, no per-process setup, no file copying

### Mod List Separators
- MO2 separators in modlist.txt: lines with `_separator` prefix (e.g., `+_separator_Visuals`)
- Display as colored dividers/group headers in the mod list
- Not real mods — just visual organization markers
- Support: create, rename, delete, drag-drop reorder
- Collapsible groups (click separator to show/hide mods underneath)

---

## Phase 1: Plugins Tab + Mod List Persistence -- COMPLETE

**Status**: Done. 104 tests passing, clean build.

- Mod list persistence via `save_active_modlist(instance)` after toggle
- Full Plugins tab: sortable columns, CheckBox, bold masters, color-coded types
- Plugin toggle persists to `plugins.txt`
- Overwrite folder pinned at bottom, resizable columns, column chooser

---

## Phase 2: Filter Panel + Menu Bar + Enhanced Mod List Columns -- COMPLETE

**Status**: Done. 104 tests passing, clean build.

- 10-column mod list (Name, Flags, Conflicts, Category, Content, Priority, Type, Version, Nexus ID, Notes)
- Filter/Categories panel with tree-like categories, content, state sections
- Menu bar with File, Tools, View, Help menus

---

## Phase 3: Archives + Data + Downloads + Saves Tabs -- COMPLETE

**Status**: Done. 113 tests passing (97 mo2core + 16 mo2fuse), clean build.

- Archives tab scanning .bsa/.ba2 files across mods
- Data tab with VfsTree flatten/expand, conflict origins
- Downloads tab with meta parsing, install status
- Saves tab with Bethesda save parsing, profile-local saves

---

## Phase 4: Wine/Proton Integration -- COMPLETE

**Status**: Done. 124 tests passing (108 mo2core + 16 mo2fuse), clean build. (Now 157 with Phase 5A-C.)

- NaK integration for game detection and Steam proton discovery
- Fluorine Manager (global Wine/Proton prefix with dependency installation)
- Process launcher with Proton support (`proton run <exe>`)
- Wine prefix helpers, FUSE stale mount cleanup

---

## Phase 5: VFS Launch Integration + Root Builder — COMPLETE

This is the critical phase that makes mod loading actually work. The full-game VFS approach:
1. **FUSE VFS at `<instance>/VFS_FUSE/`** — merges entire game directory with mods into one view
2. **Root/ as VFS layers** — files from mods' `Root/` folders served at game root level in VFS (no file copying)
3. **Real game directory is NEVER modified**

### 5A. GameDef Trait -- COMPLETE

**File**: `crates/mo2core/src/gamedef.rs`

**Status**: Done. 13 tests passing.

- `GameId` enum for 13 known Bethesda games
- `GameDef` struct with static metadata: binary_name, data_dir_name, steam_app_id, gog_app_id, ini_files, documents_subdir
- `from_instance()` — resolves GameDef from an Instance's config
- `detect()` — auto-detects game path via `nak_rust::game_finder::find_game_install_path`
- `data_directory()`, `documents_directory()`, `binary_path()` — path helpers

### 5B. MountManager (Full-Game VFS) -- COMPLETE

**File**: `crates/mo2fuse/src/mount_manager.rs`

**Status**: Done. 4 tests passing.

Simple `MountManager` that mounts a full-game VFS at `<instance>/VFS_FUSE/`:

```
MountManager:
  mount(game_dir, data_dir_name, mods) -> Result<()>
    1. Build full-game VFS tree (game dir as base + mods under Data/ + Root/ at root + overwrite)
    2. Clean up stale FUSE mount if present
    3. Mount FUSE at VFS_FUSE/

  unmount() -> Result<()>
    1. Drop FUSE session
    2. Flush staging writes to overwrite/

  rebuild(game_dir, data_dir_name, mods) -> Result<()>
    - Live-refresh VFS tree without unmounting (for mod list changes in UI)

  vfs_binary_path(binary_name) -> PathBuf
    - Returns VFS_FUSE/<binary_name> for launching

  vfs_data_dir(data_dir_name) -> PathBuf
    - Returns VFS_FUSE/<data_dir_name> for tools that need data path
```

- Real game directory is NEVER modified — no backup/rename needed
- Drop impl ensures cleanup on panic
- Uses `build_full_game_vfs()` from overlay.rs

### 5C. Root Builder Scanning Utilities -- COMPLETE

**File**: `crates/mo2core/src/rootbuilder.rs`

**Status**: Done. 7 tests passing.

With the full-game VFS approach, Root/ files are served as VFS layers directly — no file
copying, symlinking, backup, or cleanup needed. This module provides scanning utilities
for the UI:

- `has_root_mods(instance)` — any enabled mods have Root/ folders?
- `mods_with_root(instance)` — list mod names with Root/ folders
- `list_root_files(mod_path)` — enumerate files in a mod's Root/ folder
- `has_invalid_root_contents(mod_path)` — Root/ contains Data/ or fomod/ (invalid)
- `is_data_content(relative_path)` — file belongs in Data/, not Root/ (UI warning)
- `redirect_executable(exe_path, instance)` — map mod Root/ exe path to VFS equivalent

### 5D. Wire VFS into Launch Flow -- COMPLETE

**Files**: `crates/mo2core/src/launcher/vfs.rs` (new), `crates/mo2gui/src/main.rs` (modified)

**Status**: Done. 9 new tests passing (vfs path translation).

Integrated MountManager into ALL launch callbacks with VFS path translation:

- **AppState** now holds `mount_manager: Option<MountManager>` (replaced FuseController)
- **`auto_mount_vfs()`** uses MountManager with GameDef for data_dir_name resolution
- **`build_launch_config_vfs()`** translates exe/working-dir paths through VFS:
  - Exe inside game dir → `VFS_FUSE/<relative>`
  - Exe in mod Root/ → `VFS_FUSE/<relative_to_Root>` (via redirect_executable)
  - External tool → original path unchanged
  - If no working dir set but exe is VFS-managed → uses VFS mount point as cwd
- **`rebuild_vfs_if_mounted()`** uses MountManager.rebuild() for live refresh
- **VFS path translation module**: `launcher/vfs.rs` with `translate_exe_to_vfs()`,
  `translate_working_dir_to_vfs()`, `is_vfs_executable()` + 9 tests
- VFS auto-mounts on instance load and before every launch (idempotent)
- VFS persists between launches (user can toggle via status bar)

#### Additional launch-flow work done:
- **Plugins.txt deployment**: `deploy_plugins_to_prefix()` writes managed Plugins.txt
  to Wine prefix `AppData/Local/<game>/Plugins.txt` before every launch (both `on_play`
  and `on_run_tool` callbacks). Format: `*Plugin.esp` (enabled), `Plugin.esp` (disabled).
  Uses `GameDef.my_games_folder` for the AppData subfolder name.
- **Plugin list rewrite**: `PluginList::build()` now separates game plugins from mod plugins.
  Game Data/ directory is scanned for base .esm/.esp/.esl files. Game .esm files get lowest
  priorities (loaded first), then .esl, then .esp, then mod plugins. Plugin discovery works
  even when loadorder.txt is empty.
- **Root+Data mod handling**: `build_full_game_vfs()` correctly handles mods with both
  `Root/` and `Data/` subfolders (like SKSE) — no double-prefixing of paths.
- **Mod toggle by name**: `toggle-mod` callback now passes mod name (string) instead of
  index (int) to fix incorrect toggling after sorting/filtering.

### KNOWN BUGS — RESOLVED

#### BUG 1: Mod list only shows one mod at a time — FIXED
- **Resolution**: Replaced ListView with ScrollView + VerticalBox. Layout now renders all entries correctly.

#### BUG 2: Mods not loading in-game — FIXED
- **Resolution**: Fixed by user.

### 5E. Mod List Separator Support -- COMPLETE

**Files**: `crates/mo2core/src/config/modlist.rs`, `crates/mo2gui/ui/app.slint`, `crates/mo2gui/src/main.rs`

**Status**: COMPLETE — base separators (parse, display, create, rename, delete, reorder, persistence) + collapsible groups.

- **Collapsible groups**: DONE
  - `collapsed_separators: HashSet<String>` in `AppState` (session-only, same as MO2)
  - Chevron indicator (▶ collapsed, ▼ expanded) on separator rows
  - `toggle-separator-collapse` callback toggles state and rebuilds mod list
  - `apply_collapse_state()` post-processes mod entries to set `hidden-by-collapse`
  - Applied in all 6 mod list rebuild sites (refresh_ui, sort, filter, etc.)

### 5F. Root Tag -- COMPLETE

**File**: `crates/mo2gui/ui/app.slint`, `crates/mo2gui/src/main.rs`

**Status**: COMPLETE — "R" tag appended to Content column for mods with `Root/` directory.

### 5G. MO2 Runtime Lock + Profile-Local Saves/INI Parity -- COMPLETE

**Status**: COMPLETE (177 tests passing)

#### 5G.1 Profile-specific Saves + INI behavior -- COMPLETE

**Files**:
- `crates/mo2gui/src/main.rs`
- `crates/mo2core/src/launcher/wine_prefix.rs`

Implementation:
- `deploy_profile_ini()`: copies profile INI files to prefix My Games folder when `LocalSettings=true`
- `deploy_profile_saves()`: symlinks prefix Saves/ → profile saves/ dir when `LocalSaves=true`
- `sync_ini_back_to_profile()`: copies INI files from prefix back to profile after process exits
- Wired into `deploy_plugins_to_prefix()` (pre-launch) and `start_process_monitor` (post-exit sync)
- `IniSyncInfo` struct captures sync-back context before launch, passed to process monitor timer

#### 5G.2 Global "Locked" app state while launched process is running -- COMPLETE

**Files**:
- `crates/mo2gui/ui/app.slint`
- `crates/mo2gui/src/main.rs`

Implementation:
- `locked_pid: Option<u32>` in AppState tracks active process
- `is_process_alive(pid)` checks `/proc/<pid>` existence
- `start_process_monitor()` uses `slint::Timer` polling every 2s, unlocks when process exits
- Concurrent launches blocked while locked
- UI: `locked` property disables Run button, shows "Locked: <tool>" in status bar
- Force Unlock callback stops timer and clears locked state
- On unlock: refreshes conflict cache and mod list (in case overwrite changed during run)

### 5H. Core MO2 Workflow Parity: Conflicts + Installer + Priority Reordering

**Status**: ALL COMPLETE (5H.1 conflicts, 5H.2 installer/FOMOD, 5H.3 drag-drop reorder)

#### 5H.1 Conflict system (winning/losing by overlap) -- COMPLETE

**Files**:
- `crates/mo2core/src/conflict/mod.rs`
- `crates/mo2gui/src/main.rs`
- `crates/mo2gui/ui/app.slint`

Implementation:
- Core `detect_conflicts()` walks all mod directories, builds file→owners map, determines winners by priority
- `ModConflicts` struct with `winning`/`losing` vectors of `FileConflict`
- Mod list displays "W:N L:M" in Conflicts column with color coding (green/red/orange)
- **Inspectable details**: selecting a mod shows conflict info bar below mod list:
  - "Wins over: ModA, ModB" (green) — unique opponent names from winning conflicts
  - "Loses to: ModX, ModY" (red) — unique opponent names from losing conflicts
- `winning_opponents` and `losing_opponents` fields on ModEntry
- Conflict cache refreshed on mod toggle, priority change, profile switch, and process exit

#### 5H.2 Mod installer pipeline + FOMOD handler -- COMPLETE (core)

**Files**:
- `crates/mo2core/src/install/mod.rs` — archive extraction, layout detection, simple install
- `crates/mo2core/src/install/fomod.rs` — FOMOD XML parser and resolution engine

Implementation:
- Archive extraction via `7zz` binary (supports zip, 7z, rar — all mod archive formats)
  - `find_7z()` checks bundled path, then system PATH
  - `extract_archive()` calls `7zz x` with UTF-8 charset
- Content layout detection: `detect_layout()` identifies Fomod, DataFolder, Bain, RootData, Unknown
  - Case-insensitive fomod/ directory search
  - Single-subfolder unwrapping (common pattern: archive has one top-level dir)
  - BAIN detection (numbered directories: 00, 01, 02)
  - Game data heuristic (esp/esm/bsa files, textures/meshes dirs)
- Simple install: `install_simple()` copies files based on detected layout
- FOMOD parser: `parse_module_config()` / `parse_module_config_str()`
  - Handles UTF-8 and UTF-16 (LE/BE with BOM) encoding
  - Parses: moduleName, requiredInstallFiles, installSteps, conditionalFileInstalls
  - InstallStep → OptionGroup (with GroupType) → PluginOption (with files, flags, type)
  - GroupTypes: SelectExactlyOne, SelectAny, SelectAtLeastOne, SelectAtMostOne, SelectAll
- FOMOD resolution: `resolve_installation()` combines required + selected + conditional files
  - `collect_flags()` and `visible_steps()` for multi-page wizard flow
- 17 new tests (8 FOMOD parser/resolver + 9 installer layout/install)
- GUI wizard integration: pending (core engine complete)

#### 5H.3 Drag-drop mod priority reordering (elevated requirement)

**Status**: COMPLETE — drag-drop reordering via TouchArea pointer-event/moved + up/down priority buttons.

**Files**:
- `crates/mo2gui/ui/app.slint`
- `crates/mo2gui/src/main.rs`
- `crates/mo2core/src/config/modlist.rs`

Implementation:
- Drag state: `drag-active`, `drag-from-index`, `drag-to-index`, `drag-start-y` properties
- TouchArea with `pointer-event` (down/up) + `moved` handlers — 5px threshold to distinguish click from drag
- Visual feedback: dragged row highlighted, drop target highlighted with accent-background, 3px indicator line at drop position
- Up/Down arrow buttons in mod list toolbar for keyboard-accessible reordering
- `reorder-mod(from, to)` callback: extracts mod names from UI model, moves in underlying priority order, saves modlist.txt, refreshes conflicts/VFS/UI
- Selection preserved after reorder (tracks moved mod's new index)

---

## Phase 6: BSA/BA2 Integration

### 6A. BSA Content in VFS Tree

**File**: `crates/mo2core/src/bsa_integration.rs` (new)

- Add `bsa-ba2-tool` as path dependency (extract archive module as library, separate from its Slint GUI)
- Use `list_archive_files()` to enumerate contents of .bsa/.ba2 files
- Add BSA file entries to VfsTree during `build_vfs_layers()`:
  - BSA files are virtual — they appear in the Data tab tree but are read from archives
  - Priority follows the owning mod's priority
  - Loose files always override BSA-packed files (same as MO2 behavior)
- Conflict detection should account for BSA contents (file in BSA vs loose file)

### 6B. BSA Extraction for FUSE

When FUSE serves a file that lives inside a BSA:
- `filesystem.rs` `read()` calls `extract_archive_file()` to get file data
- Cache extracted files to avoid repeated decompression
- Alternative: extract on-demand to staging directory, serve from there

### 6C. Archive Invalidation

Some games (Oblivion, Skyrim LE) need BSA invalidation to load loose files over BSA contents:
- Create/update `ArchiveInvalidation.bsa` or `ArchiveInvalidation.txt` as appropriate
- Game-specific logic via GameDef trait method `needs_archive_invalidation() -> bool`

---

## Phase 7: Python Plugin Bridge (PyO3)

### Why This Is Needed
- Hundreds of community MO2 Python plugins exist (FNIS tools, Synthesis patcher, wizard installer, etc.)
- Modlist authors depend on these — we can't rewrite them all in Rust
- Core plugins (game defs, installers) are native Rust; this bridge is for community/tool plugins

### 7A. mobase API in Rust

**File**: `crates/mo2core/src/plugin_bridge/mobase.rs` (new)

Implement the key `mobase` interfaces that Python plugins call:

```rust
// IOrganizer - the main API plugins receive
pub struct RustOrganizer { /* wraps Instance, AppState, etc. */ }
impl RustOrganizer {
    fn mods_path(&self) -> String;
    fn downloads_path(&self) -> String;
    fn overwrite_path(&self) -> String;
    fn profile_path(&self) -> String;
    fn profile_name(&self) -> String;
    fn managed_game(&self) -> PyObject;  // returns Python-wrapped GameDef
    fn mod_list(&self) -> PyObject;       // returns Python-wrapped IModList
    fn plugin_list(&self) -> PyObject;    // returns Python-wrapped IPluginList
    fn plugin_setting(&self, plugin: &str, key: &str) -> PyObject;
    fn set_plugin_setting(&self, plugin: &str, key: &str, value: PyObject);
    fn start_application(&self, exe: &str, args: Vec<String>) -> u64;
    fn wait_for_application(&self, handle: u64) -> (bool, i32);
    fn refresh_mod_list(&self);
}
```

### 7B. Plugin Loader

**File**: `crates/mo2core/src/plugin_bridge/loader.rs` (new)

- Scan `<instance>/plugins/` for .py files and directories with `__init__.py`
- Each plugin must export `createPlugin()` or `createPlugins()`
- Call `plugin.init(organizer_proxy)` with our Rust IOrganizer wrapper
- Categorize loaded plugins by type (IPluginTool, IPluginDiagnose, etc.)
- Register tool plugins in Tools menu, diagnose plugins for problem checking

### 7C. Plugin Settings Persistence

- Store per-plugin settings in `<instance>/plugins/data/<plugin_name>/settings.json`
- Exposed via `organizer.pluginSetting()` / `organizer.setPluginSetting()`

### 7D. Plugin Event Hooks

- `onAboutToRun(callback)` — called before launch (plugins can cancel)
- `onFinishedRun(callback)` — called after launch completes
- `onProfileChanged(callback)` — called on profile switch
- `onModListChanged(callback)` — called when mod list changes
- `onUserInterfaceInitialized(callback)` — called after GUI init

---

## Phase 8: Context Menus, Drag-Drop, Keyboard Shortcuts, Profile Dialog

### 8A. Context Menus — COMPLETE

Context menus implemented directly in app.slint (not a separate component). Mod list and plugin list right-click menus with all actions wired.

### 8B. Context Menu Enhancements, Plugin Reorder, Install Dialog — COMPLETE

- **Rename dialog**: Overlay dialog for renaming mods and separators (via context menu "Rename..." / "Rename Separator")
- **Install Mod dialog**: Full MO2-style install dialog with file tree, data directory detection, validation indicator. Triggered from context menu "Install Mod..." or file picker.
- **Create Separator** via context menu: Wired to same logic as old button callback
- **Plugin reorder**: `on_reorder_plugin` handler wired — drag-drop reorder persists to loadorder.txt + plugins.txt
- **Dead code removed**: Old `on_create_separator`, `on_rename_separator`, `on_delete_separator` callbacks removed (superseded by context menu dispatch)
- **`looks_like_game_data` made public** in mo2core::install for validation reuse
- **Install button** added to mod list filter bar as fallback entry point

### 8B.1. Drag-and-Drop Archive Install — PAUSED (deferred)

**Goal**: Drag a .zip/.7z/.rar from a file manager onto the MO2 window to trigger the Install Mod dialog.

**Problem**: Slint has no native external-file DnD API. However, Slint's winit backend exposes `on_winit_window_event` via the `unstable-winit-030` feature, which can intercept winit's `WindowEvent::DroppedFile(PathBuf)`.

**Implementation**:
- **Cargo.toml** (workspace): Changed `slint = "1.15"` → `slint = { version = "~1.15", features = ["unstable-winit-030"] }` — DONE
- **main.rs**: After creating `MainWindow`, call `ui.window().on_winit_window_event(...)` to intercept:
  - `WindowEvent::DroppedFile(path)` → check if archive extension (.zip/.7z/.rar/.tar/.gz), extract to temp dir, populate install dialog, show it
  - `WindowEvent::HoveredFile(path)` → optional: could show a visual indicator
- Uses `slint::winit_030::WinitWindowAccessor` trait
- Returns `EventResult::Reject` to let Slint continue processing other events

**Files to modify**:
- `crates/mo2gui/src/main.rs` — add `use slint::winit_030::WinitWindowAccessor;`, add `on_winit_window_event` handler after window creation
- Workspace `Cargo.toml` — already updated

**Current note (February 7, 2026)**:
- External drag-drop is wired in code path, but Linux desktop behavior is not yet reliable enough for release.
- Keep archive install via existing "Install Mod..." dialog as the supported path for now.
- Revisit after backend/event-loop validation pass and add explicit manual test cases per desktop environment.

### 8C. Keyboard Shortcuts

**File**: `crates/mo2gui/ui/app.slint`
- Add `FocusScope` at root of Page 1 with key handlers:
  - F5: Refresh
  - Ctrl+F: Focus filter input
  - Delete: Remove selected mod
  - Ctrl+S: Save (explicit)

### 8D. Profile Management Dialog

**File**: `crates/mo2gui/ui/app.slint`
- New overlay dialog (same pattern as Tool Editor / Settings): list of profiles, Create/Copy/Rename/Delete buttons
- Per-profile checkboxes: Local Saves, Local INIs, Archive Invalidation

**File**: `crates/mo2core/src/profile/mod.rs`
- Add `Profile::rename()` and `Profile::delete()` methods

**File**: `crates/mo2gui/src/main.rs`
- Wire up create/copy/rename/delete callbacks, refresh profile list after changes

---

## Phase 9: Polish

### 9A. Log Dock
- New `mo2gui/src/log_bridge.rs`: tracing Layer that captures log events into `Arc<Mutex<Vec<String>>>`
- Collapsible panel at bottom of main window, toggleable from View menu

### 9B. LCD-Style Counters
- Custom Slint component using monospace font styled to mimic 7-segment LCD for mod/plugin counts

### 9C. Icon System
- Create SVG icons in `crates/mo2gui/ui/icons/` for: content types, flags, actions, status indicators
- Replace text labels in toolbar/lists with `Image { source: @image-url(...) }`

### 9D. Enhanced Settings Dialog
- Expand to 8 tabs: General, Mod List, Paths, Wine/Proton, Root Builder, Nexus, Diagnostics, Theme

### 9E. Enhanced Executable Editor
- Add Up/Down reorder buttons, Reset button, Steam AppID field

### 9F. Enhanced Instance Manager
- Add: Rename, Explore (xdg-open), Open INI, filter/search, Convert portable/global

---

## File Organization (When Files Get Large)

Split `main.rs` (currently ~2200 lines, will grow to ~3000+) into:
```
mo2gui/src/
  main.rs              -- Entry point, MainWindow creation
  state.rs             -- AppState struct
  callbacks/
    mod.rs, instance.rs, wizard.rs, mod_list.rs, plugin_list.rs,
    tools.rs, settings.rs, launcher.rs
  helpers.rs           -- refresh_ui, build_mod_list, build_plugin_list
  log_bridge.rs
```

Split `app.slint` (currently ~3500 lines, will grow to ~5000+) into:
```
mo2gui/ui/
  app.slint            -- Root MainWindow, imports
  structs.slint        -- All shared struct definitions
  components/
    mod-list.slint, plugin-list.slint, filter-panel.slint,
    menu-bar.slint, context-menu.slint, tool-editor.slint,
    settings.slint, profile-dialog.slint, log-dock.slint
```

## Verification

After each phase:
1. `cargo build` -- Slint compilation catches UI struct mismatches
2. `cargo test` -- Maintain 157+ passing tests, add new ones per phase
3. Manual launch: `cargo run -p mo2gui` -- Verify UI renders correctly, callbacks work
4. Specifically test: load an existing MO2 instance (with mods, plugins, profiles), verify all new UI elements display correct data
