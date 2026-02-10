from pathlib import Path
from typing import List

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame


class Witcher3SaveGame(BasicGameSaveGame):
    def allFiles(self):
        return [self._filepath.name, self._filepath.name.replace(".sav", ".png")]


class Witcher3Game(BasicGame):
    Name = "Witcher 3 Support Plugin"
    Author = "Holt59"
    Version = "1.0.0a"

    GameName = "The Witcher 3: Wild Hunt"
    GameShortName = "witcher3"
    GaneNexusHame = "witcher3"
    GameNexusId = 952
    GameSteamId = [499450, 292030]
    GameGogId = [1640424747, 1495134320, 1207664663, 1207664643]
    GameBinary = "bin/x64/witcher3.exe"
    GameDataPath = "Mods"
    GameSaveExtension = "sav"
    GameDocumentsDirectory = "%DOCUMENTS%/The Witcher 3"
    GameSavesDirectory = "%GAME_DOCUMENTS%/gamesaves"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-The-Witcher-3"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(BasicGameSaveGameInfo(lambda s: s.with_suffix(".png")))
        return True

    def iniFiles(self):
        return ["user.settings", "input.settings"]

    def listSaves(self, folder: QDir) -> List[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            Witcher3SaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"*.{ext}")
        ]
