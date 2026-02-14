from __future__ import annotations

import os
import platform
import shutil
import sys
from pathlib import Path
from typing import Callable, Generic, TypeVar

from PyQt6.QtCore import QDir, QFileInfo, QStandardPaths, qWarning
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QMessageBox

import mobase

from .basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
)


def _find_wine_userprofile() -> str | None:
    """On Linux, find the Wine/Proton user profile directory inside the prefix.

    Returns the host path equivalent of %USERPROFILE% (e.g.
    ``<prefix>/drive_c/users/steamuser``) or *None* if no valid prefix is
    found.
    """
    if platform.system() == "Windows":
        return None

    candidates: list[str] = []

    # 1. Fluorine data prefix (shared by native and Flatpak builds)
    fluorine_pfx = os.path.expanduser(
        "~/.local/share/fluorine/Prefix/pfx"
    )
    candidates.append(fluorine_pfx)

    # 2. Fluorine config prefix_path
    try:
        config_dir = os.environ.get(
            "XDG_CONFIG_HOME", os.path.join(str(Path.home()), ".config")
        )
        cfg_path = os.path.join(config_dir, "fluorine", "config.json")
        if os.path.isfile(cfg_path):
            import json

            with open(cfg_path, "r") as f:
                cfg = json.load(f)
            pfx = cfg.get("prefix_path", "")
            if pfx:
                candidates.append(pfx)
    except Exception:
        pass

    for pfx in candidates:
        user_dir = os.path.join(pfx, "drive_c", "users", "steamuser")
        if os.path.isdir(user_dir):
            return user_dir

    return None


def replace_variables(value: str, game: BasicGame) -> str:
    """Replace special paths in the given value."""

    if value.find("%DOCUMENTS%") != -1:
        value = value.replace(
            "%DOCUMENTS%",
            QStandardPaths.writableLocation(
                QStandardPaths.StandardLocation.DocumentsLocation
            ),
        )
    if value.find("%USERPROFILE%") != -1:
        if platform.system() != "Windows":
            wine_profile = _find_wine_userprofile()
            if wine_profile:
                value = value.replace("%USERPROFILE%", wine_profile)
            else:
                qWarning(
                    "No Wine/Proton prefix found. "
                    "Ensure a prefix is configured in Settings > Proton."
                )
                value = value.replace("%USERPROFILE%", "")
        else:
            value = value.replace(
                "%USERPROFILE%",
                QStandardPaths.writableLocation(
                    QStandardPaths.StandardLocation.HomeLocation
                ),
            )
    if value.find("%GAME_DOCUMENTS%") != -1:
        value = value.replace(
            "%GAME_DOCUMENTS%", game.documentsDirectory().absolutePath()
        )
    if value.find("%GAME_PATH%") != -1:
        value = value.replace("%GAME_PATH%", game.gameDirectory().absolutePath())

    # Normalize Windows backslash path separators for Linux.
    value = value.replace("\\", "/")

    return value


_T = TypeVar("_T")


class BasicGameMapping(Generic[_T]):
    # The game:
    _game: "BasicGame"

    # Name of the attribute for exposure:
    _exposed_name: str

    # Name of the internal method:
    _internal_method_name: str

    # Required:
    _required: bool

    # Callable returning a default value (if not required):
    _default: Callable[["BasicGame"], _T]

    # Function to apply to the value:
    _apply_fn: Callable[[_T | str], _T] | None

    def __init__(
        self,
        game: BasicGame,
        exposed_name: str,
        internal_method: str,
        default: Callable[[BasicGame], _T] | None = None,
        apply_fn: Callable[[_T | str], _T] | None = None,
    ):
        self._game = game
        self._exposed_name = exposed_name
        self._internal_method_name = internal_method
        self._apply_fn = apply_fn

        if hasattr(game, self._exposed_name):
            value = getattr(game, self._exposed_name)

            if self._apply_fn is not None:
                try:
                    value = self._apply_fn(value)
                except Exception as err:
                    raise ValueError(
                        "Basic game plugin from {} has an invalid {} property.".format(
                            game._fromName,  # pyright: ignore[reportPrivateUsage]
                            self._exposed_name,
                        )
                    ) from err
            self._default = lambda game: value  # type: ignore
        elif default is not None:
            self._default = default  # type: ignore
        elif getattr(game.__class__, self._internal_method_name) is getattr(
            BasicGame, self._internal_method_name
        ):
            raise ValueError(
                "Basic game plugin from {} is missing {} property.".format(
                    game._fromName,  # pyright: ignore[reportPrivateUsage]
                    self._exposed_name,
                )
            )

    def get(self) -> _T:
        """Return the value of this mapping."""
        value = self._default(self._game)  # type: ignore

        if isinstance(value, str):
            return replace_variables(value, self._game)  # type: ignore
        elif isinstance(value, QDir):
            return QDir(replace_variables(value.path(), self._game))  # type: ignore

        # MO2 does not support Path anywhere so we always convert to str:
        elif isinstance(value, Path):
            return replace_variables(str(value), self._game)  # type: ignore

        return value


