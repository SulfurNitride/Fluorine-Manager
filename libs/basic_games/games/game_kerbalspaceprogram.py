from pathlib import Path

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame


class KerbalSpaceProgramSaveGame(BasicGameSaveGame):
    def allFiles(self):
        files = [super().getFilepath()]
        banner = self._filepath.joinpath("banners").joinpath(f"${self.getName()}.png")
        if banner.exists():
            files.append(banner.as_posix())
        return files

    def getName(self):
        return self._filepath.stem

    def getSaveGroupIdentifier(self):
        return self._filepath.parent.name


class KerbalSpaceProgramGame(BasicGame):
    Name = "Kerbal Space Program Support Plugin"
    Author = "LaughingHyena"
    Version = "1.0.0"

    GameName = "Kerbal Space Program"
    GameShortName = "kerbalspaceprogram"
    GameNexusName = "kerbalspaceprogram"
    GameSteamId = [220200, 283740, 982970]
    GameBinary = "KSP_x64.exe"
    GameDataPath = "GameData"
    GameSavesDirectory = "%GAME_PATH%/saves"
    GameSaveExtension = "sfs"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Kerbal-Space-Program"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(
            BasicGameSaveGameInfo(
                lambda s: str(
                    Path(s).parent.joinpath("banners").joinpath(f"{Path(s).stem}.png")
                )
            )
        )
        return True

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            KerbalSpaceProgramSaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"*/*.{ext}")
        ]
