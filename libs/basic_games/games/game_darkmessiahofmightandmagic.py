import struct
from pathlib import Path

from PyQt6.QtGui import QImage

import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_game import BasicGame


class DarkMessiahOfMightAndMagicGame(BasicGame):
    Name = "Dark Messiah of Might and Magic Support Plugin"
    Author = "Holt59"
    Version = "0.1.0"

    GameName = "Dark Messiah of Might & Magic"
    GameShortName = "darkmessiahofmightandmagic"
    GameNexusName = "darkmessiahofmightandmagic"
    GameNexusId = 628
    GameSteamId = 2100
    GameBinary = "mm.exe"
    GameDataPath = "mm"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Dark-Messiah-of-Might-&-Magic"
    )

    GameDocumentsDirectory = "%GAME_PATH%/mm"
    GameSavesDirectory = "%GAME_PATH%/mm/SAVE"
    GameSaveExtension = "sav"

    def _read_save_tga(self, filepath: Path) -> QImage | None:
        # Qt TGA reader does not work for TGA, I hope that all files
        # have the same format:
        with open(
            filepath.parent.joinpath(filepath.name.replace(".sav", ".tga")), "rb"
        ) as fp:
            data = fp.read()
        _, _, w, h, bpp, _ = struct.unpack("<HHHHBB", data[8:18])
        if bpp != 24:
            return None
        return QImage(data[18:], w, h, QImage.Format.Format_RGB888)

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(BasicGameSaveGameInfo(self._read_save_tga))
        return True
