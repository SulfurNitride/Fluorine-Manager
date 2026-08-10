"""
game_openmw.py — Fluorine game plugin for The Elder Scrolls III: Morrowind run
under OpenMW (the native Linux engine).

Unlike the Windows Morrowind plugin this:
  - is a native Linux launch (no Proton/Wine) via isNativeLinux();
  - does NOT rely on Fluorine's FUSE VFS — OpenMW has its own VFS, so we hand it
    one data= directory per active mod (in priority order) plus the load order
    as content= lines, written into openmw.cfg right before launch. The FUSE
    mount is skipped for this game by returning usesVFS() == False (wired in the
    C++ core; harmless until then);
  - keeps OpenMW-native plugins (.omwaddon/.omwgame/.omwscripts) in the load
    order instead of dropping them, and routes groundcover plugins to
    groundcover= lines (listed in <profile>/groundcover.txt) so they don't tank
    performance as content= entries.
  - uses OpenMW's native config-directory chaining for MO2 profiles with local
    settings, making the profile OpenMW's writable user-config directory without
    copying or classifying settings/storage files, and synchronizes the
    launcher's separate content list with the generated native paths.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo, qInfo, qWarning

import mobase

from ..basic_game import BasicGame
from .openmw_support.flatpak_access import (
    PathRequirement,
    format_access_failures,
    merge_requirements,
    probe_flatpak_access,
)
from .openmw_support.openmw_cfg import (
    VANILLA_BSAS,
    find_openmw_cfg,
    is_openmw_player_stub,
    order_selected_files,
    parse_openmw_selection_chain,
    read_openmw_data_dirs,
    read_openmw_launcher_profile,
    read_profile_selector,
    read_selection_state,
    restore_profile_config_entries,
    rollback_file_changes,
    suspend_profile_config_entries,
    upgrade_selection_state,
    validate_file_roles,
    write_local_saves,
    write_openmw_cfg,
    write_openmw_launcher_cfg,
    write_profile_selector,
    write_selection_state,
    TransactionCleanupError,
    TransactionRollbackError,
)
from .openmw_support.game_plugins import (
    OpenMWGamePlugins,
    OpenMWPluginListLifecycle,
)

_FLATPAK_ID = "org.openmw.OpenMW"

# openmw.cfg candidates, Flatpak first (matches Amethyst's detection order).
_FLATPAK_CFG = (
    Path.home() / ".var" / "app" / _FLATPAK_ID / "config" / "openmw" / "openmw.cfg"
)


def _native_cfg() -> Path:
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = Path(xdg) if xdg else Path.home() / ".config"
    return base / "openmw" / "openmw.cfg"


def _flatpak_installed() -> bool:
    flatpak = shutil.which("flatpak")
    if flatpak is None:
        return False
    try:
        result = subprocess.run(
            [flatpak, "info", "--show-location", _FLATPAK_ID],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0


def _detect_openmw_cfg(prefer_flatpak: bool) -> Path | None:
    """Return the openmw.cfg to manage, or None if none exists yet."""
    return find_openmw_cfg(_native_cfg(), _FLATPAK_CFG, prefer_flatpak)


# Directories/extensions that mark a folder as valid OpenMW/Morrowind mod data.
_VALID_DIRS = {
    "bookart", "fonts", "icons", "meshes", "music", "shaders", "sound",
    "splash", "textures", "video", "mwse", "distantland",
    "l10n", "mygui", "scripts",  # OpenMW-native (Lua / localisation / GUI)
}
_PLUGIN_EXTS = {".esp", ".esm", ".omwaddon", ".omwgame", ".omwscripts"}

_SELECTION_STATE_FILE = "fluorine-openmw-selection.json"


class OpenMWModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        for entry in filetree:
            if entry.isDir():
                if entry.name().lower() in _VALID_DIRS:
                    return mobase.ModDataChecker.VALID
            else:
                if Path(entry.name().lower()).suffix in _PLUGIN_EXTS:
                    return mobase.ModDataChecker.VALID
        return mobase.ModDataChecker.INVALID


class OpenMWGame(BasicGame):
    Name = "OpenMW Support Plugin"
    Author = "Fluorine OpenMW contributors"
    Version = "0.1.0"

    GameName = "Morrowind (OpenMW)"
    GameShortName = "morrowind"
    GameNexusName = "morrowind"
    GameNexusId = 100
    GameSteamId = 22320
    # Detection only — the Steam Morrowind install owns Morrowind.exe. We never
    # launch it; executables() returns the native OpenMW binary instead and
    # isNativeLinux() keeps Proton out of the picture.
    GameBinary = "Morrowind.exe"
    GameLauncher = "openmw-launcher"
    GameDataPath = "Data Files"
    GameSaveExtension = "omwsave"
    GameSupportURL = "https://openmw.org/"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(OpenMWModDataChecker())
        self._openmw_game_plugins = OpenMWGamePlugins(organizer)
        if not self._register_feature(self._openmw_game_plugins):
            return False
        self._openmw_plugin_list_lifecycle = OpenMWPluginListLifecycle(
            self._openmw_game_plugins
        )
        if not self._register_feature(self._openmw_plugin_list_lifecycle):
            return False
        organizer.onAboutToRun(self._export_openmw_cfg)
        return True

    # OpenMW is always a native Linux launch — never Proton/Wine.
    def isNativeLinux(self) -> bool:
        return True

    # A persistent Plugins tab for OpenMW. PLUGINS_TXT tells the core to keep
    # the load order in plugins.txt/loadorder.txt (written by OpenMWGamePlugins
    # to the profile dir); _export_openmw_cfg then transcribes the active order
    # into openmw.cfg at launch.
    def loadOrderMechanism(self) -> mobase.LoadOrderMechanism:
        return mobase.LoadOrderMechanism.PLUGINS_TXT

    # Declaring LOOT makes Fluorine's native Sort button visible on the Plugins
    # tab. For every other game that button downloads the Windows LOOT.exe and
    # runs it under Proton on the merged VFS — impossible here (native Linux,
    # no VFS); sortToolName() below reroutes it to our libloot-based tool.
    def sortMechanism(self) -> mobase.SortMechanism:
        return mobase.SortMechanism.LOOT

    # The registered IPluginTool the Sort button runs instead of the LOOT.exe
    # flow (openmw_support/plugins/sort_with_loot.py). The core hides it from
    # the Tools menu; the Sort button is its only entry point.
    def sortToolName(self) -> str:
        return "OpenMW Sort With LOOT"

    # The base masters that ship with Morrowind; force-loaded and listed first.
    def primaryPlugins(self) -> list[str]:
        return ["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"]

    # Extend the plugin list to OpenMW-native formats so they appear in the tab.
    def pluginFileExtensions(self) -> list[str]:
        return ["esp", "esm", "omwgame", "omwaddon", "omwscripts"]

    def ignoredPluginFileSuffixes(self) -> list[str]:
        return [".omwaddon.esp", ".omwscripts.esp", ".omwgame.esp"]

    def genericPluginStateFollowsModState(self) -> bool:
        return False

    def parsePluginHeader(self, fileName: str) -> bool:
        return Path(fileName).suffix.lower() in {
            ".esm", ".esp", ".omwgame", ".omwaddon"
        }

    def enforcePluginRelationships(self) -> bool:
        return False

    # OpenMW manages its own VFS via data= dirs, so Fluorine must NOT FUSE-mount
    # over Data Files for this game.
    def usesVFS(self) -> bool:
        return False

    def documentsDirectory(self) -> QDir:
        return self.gameDirectory()

    def executables(self) -> list[mobase.ExecutableInfo]:
        out: list[mobase.ExecutableInfo] = []
        flatpak = shutil.which("flatpak")
        if _flatpak_installed() and flatpak:
            out.append(
                mobase.ExecutableInfo("OpenMW (Flatpak)", QFileInfo(flatpak))
                .withArgument("run")
                .withArgument(_FLATPAK_ID)
            )
            out.append(
                mobase.ExecutableInfo("OpenMW Launcher (Flatpak)", QFileInfo(flatpak))
                .withArgument("run")
                .withArgument("--command=openmw-launcher")
                .withArgument(_FLATPAK_ID)
            )
        launcher = shutil.which("openmw-launcher")
        if launcher:
            out.append(mobase.ExecutableInfo("OpenMW Launcher", QFileInfo(launcher)))
        openmw = shutil.which("openmw")
        if openmw:
            out.append(mobase.ExecutableInfo("OpenMW", QFileInfo(openmw)))
        return out

    # ------------------------------------------------------------------
    # openmw.cfg export (runs on every launch via onAboutToRun)
    # ------------------------------------------------------------------

    def _is_openmw_binary(self, app_name: str) -> bool:
        base = Path(app_name).name.lower()
        # 'flatpak' here is our OpenMW launcher (we only register it for OpenMW).
        return base in {"openmw", "openmw-launcher", "flatpak"}

    def _export_openmw_cfg(self, app_name: str) -> bool:
        # onAboutToRun fires for every launched program; only act for OpenMW.
        if not self._is_openmw_binary(app_name):
            return True
        state: dict | None = None
        try:
            organizer = self._organizer
            profile = organizer.profile()
            profile_dir = Path(profile.absolutePath())
            export_snapshot = self._openmw_game_plugins.exportSnapshot(
                organizer.pluginList()
            )
            state = export_snapshot["state"]
            available_keys = {
                name.casefold() for name in export_snapshot["available_plugins"]
            }
            enabled_keys = {
                name.casefold() for name in state["enabled_plugins"]
            }
            groundcover_keys = {
                name.casefold() for name in state["groundcover"]
            }
            enabled_available = [
                name
                for name in state["plugin_order"]
                if name.casefold() in available_keys
                and name.casefold() in enabled_keys
            ]
            content = [
                name
                for name in enabled_available
                if name.casefold() not in groundcover_keys
            ]
            active_groundcover = [
                name
                for name in state["groundcover"]
                if name.casefold() in available_keys
                and name.casefold() in enabled_keys
            ]
            local_settings = profile.localSettingsEnabled()
            local_saves = profile.localSavesEnabled()
            game_dir = Path(self.gameDirectory().absolutePath())
            data_files = game_dir / "Data Files"

            root_cfg = _detect_openmw_cfg(
                prefer_flatpak="flatpak" in Path(app_name).name.lower()
            )
            if root_cfg is None:
                qWarning(
                    "OpenMW: no openmw.cfg found. Run openmw-launcher once to "
                    "create it, then mods will be applied on the next launch."
                )
                return True
            profile_cfg = profile_dir / "openmw.cfg"
            profile_target = profile_cfg.resolve(strict=False)
            root_target = root_cfg.resolve(strict=False)
            chained_profile = local_settings and profile_target != root_target
            cfg = profile_cfg if chained_profile else root_cfg
            previous_profile_dir = read_profile_selector(root_cfg)
            if previous_profile_dir is not None:
                known_profiles: dict[Path, str] = {}
                for profile_name in organizer.profileNames():
                    known_profile = organizer.getProfile(profile_name)
                    if known_profile is None:
                        continue
                    known_path = Path(
                        known_profile.absolutePath()
                    ).resolve(strict=False)
                    if known_path in known_profiles:
                        raise ValueError(
                            "OpenMW profiles resolve to the same directory: "
                            f"{known_profiles[known_path]} and {profile_name}"
                        )
                    known_profiles[known_path] = str(profile_name)
                if previous_profile_dir not in known_profiles:
                    qWarning(
                        "OpenMW: ignoring previous Fluorine profile selector "
                        f"that is not a known profile: {previous_profile_dir}"
                    )
                    previous_profile_dir = None
            modlist = organizer.modList()

            # data=: vanilla Data Files first (lowest prio), then each active mod
            # in profile-priority order, then Overwrite last (wins). Mirrors the
            # ordering of AnyOldName3's MO2 exporter.
            data_dirs: list[Path] = [data_files]
            bsa_archives: list[str] = []

            def _scan_archives(path: Path) -> None:
                try:
                    entries = sorted(path.iterdir(), key=lambda p: p.name.lower())
                except OSError:
                    return
                bsa_archives.extend(
                    entry.name
                    for entry in entries
                    if entry.is_file() and entry.suffix.lower() == ".bsa"
                )

            # Data Files may contain enabled archives that are not one of the
            # canonical vanilla BSAs, such as Morrowind - Invalidation.bsa.
            _scan_archives(data_files)
            # Plugin scanning is discovery-only here. Version-3 state already owns
            # activation and order; scans only build data paths, collect archives,
            # and provide non-destructive groundcover hints.
            masters: list[str] = []         # .esm / .omwgame
            normal_plugins: list[str] = []  # .esp / .omwaddon

            def _scan_mod(path: Path) -> None:
                data_dirs.append(path)
                try:
                    entries = sorted(path.iterdir(), key=lambda p: p.name.lower())
                except OSError:
                    return
                for f in entries:
                    if not f.is_file():
                        continue
                    # Skip Kezyma "OpenMW Player" stub esps: empty TES3 esps named
                    # <name>.omwaddon.esp / <name>.omwscripts.esp that some MO2<->OpenMW
                    # tools drop next to the real .omwaddon/.omwscripts purely so the
                    # entry shows up in MO2's plugin list. The real file is scanned
                    # separately; loading the empty stub as content= is at best useless
                    # and at worst aborts OpenMW ("sub-record incomplete").
                    if is_openmw_player_stub(f.name):
                        continue
                    ext = f.suffix.lower()
                    if ext in {".esm", ".omwgame"}:
                        masters.append(f.name)
                    elif ext in {".esp", ".omwaddon"}:
                        normal_plugins.append(f.name)
                    elif ext == ".bsa":
                        bsa_archives.append(f.name)

            for name in modlist.allModsByProfilePriority():
                if name == "Overwrite":
                    continue
                try:
                    if not (modlist.state(name) & mobase.ModState.ACTIVE):
                        continue
                    mod = modlist.getMod(name)
                except Exception:
                    continue
                if mod is None:
                    continue
                mod_path = Path(mod.absolutePath())
                if mod_path.is_dir():
                    _scan_mod(mod_path)

            try:
                overwrite = modlist.getMod("Overwrite")
                if overwrite is not None:
                    ov_path = Path(overwrite.absolutePath())
                    if ov_path.is_dir() and any(ov_path.iterdir()):
                        _scan_mod(ov_path)
            except Exception:
                pass

            is_flatpak_launch = Path(app_name).name.casefold() == "flatpak"
            if is_flatpak_launch:
                flatpak_executable = (
                    app_name
                    if Path(app_name).is_file()
                    else shutil.which("flatpak")
                )
                if not flatpak_executable:
                    raise RuntimeError("Flatpak executable is unavailable")
                requirements = [
                    PathRequirement(path, False) for path in data_dirs
                ]
                requirements.append(
                    PathRequirement(root_cfg.parent, not chained_profile)
                )
                if chained_profile or local_saves:
                    requirements.append(PathRequirement(profile_dir, True))
                requirements = merge_requirements(requirements)
                failures = probe_flatpak_access(
                    flatpak_executable, _FLATPAK_ID, requirements
                )
                if failures:
                    qWarning(
                        "OpenMW: Flatpak sandbox access preflight failed:\n"
                        f"{format_access_failures(failures)}\n"
                        "Grant narrowly scoped access with 'flatpak override "
                        f"--user --filesystem=PATH {_FLATPAK_ID}'."
                    )
                    return False
                qInfo(
                    "OpenMW: Flatpak sandbox access preflight passed for "
                    f"{len(requirements)} path(s)."
                )

            state_path = profile_dir / _SELECTION_STATE_FILE
            state_dirty = False
            if chained_profile and suspend_profile_config_entries(
                state, profile_cfg
            ):
                state_dirty = True

            restore_current_profile = (
                not chained_profile and state["profile_config_terminal"]
            )
            if restore_current_profile:
                state["profile_config_terminal"] = False
                state_dirty = True

            previous_state_path: Path | None = None
            previous_cfg: Path | None = None
            previous_state = None
            previous_state_dirty = False
            restore_previous_profile = False
            if (
                previous_profile_dir is not None
                and previous_profile_dir != profile_dir.resolve(strict=False)
            ):
                candidate_state_path = (
                    previous_profile_dir / _SELECTION_STATE_FILE
                )
                candidate_state = read_selection_state(candidate_state_path)
                if candidate_state is not None:
                    previous_state_path = candidate_state_path
                    previous_cfg = previous_profile_dir / "openmw.cfg"
                    previous_state = candidate_state
                    previous_state_dirty = upgrade_selection_state(previous_state)
                    if previous_state_dirty:
                        previous_state["profile_config_terminal"] = True
                        qWarning(
                            "OpenMW: previous profile was terminal before nested "
                            "config backups were available; removed selectors "
                            "cannot be recovered automatically."
                        )
                    restore_previous_profile = previous_state[
                        "profile_config_terminal"
                    ]
                    if restore_previous_profile:
                        previous_state["profile_config_terminal"] = False
                        previous_state_dirty = True
            selected_archives = order_selected_files(
                bsa_archives, state["archives"]
            )

            # Helpful, non-destructive nudge: flag likely groundcover plugins the
            # user hasn't listed yet (we never reroute automatically).
            for p in masters + normal_plugins:
                low = p.casefold()
                if low not in groundcover_keys and (
                    "grass" in low or "groundcover" in low
                ):
                    qInfo(
                        f"OpenMW: '{p}' looks like a groundcover plugin. If it is, "
                        f"add it to {Path(self._organizer.profile().absolutePath()) / 'groundcover.txt'} "
                        "so it loads as groundcover= (better performance)."
                    )

            log_fn = lambda m: qInfo("OpenMW:" + m)
            launcher_cfg = cfg.parent / "launcher.cfg"
            file_roles = {
                "selection state": state_path,
                "active plugin projection": profile_dir / "plugins.txt",
                "plugin order projection": profile_dir / "loadorder.txt",
                "launcher config": launcher_cfg,
                "root config": root_cfg,
            }
            if profile_target != root_target:
                file_roles["profile config"] = profile_cfg
            if previous_cfg is not None and (
                restore_previous_profile or previous_state_dirty
            ):
                file_roles["previous profile config"] = previous_cfg
            if previous_state_path is not None and previous_state_dirty:
                file_roles["previous profile state"] = previous_state_path
            validate_file_roles(file_roles)

            with rollback_file_changes(file_roles.values()):
                if state_dirty and chained_profile:
                    write_selection_state(state_path, state)
                write_openmw_cfg(
                    cfg,
                    data_dirs=data_dirs,
                    content_plugins=content,
                    groundcover_plugins=active_groundcover,
                    fallback_archives=selected_archives,
                    replace_managed=chained_profile,
                    strip_config=chained_profile,
                    log_fn=log_fn,
                )
                write_openmw_launcher_cfg(
                    launcher_cfg,
                    data_dirs=data_dirs,
                    content_plugins=content,
                    fallback_archives=selected_archives,
                    log_fn=log_fn,
                )
                if chained_profile:
                    # The profile is the highest-priority OpenMW config directory.
                    # OpenMW consequently reads and writes settings.cfg, Lua storage,
                    # key bindings, shaders.yaml, launcher.cfg, and future config
                    # artifacts there without Fluorine needing a filename list.
                    write_local_saves(
                        profile_cfg,
                        profile_dir if local_saves else None,
                        log_fn=log_fn,
                    )
                    # Clear a stale root-level local-saves override before selecting
                    # the profile. Only Fluorine's marked block is removed.
                    write_local_saves(root_cfg, None, log_fn=log_fn)
                    write_profile_selector(
                        root_cfg,
                        profile_dir,
                        strip_managed=True,
                        log_fn=log_fn,
                    )
                else:
                    # Without a separate profile config, keep the generated config
                    # in OpenMW's normal user directory. Local saves remain
                    # independent of that choice.
                    write_local_saves(
                        root_cfg,
                        profile_dir if local_saves else None,
                        log_fn=log_fn,
                    )
                    # Remove a stale local-saves marker left in this profile if the
                    # profile previously used local settings.
                    if profile_target != root_target:
                        write_local_saves(profile_cfg, None, log_fn=log_fn)
                    write_profile_selector(root_cfg, None, log_fn=log_fn)
                    if restore_current_profile:
                        restore_profile_config_entries(
                            profile_cfg, state["profile_config_entries"]
                        )
                    if state_dirty:
                        write_selection_state(state_path, state)

                if (
                    restore_previous_profile
                    and previous_cfg is not None
                    and previous_state is not None
                ):
                    restore_profile_config_entries(
                        previous_cfg,
                        previous_state["profile_config_entries"],
                    )
                if (
                    previous_state_dirty
                    and previous_state_path is not None
                    and previous_state is not None
                ):
                    write_selection_state(previous_state_path, previous_state)
                persisted_selection = parse_openmw_selection_chain(
                    cfg,
                    self._openmw_game_plugins.pathTokensForLaunch(
                        is_flatpak_launch, app_name
                    ),
                )
                expected_content = []
                seen_content: set[str] = set()
                for name in [*self.primaryPlugins(), *content]:
                    folded = name.casefold()
                    if folded not in seen_content:
                        seen_content.add(folded)
                        expected_content.append(name)
                expected_archives = []
                seen_archives: set[str] = set()
                for name in [*VANILLA_BSAS, *selected_archives]:
                    folded = name.casefold()
                    if folded not in seen_archives:
                        seen_archives.add(folded)
                        expected_archives.append(name)
                if (
                    persisted_selection["content"] != expected_content
                    or persisted_selection["groundcover"] != active_groundcover
                    or persisted_selection["fallback_archive"]
                    != expected_archives
                    or read_openmw_data_dirs(cfg)
                    != [str(path) for path in data_dirs]
                ):
                    raise RuntimeError(
                        "OpenMW config read-back differs from canonical plugin state"
                    )
                launcher_profile = read_openmw_launcher_profile(launcher_cfg)
                launcher_data = []
                seen_data: set[str] = set()
                for path in data_dirs:
                    value = str(path)
                    if value not in seen_data:
                        seen_data.add(value)
                        launcher_data.append(value)
                if launcher_profile != {
                    "current_profile": "Fluorine",
                    "data": launcher_data,
                    "content": expected_content,
                    "fallback_archive": expected_archives,
                }:
                    raise RuntimeError(
                        "OpenMW launcher config read-back differs from export"
                    )
                self._openmw_game_plugins.stageExportState(
                    organizer.pluginList(), state
                )
            self._openmw_game_plugins.commitExportState(
                organizer.pluginList(), state
            )
            qInfo(
                f"OpenMW: wrote {len(data_dirs)} data dir(s) and "
                f"{len(content)} content plugin(s) to {cfg}."
            )
        except Exception as e:
            if isinstance(e, TransactionRollbackError):
                self._openmw_game_plugins.blockPersistence(e)
            if isinstance(e, TransactionCleanupError):
                if state is not None:
                    try:
                        self._openmw_game_plugins.commitExportState(
                            organizer.pluginList(), state
                        )
                    except Exception as sync_error:
                        qWarning(
                            "OpenMW: committed export state could not be synchronized "
                            f"after cleanup failure: {sync_error}"
                        )
            qWarning(f"OpenMW: openmw.cfg export failed: {e}")
            # The profile selector, openmw.cfg, and launcher.cfg form one
            # configuration. Launching after a partial update can select stale
            # paths or the wrong profile; let the user retry instead.
            return False
        return True
