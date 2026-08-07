import json
import os
import shutil
from pathlib import Path
from xml.sax.saxutils import quoteattr

from PyQt6.QtCore import QDir, QFileInfo, qWarning

import mobase

from ..basic_game import BasicGame

# Top-level mod subdirs routed by Region A (step 8) as data-dir overlays.
# Region B must skip them so their content is not also materialized into
# %LOCALAPPDATA%.
_REGION_A_SUBDIRS = {"bin", "data"}

# Destination for `.pak` files, relative to %LOCALAPPDATA%.
_MODS_DIR = Path("Larian Studios") / "Baldur's Gate 3" / "Mods"

# Load-order XML file, its backup, and the per-profile generation state, all
# relative to %LOCALAPPDATA% / the profile directory (step 10).
_PLAYER_PROFILE_DIR = (
    Path("Larian Studios") / "Baldur's Gate 3" / "PlayerProfiles" / "Public"
)
_MODSETTINGS_NAME = "modsettings.lsx"
_MODSETTINGS_PREVIOUS = "modsettings.lsx.previous"
_MODSETTINGS_STATE = "modsettings.lsx.state"

# The base game module is always required in modsettings.lsx, in the same
# position it occupies in the file the game itself produces.
_BASE_MODULE_UUID = "cb555efe-2d9e-131f-8195-a89329d218ea"

# Executables that count as a BG3 launch (about-to-run materializes Region B
# only when one of these is being started).
_BG3_BINARIES = {"bg3.exe", "bg3_dx11.exe", "LariLauncher.exe"}

# Launcher preferences file, relative to %LOCALAPPDATA% (Region B).
_LAUNCHER_PREFERENCES = (
    Path("Larian Studios") / "Launcher" / "Settings" / "preferences.json"
)

# Rendering-backend keys the runners force in preferences.json.  Vulkan is the
# reference value; the exact DX11 value is deferred (only on request).
_VULKAN_BACKEND = {"DefaultRenderingBackend": 0, "OverrideRenderingBackend": True}


def _wine_user_profile() -> str | None:
    """Resolve the Windows user profile inside the Wine prefix.

    Mirrors the core's own prefix resolution (config.json first, then the
    built-in default location) so the capture root returned by
    :meth:`BG3ProfileDirectories.directories` can never disagree with the
    ``%LOCALAPPDATA%`` the core uses in ``organizercore.cpp``.
    """
    candidates: list[str] = []

    # 1. The Fluorine config is authoritative, exactly as in the core's
    #    resolveWinePrefixPath().  prefix_path may point at the pfx dir
    #    directly (containing drive_c) or at the compatdata root (containing
    #    a pfx/ child) — normalise both.
    try:
        config_root = os.environ.get(
            "XDG_CONFIG_HOME", os.path.join(os.path.expanduser("~"), ".config")
        )
        cfg_path = os.path.join(config_root, "fluorine", "config.json")
        if os.path.isfile(cfg_path):
            with open(cfg_path, "r") as f:
                cfg = json.load(f)
            pfx = str(cfg.get("prefix_path", "")).strip()
            if pfx:
                if os.path.isdir(os.path.join(pfx, "drive_c")):
                    candidates.append(pfx)
                elif os.path.isdir(os.path.join(pfx, "pfx", "drive_c")):
                    candidates.append(os.path.join(pfx, "pfx"))
    except (OSError, ValueError):
        pass

    # 2. Built-in default location.
    candidates.append(
        os.path.expanduser("~/.local/share/fluorine/Prefix/pfx")
    )

    for pfx in candidates:
        user_dir = os.path.join(pfx, "drive_c", "users", "steamuser")
        if os.path.isdir(user_dir):
            return user_dir
    return None


class BG3ProfileDirectories(mobase.ProfileDirectories):
    """Registers the game's `%LOCALAPPDATA%` directory for per-profile capture."""

    def directories(self) -> list[QDir]:
        profile = _wine_user_profile()
        if profile is None:
            return []
        return [QDir(os.path.join(profile, "AppData", "Local"))]


