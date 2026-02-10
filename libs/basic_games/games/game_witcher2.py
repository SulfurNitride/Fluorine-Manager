from pathlib import Path
from typing import List

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame


class Witcher2SaveGame(BasicGameSaveGame):
    def allFiles(self):
        return [
            self._filepath.name,
            self._filepath.name.replace(".sav", "_640x360.bmp"),
        ]


class Witcher2Game(BasicGame):
    Name = "Witcher 2 Support Plugin"
    Author = "DefinitelyNotSade"
    Version = "1.0.0"

    GameName = "The Witcher 2: Assassins of Kings"
    GameShortName = "witcher2"
    GaneNexusHame = "witcher2"
    # GameNexusId = 952
    GameSteamId = 20920
    GameGogId = 1207658930
    GameLauncher = "Launcher.exe"
    GameBinary = "bin/witcher2.exe"
    GameDataPath = "CookedPC"
    GameSaveExtension = "sav"
    GameDocumentsDirectory = "%DOCUMENTS%/witcher 2/Config"
    GameSavesDirectory = "%GAME_DOCUMENTS%/../gamesaves"

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(
            BasicGameSaveGameInfo(
                lambda s: Path(str(s.parent) + "\\" + s.stem + "_640x360").with_suffix(
                    ".bmp"
                )
            )
        )
        return True

    def iniFiles(self):
        return [
            "User.ini",
            "Rendering.ini",
            "Community.ini",
            "UserContent.ini",
            "DIMapping.ini",
            "Input_QWERTY.ini",
            "Input_AZERTY.ini",
            "Input_QWERTZ.ini",
        ]

    def listSaves(self, folder: QDir) -> List[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            Witcher2SaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"*.{ext}")
        ]
