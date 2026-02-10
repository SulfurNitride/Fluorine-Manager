from pathlib import Path

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicLocalSavegames, BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame
from ..steam_utils import find_steam_path


class DispatchModDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                move={
                    "*.fliarchive": "Mods/",
                    "*.pak": "Paks/~mods/",
                    "*.ucas": "Paks/~mods/",
                    "*.utoc": "Paks/~mods/",
                },
                valid=["L10N", "sound", "Mods", "Paks"],
            )
        )


class Dispatch(BasicGame, mobase.IPluginFileMapper):
    Name = "Dispatch Support Plugin"
    Author = "Syer10"
    Version = "0.1.0"

    GameName = "Dispatch"
    GameShortName = "dispatch"
    GameNexusName = "dispatch"
    GameValidShortNames = []

    GameDataPath = "Dispatch/Content/"
    GameBinary = "Dispatch/Binaries/Win64/Dispatch-Win64-Shipping.exe"
    GameSteamId = 2592160

    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Dispatch"
    )

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(DispatchModDataChecker())
        self._register_feature(BasicLocalSavegames(self))
        return True

    def executables(self):
        return [
            mobase.ExecutableInfo("Dispatch", self.GameBinary),
        ]

    ## SAVE
    # credit to game.darkestdungeon.py
    @staticmethod
    def getCloudSaveDirectory() -> str | None:
        steamPath = find_steam_path()
        if steamPath is None:
            return None

        userData = steamPath.joinpath("userdata")
        for child in userData.iterdir():
            name = child.name
            try:
                int(name)
            except ValueError:
                continue

            cloudSaves = child.joinpath("2592160/remote")
            if cloudSaves.exists() and cloudSaves.is_dir():
                return str(cloudSaves)
        return None

    def savesDirectory(self) -> QDir:
        return QDir(self.getCloudSaveDirectory())

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        saves: list[Path] = []
        for path in Path(folder.absolutePath()).glob("*.bin"):
            saves.append(path)

        ##TODO: need a proper implementation
        return [BasicGameSaveGame(path) for path in saves]

    ## MAPPING

    def exeDirectory(self) -> QDir:
        return QDir(QDir(self.gameDirectory()).filePath("Dispatch/Binaries/Win64"))

    def mappings(self) -> list[mobase.Mapping]:
        return [
            mobase.Mapping("*.dll", self.exeDirectory().absolutePath(), False, True),
        ]
