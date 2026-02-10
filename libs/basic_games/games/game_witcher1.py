from pathlib import Path
from typing import BinaryIO, List

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame, BasicGameSaveGame


class Witcher1SaveGame(BasicGameSaveGame):
    def __init__(self, filepath: Path):
        super().__init__(filepath)
        self.areaName: str = ""
        self.parseSaveFile(filepath)

    @staticmethod
    def readInt(fp: BinaryIO, length: int = 4) -> int:
        return int.from_bytes(fp.read(length), "little")

    @staticmethod
    def readFixedString(fp: BinaryIO, length: int) -> str:
        b: bytes = fp.read(length)
        res = b.decode("utf-16")
        return res.rstrip("\0")

    def parseSaveFile(self, filepath: Path):
        # https://github.com/xoreos/xoreos/blob/82bd991052732ab1f8f75f512b3dfabfcc92ae8f/src/aurora/thewitchersavefile.cpp#L60
        with filepath.open(mode="rb") as fp:
            magic = fp.read(4)
            if magic != b"RGMH":
                raise ValueError("Invalid TheWitcherSave file!")

            version = self.readInt(fp)
            if version != 1:
                raise ValueError("Invalid TheWitcherSave file!")

            # TODO: get the preview image
            # dataOffset = self.readInt(fp, 8)
            fp.seek(8, 1)
            fp.seek(8, 1)
            fp.seek(4 * 4, 1)

            lightningStorm = self.readFixedString(fp, 2048)
            if lightningStorm != "Lightning Storm":
                raise ValueError('Missing "Lightning Storm"')

            areaName1 = self.readFixedString(fp, 2048)
            areaName2 = self.readFixedString(fp, 2048)

            if areaName1 != areaName2:
                raise ValueError("Invalid Area Name!")

            self.areaName = areaName1

    def getName(self) -> str:
        return self.areaName


class Witcher1Game(BasicGame):
    Name = "Witcher 1 Support Plugin"
    Author = "erri120"
    Version = "1.0.0"

    GameName = "The Witcher: Enhanced Edition"
    GameShortName = "witcher"
    GameNexusName = "witcher"
    GameNexusId = 150
    GameSteamId = 20900
    GameGogId = 1207658924
    GameBinary = "System/witcher.exe"
    GameDataPath = "Data"
    GameSaveExtension = "TheWitcherSave"
    GameDocumentsDirectory = "%DOCUMENTS%/The Witcher"
    GameSavesDirectory = "%GAME_DOCUMENTS%/saves"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-The-Witcher"
    )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        return True

    def executables(self) -> List[mobase.ExecutableInfo]:
        path = QFileInfo(self.gameDirectory(), "System/witcher.exe")
        return [mobase.ExecutableInfo("The Witcher", path)]

    def listSaves(self, folder: QDir) -> List[mobase.ISaveGame]:
        return [
            Witcher1SaveGame(path)
            for path in Path(folder.absolutePath()).glob("*.TheWitcherSave")
        ]
