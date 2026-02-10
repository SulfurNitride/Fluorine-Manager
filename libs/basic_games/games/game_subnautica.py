from __future__ import annotations

import fnmatch
import os
from collections.abc import Iterable
from enum import Enum
from pathlib import Path

from PyQt6.QtCore import QDir, qWarning

import mobase

from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
)
from ..basic_features.utils import is_directory
from ..basic_game import BasicGame


class SubnauticaModDataChecker(BasicModDataChecker):
    use_qmods: bool = False

    def __init__(self, patterns: GlobPatterns | None = None, use_qmods: bool = False):
        super().__init__(
            GlobPatterns(
                unfold=["BepInExPack_Subnautica"],
                valid=[
                    "BepInEx",
                    "doorstop_libs",
                    "doorstop_config.ini",
                    "run_bepinex.sh",
                    "winhttp.dll",
                    "QMods",
                    ".doorstop_version",  # Added in Tobey's BepInEx Pack for Subnautica v5.4.23
                    "changelog.txt",
                    "libdoorstop.dylib",
                ],
                ignore=["*.mohidden"],
                delete=[
                    "*.txt",
                    "*.md",
                    "icon.png",
                    "license",
                    "manifest.json",
                ],
                move={
                    "plugins": "BepInEx/",
                    "patchers": "BepInEx/",
                    "CustomCraft2SML": "QMods/" if use_qmods else "BepInEx/plugins/",
                    "CustomCraft3": "QMods/" if use_qmods else "BepInEx/plugins/",
                },
            ).merge(patterns or GlobPatterns()),
        )
        self.use_qmods = use_qmods

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # fix: single root folders get traversed by Simple Installer
        parent = filetree.parent()
        if parent is not None and self.dataLooksValid(parent) is self.FIXABLE:
            return self.FIXABLE
        check_return = super().dataLooksValid(filetree)
        # A single unknown folder with a dll file in is to be moved to BepInEx/plugins/
        if (
            check_return is self.INVALID
            and len(filetree) == 1
            and is_directory(folder := filetree[0])
            and any(fnmatch.fnmatch(entry.name(), "*.dll") for entry in folder)
        ):
            return self.FIXABLE
        return check_return

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        filetree = super().fix(filetree)
        if (
            self.dataLooksValid(filetree) is self.FIXABLE
            and len(filetree) == 1
            and is_directory(folder := filetree[0])
            and any(fnmatch.fnmatch(entry.name(), "*.dll") for entry in folder)
        ):
            filetree.move(folder, "QMods/" if self.use_qmods else "BepInEx/plugins/")
        return filetree


