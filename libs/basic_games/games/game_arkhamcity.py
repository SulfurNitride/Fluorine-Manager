from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_features import BasicLocalSavegames
from ..basic_game import BasicGame
from ..steam_utils import find_steam_path


# Lifted from https://github.com/ModOrganizer2/modorganizer-basic_games/blob/71dbb8c557d43cba9d290674a332e7ecd1650261/games/game_darkestdungeon.py
class ArkhamCityModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()
        self.validDirNames = [
            "config",
            "cookedpcconsole",
            "localization",
            "movies",
            "moviesstereo",
            "splash",
        ]

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        for entry in filetree:
            if not entry.isDir():
                continue
            if entry.name().casefold() in self.validDirNames:
                return mobase.ModDataChecker.VALID
        return mobase.ModDataChecker.INVALID


class ArkhamCityGame(BasicGame):
    Name = "Batman: Arkham City Plugin"
    Author = "Paynamia"
    Version = "0.5.3"

    GameName = "Batman: Arkham City"
    GameShortName = "batmanarkhamcity"
    GameNexusId = 372
    GameSteamId = 200260
    GameGogId = 1260066469
    GameEpicId = "Egret"
    GameBinary = "Binaries/Win32/BatmanAC.exe"
    GameLauncher = "Binaries/Win32/BmLauncher.exe"
    GameDataPath = "BmGame"
    GameDocumentsDirectory = (
        "%DOCUMENTS%/WB Games/Batman Arkham City GOTY/BmGame/Config"
    )
    GameIniFiles = ["UserEngine.ini", "UserGame.ini", "UserInput.ini"]
    GameSaveExtension = "sgd"

    # This will only detect saves from the earliest-created Steam profile on the user's PC.
    def savesDirectory(self) -> QDir:
        docSaves = QDir(self.documentsDirectory().cleanPath("../../SaveData"))
        if self.is_steam():
            if (steamDir := find_steam_path()) is None:
                return docSaves
            for child in steamDir.joinpath("userdata").iterdir():
                if not child.is_dir() or child.name == "0":
                    continue
                steamSaves = child.joinpath("200260", "remote")
                if steamSaves.is_dir():
                    return QDir(str(steamSaves))
            else:
                return docSaves
        else:
            return docSaves

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(ArkhamCityModDataChecker())
        self._register_feature(BasicLocalSavegames(self))
        return True

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Batman: Arkham City",
                QFileInfo(self.gameDirectory(), "Binaries/Win32/BatmanAC.exe"),
            ),
            mobase.ExecutableInfo(
                "Arkham City Launcher",
                QFileInfo(self.gameDirectory(), "Binaries/Win32/BmLauncher.exe"),
            ),
        ]
