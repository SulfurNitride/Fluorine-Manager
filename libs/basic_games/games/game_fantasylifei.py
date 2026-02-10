from pathlib import Path

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicLocalSavegames, BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame
from ..steam_utils import find_steam_path


class FantasyLifeIModDataChecker(BasicModDataChecker):
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


class FantasyLifeI(BasicGame, mobase.IPluginFileMapper):
    Name = "Fantasy Life I Support Plugin"
    Author = "AmeliaCute"
    Version = "0.2.2"

    GameName = "FANTASY LIFE i"
    GameShortName = "fantasylifei"
    GameNexusName = "fantasylifeithegirlwhostealstime"
    GameValidShortNames = ["fli"]

    GameDataPath = "Game/Content/"
    GameBinary = "Game/Binaries/Win64/NFL1-Win64-Shipping.exe"
    GameSteamId = 2993780

    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Fantasy-Life-I:-The-Girl-Who-Steals-Time"
    )

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(FantasyLifeIModDataChecker())
        self._register_feature(BasicLocalSavegames(self))
        return True

    def executables(self):
        return [
            mobase.ExecutableInfo("Fantasy Life I", self.GameBinary),
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

            cloudSaves = child.joinpath("2993780/remote")
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
        return QDir(QDir(self.gameDirectory()).filePath("Game/Binaries/Win64"))

    def mappings(self) -> list[mobase.Mapping]:
        return [
            mobase.Mapping("*.dll", self.exeDirectory().absolutePath(), False, True),
        ]