class SubnauticaGame(BasicGame, mobase.IPluginFileMapper):
    Name = "Subnautica Support Plugin"
    Author = "dekart811, Zash"
    Version = "2.3"

    GameName = "Subnautica"
    GameShortName = "subnautica"
    GameNexusName = "subnautica"
    GameThunderstoreName = "subnautica"
    GameSteamId = 264710
    GameEpicId = "Jaguar"
    GameBinary = "Subnautica.exe"
    GameDataPath = "_ROOT"  # Custom mappings to actual root folders below.
    GameDocumentsDirectory = r"%GAME_PATH%"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Subnautica"
    )
    GameSavesDirectory = r"%GAME_PATH%\SNAppData\SavedGames"

    _game_extra_save_paths = [
        r"%USERPROFILE%\Appdata\LocalLow\Unknown Worlds"
        r"\Subnautica\Subnautica\SavedGames"
    ]

    _forced_libraries = ["winhttp.dll"]

    _root_blacklist = {GameDataPath.casefold()}

    class MapType(Enum):
        FILE = 0
        FOLDER = 1

    _root_extra_overwrites: dict[str, MapType] = {
        "qmodmanager_log-Subnautica.txt": MapType.FILE,
        "qmodmanager-config.json": MapType.FILE,
        "BepInEx_Shim_Backup": MapType.FOLDER,
    }
    """Extra files & folders created in game root by mods / BepInEx after game launch,
    but not included in the mod archives.
    """

    def __init__(self):
        super().__init__()
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._set_mod_data_checker()
        self._register_feature(
            BasicGameSaveGameInfo(lambda s: Path(s or "", "screenshot.jpg"))
        )

        organizer.onPluginSettingChanged(self._settings_change_callback)
        return True

    def _set_mod_data_checker(
        self, extra_patterns: GlobPatterns | None = None, use_qmod: bool | None = None
    ):
        self._register_feature(
            SubnauticaModDataChecker(
                patterns=(GlobPatterns() if extra_patterns is None else extra_patterns),
                use_qmods=(
                    bool(self._organizer.pluginSetting(self.name(), "use_qmods"))
                    if use_qmod is None
                    else use_qmod
                ),
            )
        )

    def _settings_change_callback(
        self,
        plugin_name: str,
        setting: str,
        old: mobase.MoVariant,
        new: mobase.MoVariant,
    ):
        if plugin_name == self.name() and setting == "use_qmods":
            self._set_mod_data_checker(use_qmod=bool(new))

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "use_qmods",
                (
                    "Install */.dll mods in legacy QMods folder,"
                    " instead of BepInEx/plugins (default)."
                ),
                default_value=False,
            )
        ]

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        return [
            BasicGameSaveGame(folder)
            for save_path in (
                folder.absolutePath(),
                *(os.path.expandvars(p) for p in self._game_extra_save_paths),
            )
            for folder in Path(save_path).glob("slot*")
        ]

    def executables(self) -> list[mobase.ExecutableInfo]:
        binary = self.gameDirectory().absoluteFilePath(self.binaryName())
        return [
            mobase.ExecutableInfo(
                self.gameName(),
                binary,
            ).withArgument("-vrmode none"),
            mobase.ExecutableInfo(
                f"{self.gameName()} VR",
                self.gameDirectory().absoluteFilePath(self.binaryName()),
            ),
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        return [
            mobase.ExecutableForcedLoadSetting(self.binaryName(), lib).withEnabled(True)
            for lib in self._forced_libraries
        ]

    def mappings(self) -> list[mobase.Mapping]:
        game = self._organizer.managedGame()
        game_path = Path(game.gameDirectory().absolutePath())
        overwrite_path = Path(self._organizer.overwritePath())

        return [
            *(
                # Extra overwrites
                self._overwrite_mapping(
                    overwrite_path / name,
                    dest,
                    is_dir=(map_type is self.MapType.FOLDER),
                )
                for name, map_type in self._root_extra_overwrites.items()
                if not (dest := game_path / name).exists()
            ),
            *self._root_mappings(game_path, overwrite_path),
        ]

    def _root_mappings(
        self, game_path: Path, overwrite_path: Path
    ) -> Iterable[mobase.Mapping]:
        for mod_path in self._active_mod_paths():
            mod_name = mod_path.name

            for child in mod_path.iterdir():
                # Check blacklist
                if child.name.casefold() in self._root_blacklist:
                    qWarning(f"Skipping {child.name} ({mod_name})")
                    continue
                destination = game_path / child.name
                # Check existing
                if destination.exists():
                    qWarning(
                        f"Overwriting of existing game files/folders is not supported! "
                        f"{destination.as_posix()} ({mod_name})"
                    )
                    continue
                # Mapping: mod -> root
                yield mobase.Mapping(
                    source=str(child),
                    destination=str(destination),
                    is_directory=child.is_dir(),
                    create_target=False,
                )
                if child.is_dir():
                    # Mapping: overwrite <-> root
                    yield self._overwrite_mapping(
                        overwrite_path / child.name, destination, is_dir=True
                    )

    def _active_mod_paths(self) -> Iterable[Path]:
        mods_parent_path = Path(self._organizer.modsPath())
        modlist = self._organizer.modList().allModsByProfilePriority()
        for mod in modlist:
            if self._organizer.modList().state(mod) & mobase.ModState.ACTIVE:
                yield mods_parent_path / mod

    def _overwrite_mapping(
        self, overwrite_source: Path, destination: Path, is_dir: bool
    ) -> mobase.Mapping:
        """Mapping: overwrite <-> root"""
        if is_dir:
            # Root folders in overwrite need to exits.
            overwrite_source.mkdir(parents=True, exist_ok=True)
        return mobase.Mapping(
            str(overwrite_source),
            str(destination),
            is_dir,
            create_target=True,
        )