class BasicGameOptionsMapping(BasicGameMapping[list[_T]]):
    """
    Represents a game mappings for which multiple options are possible. The game
    plugin is responsible to choose the right option depending on the context.
    """

    _index: int

    def __init__(
        self,
        game: BasicGame,
        exposed_name: str,
        internal_method: str,
        default: Callable[[BasicGame], _T] | None = None,
        apply_fn: Callable[[list[_T] | str], list[_T]] | None = None,
    ):
        super().__init__(game, exposed_name, internal_method, lambda g: [], apply_fn)
        self._index = -1
        self._current_default = default

    def set_index(self, index: int):
        """
        Set the index of the option to use.

        Args:
            index: Index of the option to use.
        """
        self._index = index

    def set_value(self, value: _T):
        """
        Set the index corresponding of the given value. If the value is not present,
        the index is set to -1.

        Args:
            value: The value to set the index to.
        """
        try:
            self._index = self.get().index(value)
        except ValueError:
            self._index = -1

    def has_value(self) -> bool:
        """
        Check if a value was set for this options mapping.

        Returns:
            True if a value was set, False otherwise.
        """
        return self._index != -1

    def current(self) -> _T:
        values = self._default(self._game)  # type: ignore

        if not values:
            return self._current_default(self._game)  # type: ignore

        if self._index == -1:
            value = values[0]
        else:
            value = values[self._index]

        if isinstance(value, str):
            return replace_variables(value, self._game)  # type: ignore
        elif isinstance(value, QDir):
            return QDir(replace_variables(value.path(), self._game))  # type: ignore

        return value