class BG3Game(BasicGame, mobase.IPluginFileMapper):
    Name = "Baldur's Gate 3 Plugin"
    Author = "daescha"
    Version = "0.1.0"
    GameName = "Baldur's Gate 3"
    GameShortName = "baldursgate3"
    GameNexusName = "baldursgate3"
    GameValidShortNames = ["bg3"]
    GameLauncher = "Launcher/LariLauncher.exe"
    GameBinary = "bin/bg3.exe"
    GameDataPath = ""
    GameDocumentsDirectory = (
        "%USERPROFILE%/AppData/Local/Larian Studios/Baldur's Gate 3"
    )
    GameSavesDirectory = "%GAME_DOCUMENTS%/PlayerProfiles/Public/Savegames/Story"
    GameSaveExtension = "lsv"
    GameNexusId = 3474
    GameSteamId = 1086940

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        organizer.onAboutToRun(self._on_about_to_run)
        return self._register_feature(BG3ProfileDirectories())

    def mappings(self) -> list[mobase.Mapping]:
        # Region A routing: a mod subdir named `bin` or `Data` (matched
        # case-insensitively) is deployed as a data-dir overlay mapping into
        # the matching `$install_dir\bin` / `$install_dir\Data` folder.  Both
        # connectors consume the same mapping list, so native base files stay
        # preserved underneath the overlay.
        install_dir = Path(self.gameDirectory().absolutePath())
        mods_root = Path(self._organizer.modsPath())
        result: list[mobase.Mapping] = []
        for mod in self._organizer.modList().allModsByProfilePriority():
            if not (self._organizer.modList().state(mod) & mobase.ModState.ACTIVE):
                continue
            mod_dir = mods_root / mod
            try:
                children = list(mod_dir.iterdir())
            except OSError:
                continue
            for sub in children:
                if not sub.is_dir():
                    continue
                match sub.name.lower():
                    case "bin":
                        dest = install_dir / "bin"
                    case "data":
                        dest = install_dir / "Data"
                    case _:
                        continue
                result.append(
                    mobase.Mapping(str(sub), str(dest), True, False)
                )
        return result

    def _on_about_to_run(self, app_path: str, *args: object) -> bool:
        # Region B routing runs only when a BG3 executable is being launched,
        # after the core has captured %LOCALAPPDATA% into <profile>/Local, so
        # the materialized links land inside the captured profile tree.
        binary = Path(app_path.replace("\\", "/")).name.lower()
        if binary not in _BG3_BINARIES:
            return True
        try:
            self._migrate_legacy_saves()
            self._materialize_region_b()
            self._sync_modsettings()
            self._sync_preferences(binary)
        except OSError as err:
            qWarning(f"BG3 Region B routing failed: {err}")
        return True

    def _migrate_legacy_saves(self) -> None:
        """Move the pre-step-8 `<profile>/saves` content into the captured tree.

        Instances created with the previous plugin registered
        ``BasicLocalSavegames``, which relinked the prefix save directory into
        ``<profile>/saves``.  The current plugin captures saves into
        ``<profile>/Local`` instead, so any leftover legacy content is folded
        into the captured save directory here.

        The move itself is the idempotency marker: after the first successful
        run the source directory no longer exists, so nothing is replayed.  It
        only runs when the captured target is missing or empty so newer saves
        are never overwritten.
        """
        local = self._local_appdata()
        if local is None:
            return
        profile = self._organizer.profilePath()
        if not profile:
            return
        source = Path(profile) / "saves"
        if not source.is_dir():
            return
        try:
            if not any(source.iterdir()):
                return
        except OSError:
            return
        target = local / _PLAYER_PROFILE_DIR / "Savegames" / "Story"
        if target.exists():
            try:
                if any(target.iterdir()):
                    return
            except OSError:
                return
        try:
            target.mkdir(parents=True, exist_ok=True)
            for child in source.iterdir():
                shutil.move(os.fspath(child), os.fspath(target / child.name))
            # Only drop the now-empty legacy dir; never delete one that has
            # since been repopulated.
            if source.is_dir() and not any(source.iterdir()):
                source.rmdir()
        except OSError as err:
            qWarning(f"BG3 legacy save migration failed: {err}")

    def executables(self) -> list[mobase.ExecutableInfo]:
        # The game ships separate rendering-backend binaries; expose them as
        # dedicated launch entries so each runner can force its preferences.
        game_dir = self.gameDirectory()
        return [
            mobase.ExecutableInfo(
                "BG3: Vulkan",
                QFileInfo(game_dir.absoluteFilePath("bin/bg3.exe")),
            ),
            mobase.ExecutableInfo(
                "BG3: DX11",
                QFileInfo(game_dir.absoluteFilePath("bin/bg3_dx11.exe")),
            ),
        ]

    def _sync_preferences(self, binary: str) -> None:
        """Force the rendering backend in the launcher's preferences.json.

        Only the Vulkan runner has a known value; the DX11 value is deferred
        (only on request), so nothing is written for it yet.  The file lives
        inside Region B, so the write is automatically per profile.
        """
        if binary == "bg3.exe":
            backend = _VULKAN_BACKEND
        else:
            backend = None
        if backend is None:
            return
        local = self._local_appdata()
        if local is None:
            qWarning("BG3 preferences: could not resolve %LOCALAPPDATA%, skipping")
            return
        target = local / _LAUNCHER_PREFERENCES
        try:
            prefs = json.loads(target.read_text(encoding="utf-8"))
        except OSError:
            prefs = {}
        except ValueError:
            prefs = {}
        if not isinstance(prefs, dict):
            prefs = {}
        prefs.update(backend)
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                json.dumps(prefs, indent=4), encoding="utf-8"
            )
        except OSError as err:
            qWarning(f"BG3 preferences: cannot write '{target}': {err}")

    @staticmethod
    def _local_appdata() -> Path | None:
        profile = _wine_user_profile()
        if profile is None:
            return None
        return Path(profile) / "AppData" / "Local"

    def _materialize_region_b(self) -> None:
        local = self._local_appdata()
        if local is None:
            qWarning("BG3 Region B: could not resolve %LOCALAPPDATA%, skipping")
            return
        mods_root = Path(self._organizer.modsPath())
        for mod in self._organizer.modList().allModsByProfilePriority():
            if not (self._organizer.modList().state(mod) & mobase.ModState.ACTIVE):
                continue
            mod_dir = mods_root / mod
            if not mod_dir.is_dir():
                continue
            self._materialize_mod_content(mod_dir, local)

    def _materialize_mod_content(self, mod_dir: Path, local: Path) -> None:
        for source in mod_dir.rglob("*"):
            if not source.is_file():
                continue
            rel = source.relative_to(mod_dir)
            # Top-level `bin`/`Data` subdirs are Region A (served by the VFS
            # overlay); skip them here so they are not also materialized.
            if rel.parts and rel.parts[0].lower() in _REGION_A_SUBDIRS:
                continue
            if source.suffix.lower() == ".pak":
                # `.pak` wins: regardless of where it is inside the mod, route
                # it (by its own basename) into the game's Mods folder.
                dest = local / _MODS_DIR / source.name
            else:
                dest = local / rel
            self._materialize_link(source, dest)

    @staticmethod
    def _materialize_link(source: Path, dest: Path) -> None:
        try:
            dest.parent.mkdir(parents=True, exist_ok=True)
        except OSError as err:
            qWarning(f"BG3 Region B: cannot create '{dest.parent}': {err}")
            return
        if dest.is_symlink() or dest.exists():
            try:
                dest.unlink()
            except OSError as err:
                qWarning(f"BG3 Region B: cannot replace '{dest}': {err}")
                return
        try:
            dest.symlink_to(os.fspath(source))
        except OSError as err:
            qWarning(f"BG3 Region B: cannot link '{dest}' -> '{source}': {err}")

    # --- modsettings.lsx lifecycle (step 10) ---

    _MODSETTINGS_XML_START = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        "<save>\n"
        '    <version major="4" minor="8" revision="0" build="500"/>\n'
        '    <region id="ModuleSettings">\n'
        '        <node id="root">\n'
        "            <children>\n"
        '                <node id="Mods">\n'
        "                    <children>\n"
    )
    _MODSETTINGS_XML_END = (
        "                    </children>\n"
        "                </node>\n"
        "            </children>\n"
        "        </node>\n"
        "    </region>\n"
        "</save>\n"
    )

    _BASE_MODULE_META = {
        "Folder": "Gustav",
        "MD5": "",
        "Name": "Gustav",
        "PublishHandle": "0",
        "UUID": _BASE_MODULE_UUID,
        "Version64": "36028797018963968",
    }

    def _modsettings_state_path(self) -> Path:
        profile = self._organizer.profilePath()
        if profile:
            return Path(profile) / _MODSETTINGS_STATE
        return Path(self._organizer.pluginDataPath()) / _MODSETTINGS_STATE

    def _routed_paks_in_mod(self, mod_dir: Path) -> list[Path]:
        """The `.pak` files of a mod that Region B materializes into Mods.

        Mirrors `_materialize_mod_content`: paks anywhere in the mod count,
        except those inside a top-level `bin`/`Data` subdir (Region A overlay).
        """
        result: list[Path] = []
        for source in mod_dir.rglob("*"):
            if not source.is_file() or source.suffix.lower() != ".pak":
                continue
            rel = source.relative_to(mod_dir)
            if rel.parts and rel.parts[0].lower() in _REGION_A_SUBDIRS:
                continue
            result.append(source)
        return sorted(result, key=lambda p: os.fspath(p).lower())

    def _ordered_pak_ids(self) -> list[str]:
        """Ordered identity of enabled `.pak`s, in profile-priority order.

        Override paks are not modules and never form part of the load order,
        so they are filtered out here — this keeps the state snapshot in sync
        with the generated XML (see `_build_modsettings`).
        """
        try:
            import larian_formats
        except ImportError:
            larian_formats = None
        mods_root = Path(self._organizer.modsPath())
        ids: list[str] = []
        for mod in self._organizer.modList().allModsByProfilePriority():
            if not (self._organizer.modList().state(mod) & mobase.ModState.ACTIVE):
                continue
            mod_dir = mods_root / mod
            if not mod_dir.is_dir():
                continue
            for pak in self._routed_paks_in_mod(mod_dir):
                if larian_formats is not None:
                    try:
                        if larian_formats.is_override(pak):
                            continue
                    except Exception:
                        pass
                ids.append(f"{mod}/{pak.relative_to(mod_dir).as_posix()}")
        return ids

    def _sync_modsettings(self) -> None:
        local = self._local_appdata()
        if local is None:
            qWarning("BG3 modsettings: could not resolve %LOCALAPPDATA%, skipping")
            return
        state_path = self._modsettings_state_path()
        current_ids = self._ordered_pak_ids()
        if current_ids == self._load_state(state_path):
            return
        target = local / _PLAYER_PROFILE_DIR / _MODSETTINGS_NAME
        previous = target.with_name(_MODSETTINGS_PREVIOUS)
        try:
            if target.exists():
                shutil.copy2(target, previous)
        except OSError as err:
            qWarning(f"BG3 modsettings: cannot back up '{target}': {err}")
            return
        try:
            import larian_formats
        except ImportError:
            qWarning(
                "BG3 modsettings: larian-formats is unavailable, "
                "leaving modsettings.lsx unchanged"
            )
            return
        try:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(
                self._build_modsettings(current_ids, larian_formats),
                encoding="utf-8",
            )
        except OSError as err:
            qWarning(f"BG3 modsettings: cannot write '{target}': {err}")
            return
        self._save_state(state_path, current_ids)

    def _build_modsettings(
        self, current_ids: list[str], larian_formats
    ) -> str:
        mods_root = Path(self._organizer.modsPath())
        nodes = [self._module_short_desc(self._BASE_MODULE_META)]
        for mod_id in current_ids:
            mod, _, rel = mod_id.partition("/")
            pak = mods_root / mod / rel
            try:
                meta = dict(larian_formats.get_metadata_for_file(pak))
                if larian_formats.is_override(pak):
                    continue
            except Exception as err:
                qWarning(f"BG3 modsettings: could not read metadata for '{pak}': {err}")
                meta = {}
            nodes.append(self._module_short_desc(meta))
        return (
            self._MODSETTINGS_XML_START
            + "\n".join(nodes)
            + "\n"
            + self._MODSETTINGS_XML_END
        )

    @staticmethod
    def _module_short_desc(meta: dict) -> str:
        def attr(aid: str, atype: str, value) -> str:
            return (
                f'                            <attribute id="{aid}" '
                f'type="{atype}" value={quoteattr(str(value))}/>'
            )

        return (
            '                        <node id="ModuleShortDesc">\n'
            + attr("Folder", "LSString", meta.get("Folder", ""))
            + "\n"
            + attr("MD5", "LSString", meta.get("MD5", ""))
            + "\n"
            + attr("Name", "LSString", meta.get("Name", ""))
            + "\n"
            + attr("PublishHandle", "uint64", meta.get("PublishHandle", "0"))
            + "\n"
            + attr("UUID", "guid", meta.get("UUID", ""))
            + "\n"
            + attr("Version64", "int64", meta.get("Version64", "0"))
            + "\n"
            + "                        </node>"
        )

    @staticmethod
    def _load_state(path: Path) -> list:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            return data if isinstance(data, list) else []
        except (OSError, ValueError):
            return []

    @staticmethod
    def _save_state(path: Path, ids: list) -> None:
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(ids), encoding="utf-8")
        except OSError as err:
            qWarning(f"BG3 modsettings: cannot save state '{path}': {err}")

    def loadOrderMechanism(self) -> mobase.LoadOrderMechanism:
        return mobase.LoadOrderMechanism.PLUGINS_TXT