import os
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class GTA3DefinitiveEditionModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        for entry in filetree:
            if Path(entry.name().casefold()).suffix == ".pak":
                return mobase.ModDataChecker.VALID

        return mobase.ModDataChecker.INVALID


class GTA3DefinitiveEditionGame(BasicGame):
    Name = "Grand Theft Auto III - Definitive Edition Support Plugin"
    Author = "dekart811"
    Version = "1.0"

    GameName = "GTA III - Definitive Edition"
    GameShortName = "grandtheftautothetrilogy"
    GameNexusName = "grandtheftautothetrilogy"
    GameBinary = "Gameface/Binaries/Win64/LibertyCity.exe"
    GameDataPath = "Gameface/Content/Paks/~mods"
    GameDocumentsDirectory = (
        "%USERPROFILE%/Documents/Rockstar Games/"
        "GTA III Definitive Edition/Config/WindowsNoEditor"
    )
    GameSavesDirectory = "%GAME_DOCUMENTS%/../../SaveGames"
    GameSaveExtension = "sav"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Grand-Theft-Auto:-The-Trilogy-%E2%80%90-The-Definitive-Edition"
    )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(GTA3DefinitiveEditionModDataChecker())
        return True

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "GTA III - Definitive Edition",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "GTA III - Definitive Edition (DirectX 12)",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ).withArgument("-dx12"),
        ]

    def iniFiles(self):
        return ["GameUserSettings.ini", "CustomSettings.ini"]

    def initializeProfile(self, directory: QDir, settings: mobase.ProfileSetting):
        # Create the mods directory if it doesn't exist
        modsPath = self.dataDirectory().absolutePath()
        if not os.path.exists(modsPath):
            os.mkdir(modsPath)

        super().initializeProfile(directory, settings)