class BasicGameMappings:
    name: BasicGameMapping[str]
    author: BasicGameMapping[str]
    version: BasicGameMapping[mobase.VersionInfo]
    description: BasicGameMapping[str]
    gameName: BasicGameMapping[str]
    gameShortName: BasicGameMapping[str]
    gameNexusName: BasicGameMapping[str]
    gameThunderstoreName: BasicGameMapping[str]
    validShortNames: BasicGameMapping[list[str]]
    nexusGameId: BasicGameMapping[int]
    binaryName: BasicGameMapping[str]
    launcherName: BasicGameMapping[str]
    dataDirectory: BasicGameMapping[str]
    documentsDirectory: BasicGameMapping[QDir]
    iniFiles: BasicGameMapping[list[str]]
    savesDirectory: BasicGameMapping[QDir]
    savegameExtension: BasicGameMapping[str]
    steamAPPId: BasicGameOptionsMapping[str]
    gogAPPId: BasicGameOptionsMapping[str]
    originManifestIds: BasicGameOptionsMapping[str]
    originWatcherExecutables: BasicGameMapping[list[str]]
    epicAPPId: BasicGameOptionsMapping[str]
    eaDesktopContentId: BasicGameOptionsMapping[str]
    supportURL: BasicGameMapping[str]

    @staticmethod
    def _default_documents_directory(game: mobase.IPluginGame):
        folders = [
            "{}/My Games/{}".format(
                QStandardPaths.writableLocation(
                    QStandardPaths.StandardLocation.DocumentsLocation
                ),
                game.gameName(),
            ),
            "{}/{}".format(
                QStandardPaths.writableLocation(
                    QStandardPaths.StandardLocation.DocumentsLocation
                ),
                game.gameName(),
            ),
        ]
        for folder in folders:
            qdir = QDir(folder)
            if qdir.exists():
                return qdir

        return QDir()

    # Game mappings:
    def __init__(self, game: BasicGame):
        self._game = game

        self.name = BasicGameMapping(game, "Name", "name")
        self.author = BasicGameMapping(game, "Author", "author")
        self.version = BasicGameMapping(
            game,
            "Version",
            "version",
            apply_fn=lambda s: mobase.VersionInfo(s) if isinstance(s, str) else s,
        )
        self.description = BasicGameMapping(
            game,
            "Description",
            "description",
            lambda g: "Adds basic support for game {}.".format(g.gameName()),
        )
        self.gameName = BasicGameMapping(game, "GameName", "gameName")
        self.gameShortName = BasicGameMapping(game, "GameShortName", "gameShortName")
        self.gameNexusName = BasicGameMapping(
            game,
            "GameNexusName",
            "gameNexusName",
            default=lambda g: g.gameShortName(),
        )
        self.gameThunderstoreName = BasicGameMapping(
            game,
            "GameThunderstoreName",
            "gameThunderstoreName",
            default=lambda g: "",
        )
        self.validShortNames = BasicGameMapping(
            game,
            "GameValidShortNames",
            "validShortNames",
            default=lambda g: [],
            apply_fn=lambda value: (
                [c.strip() for c in value.split(",")]  # type: ignore
                if isinstance(value, str)
                else value
            ),
        )
        self.nexusGameId = BasicGameMapping(
            game, "GameNexusId", "nexusGameID", default=lambda g: 0, apply_fn=int
        )
        self.binaryName = BasicGameMapping(game, "GameBinary", "binaryName")
        self.launcherName = BasicGameMapping(
            game,
            "GameLauncher",
            "getLauncherName",
            default=lambda g: "",
        )
        self.dataDirectory = BasicGameMapping(game, "GameDataPath", "dataDirectory")
        self.documentsDirectory = BasicGameMapping(
            game,
            "GameDocumentsDirectory",
            "documentsDirectory",
            apply_fn=lambda s: QDir(s) if isinstance(s, str) else s,
            default=BasicGameMappings._default_documents_directory,
        )
        self.iniFiles = BasicGameMapping(
            game,
            "GameIniFiles",
            "iniFiles",
            lambda g: [],
            apply_fn=lambda value: (
                [c.strip() for c in value.split(",")]
                if isinstance(value, str)
                else value
            ),
        )
        self.savesDirectory = BasicGameMapping(
            game,
            "GameSavesDirectory",
            "savesDirectory",
            apply_fn=lambda s: QDir(s) if isinstance(s, str) else s,
            default=lambda g: g.documentsDirectory(),
        )
        self.savegameExtension = BasicGameMapping(
            game, "GameSaveExtension", "savegameExtension", default=lambda g: "save"
        )

        # Convert Union[int, str, List[Union[int, str]]] to List[str].
        def ids_apply(v: list[int] | list[str] | int | str) -> list[str]:
            """
            Convert various types to a list of string. If the given value is already a
            list, returns a new list with all values converted to string, otherwise
            returns a list with the value convert to a string as its only element.
            """
            if isinstance(v, (int, str)):
                v = [str(v)]
            return [str(x) for x in v]

        self.steamAPPId = BasicGameOptionsMapping(
            game, "GameSteamId", "steamAPPId", default=lambda g: "", apply_fn=ids_apply
        )
        self.gogAPPId = BasicGameOptionsMapping(
            game, "GameGogId", "gogAPPId", default=lambda g: "", apply_fn=ids_apply
        )
        self.originManifestIds = BasicGameOptionsMapping(
            game,
            "GameOriginManifestIds",
            "originManifestIds",
            default=lambda g: "",
            apply_fn=ids_apply,
        )
        self.originWatcherExecutables = BasicGameMapping(
            game,
            "GameOriginWatcherExecutables",
            "originWatcherExecutables",
            apply_fn=lambda s: [s] if isinstance(s, str) else s,
            default=lambda g: [],
        )
        self.epicAPPId = BasicGameOptionsMapping(
            game, "GameEpicId", "epicAPPId", default=lambda g: "", apply_fn=ids_apply
        )
        self.eaDesktopContentId = BasicGameOptionsMapping(
            game,
            "GameEaDesktopId",
            "eaDesktopContentId",
            default=lambda g: "",
            apply_fn=ids_apply,
        )
        self.supportURL = BasicGameMapping(
            game, "GameSupportURL", "supportURL", default=lambda g: ""
        )


class BasicGame(mobase.IPluginGame):
    """This class implements some methods from mobase.IPluginGame
    to make it easier to create game plugins without having to implement
    all the methods of mobase.IPluginGame."""

    # List of steam, GOG, origin and Epic games:
    steam_games: dict[str, Path]
    gog_games: dict[str, Path]
    origin_games: dict[str, Path]
    epic_games: dict[str, Path]
    eadesktop_games: dict[str, Path]

    @staticmethod
    def setup():
        from .eadesktop_utils import find_games as find_eadesktop_games
        from .epic_utils import find_games as find_epic_games
        from .gog_utils import find_games as find_gog_games
        from .origin_utils import find_games as find_origin_games
        from .steam_utils import find_games as find_steam_games

        errors: list[tuple[str, Exception]] = []
        BasicGame.steam_games = find_steam_games()
        BasicGame.gog_games = find_gog_games()
        BasicGame.origin_games = find_origin_games()
        BasicGame.epic_games = find_epic_games(errors)
        BasicGame.eadesktop_games = find_eadesktop_games(errors)

        if errors:
            QMessageBox.critical(
                None,
                "Errors loading game list",
                (
                    "The following errors occurred while loading the list of available games:\n"
                    f"\n- {'\n\n- '.join('\n '.join(str(e) for e in messageError) for messageError in errors)}"
                ),
            )

    # File containing the plugin:
    _fromName: str

    # Organizer obtained in init:
    _organizer: mobase.IOrganizer

    # Path to the game, as set by MO2:
    _gamePath: str

    def __init__(self):
        super(BasicGame, self).__init__()

        if not hasattr(self, "_fromName"):
            self._fromName = self.__class__.__name__

        self._gamePath = ""

        self._mappings: BasicGameMappings = BasicGameMappings(self)

    def _register_feature(self, feature: mobase.GameFeature) -> bool:
        return self._organizer.gameFeatures().registerFeature(self, feature, 0, True)

    # Specific to BasicGame:
    def is_steam(self) -> bool:
        return self._mappings.steamAPPId.has_value()

    def is_gog(self) -> bool:
        return self._mappings.gogAPPId.has_value()

    def is_origin(self) -> bool:
        return self._mappings.originManifestIds.has_value()

    def is_epic(self) -> bool:
        return self._mappings.epicAPPId.has_value()

    def is_eadesktop(self) -> bool:
        return self._mappings.eaDesktopContentId.has_value()

    # IPlugin interface:

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer

        self._register_feature(BasicGameSaveGameInfo())

        if self._mappings.originWatcherExecutables.get():
            from .origin_utils import OriginWatcher

            self.origin_watcher = OriginWatcher(
                self._mappings.originWatcherExecutables.get()
            )
            if not self._organizer.onAboutToRun(
                lambda appName: self.origin_watcher.spawn_origin_watcher()
            ):
                print("Failed to register onAboutToRun callback!", file=sys.stderr)
                return False
            if not self._organizer.onFinishedRun(
                lambda appName, result: self.origin_watcher.stop_origin_watcher()
            ):
                print("Failed to register onFinishedRun callback!", file=sys.stderr)
                return False
        return True

    def name(self) -> str:
        return self._mappings.name.get()

    def author(self) -> str:
        return self._mappings.author.get()

    def description(self) -> str:
        return self._mappings.description.get()

    def version(self) -> mobase.VersionInfo:
        return self._mappings.version.get()

    def isActive(self) -> bool:
        if not self._organizer.managedGame():
            return False

        # Note: self is self._organizer.managedGame() does not work:
        return self.name() == self._organizer.managedGame().name()

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    # IPluginGame interface:

    def detectGame(self):
        for steam_id in self._mappings.steamAPPId.get():
            if steam_id in BasicGame.steam_games:
                self.setGamePath(BasicGame.steam_games[steam_id])
                return

        for gog_id in self._mappings.gogAPPId.get():
            if gog_id in BasicGame.gog_games:
                self.setGamePath(BasicGame.gog_games[gog_id])
                return

        for origin_manifest_id in self._mappings.originManifestIds.get():
            if origin_manifest_id in BasicGame.origin_games:
                self.setGamePath(BasicGame.origin_games[origin_manifest_id])
                return

        for epic_id in self._mappings.epicAPPId.get():
            if epic_id in BasicGame.epic_games:
                self.setGamePath(BasicGame.epic_games[epic_id])
                return

        for eadesktop_content_id in self._mappings.eaDesktopContentId.get():
            if eadesktop_content_id in BasicGame.eadesktop_games:
                self.setGamePath(BasicGame.eadesktop_games[eadesktop_content_id])
                return

    def gameName(self) -> str:
        return self._mappings.gameName.get()

    def gameShortName(self) -> str:
        return self._mappings.gameShortName.get()

    def gameIcon(self) -> QIcon:
        return mobase.getIconForExecutable(
            self.gameDirectory().absoluteFilePath(self.binaryName())
        )

    def validShortNames(self) -> list[str]:
        return self._mappings.validShortNames.get()

    def gameNexusName(self) -> str:
        return self._mappings.gameNexusName.get()

    def gameThunderstoreName(self) -> str:
        return self._mappings.gameThunderstoreName.get()

    def nexusModOrganizerID(self) -> int:
        return 0

    def nexusGameID(self) -> int:
        return self._mappings.nexusGameId.get()

    def steamAPPId(self) -> str:
        return self._mappings.steamAPPId.current()

    def gogAPPId(self) -> str:
        return self._mappings.gogAPPId.current()

    def epicAPPId(self) -> str:
        return self._mappings.epicAPPId.current()

    def eaDesktopContentId(self) -> str:
        return self._mappings.eaDesktopContentId.current()

    def binaryName(self) -> str:
        return self._mappings.binaryName.get()

    def getLauncherName(self) -> str:
        return self._mappings.launcherName.get()

    def getSupportURL(self) -> str:
        return self._mappings.supportURL.get()

    def iniFiles(self) -> list[str]:
        return self._mappings.iniFiles.get()

    def executables(self) -> list[mobase.ExecutableInfo]:
        execs: list[mobase.ExecutableInfo] = []
        if self.getLauncherName():
            execs.append(
                mobase.ExecutableInfo(
                    self.gameName(),
                    QFileInfo(
                        self.gameDirectory().absoluteFilePath(self.getLauncherName())
                    ),
                )
            )
        execs.append(
            mobase.ExecutableInfo(
                self.gameName(),
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            )
        )
        return execs

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        return []

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            BasicGameSaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"**/*.{ext}")
        ]

    def initializeProfile(
        self, directory: QDir, settings: mobase.ProfileSetting
    ) -> None:
        if settings & mobase.ProfileSetting.CONFIGURATION:
            for iniFile in self.iniFiles():
                try:
                    shutil.copyfile(
                        self.documentsDirectory().absoluteFilePath(iniFile),
                        directory.absoluteFilePath(QFileInfo(iniFile).fileName()),
                    )
                except FileNotFoundError:
                    Path(
                        directory.absoluteFilePath(QFileInfo(iniFile).fileName())
                    ).touch()

    def setGameVariant(self, variant: str) -> None:
        pass

    def gameVersion(self) -> str:
        return mobase.getFileVersion(
            self.gameDirectory().absoluteFilePath(self.binaryName())
        )

    def looksValid(self, directory: QDir):
        return directory.exists(self.binaryName())

    def isInstalled(self) -> bool:
        return bool(self._gamePath)

    def gameDirectory(self) -> QDir:
        """
        @return directory (QDir) to the game installation.
        """
        return QDir(self._gamePath)

    def dataDirectory(self) -> QDir:
        data_path = self._mappings.dataDirectory.get()
        if data_path and QDir.isAbsolutePath(data_path):
            return QDir(data_path)
        return QDir(
            self.gameDirectory().absoluteFilePath(data_path)
        )

    def setGamePath(self, path: Path | str) -> None:
        self._gamePath = str(path)

        path = Path(path)

        # Check if we have a matching steam, GOG, Origin or EA Desktop id and set the
        # index accordingly:
        for steamid, steampath in BasicGame.steam_games.items():
            if steampath == path:
                self._mappings.steamAPPId.set_value(steamid)
        for gogid, gogpath in BasicGame.gog_games.items():
            if gogpath == path:
                self._mappings.gogAPPId.set_value(gogid)
        for originid, originpath in BasicGame.origin_games.items():
            if originpath == path:
                self._mappings.originManifestIds.set_value(originid)
        for epicid, epicpath in BasicGame.epic_games.items():
            if epicpath == path:
                self._mappings.epicAPPId.set_value(epicid)
        for eadesktopid, eadesktoppath in BasicGame.eadesktop_games.items():
            if eadesktoppath == path:
                self._mappings.eaDesktopContentId.set_value(eadesktopid)

    def documentsDirectory(self) -> QDir:
        return self._mappings.documentsDirectory.get()

    def savesDirectory(self) -> QDir:
        return self._mappings.savesDirectory.get()
