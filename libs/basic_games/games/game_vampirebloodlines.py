from pathlib import Path
from typing import List

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicLocalSavegames
from ..basic_game import BasicGame, BasicGameSaveGame


class VampireModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()
        self.validDirNames = [
            "cfg",
            "cl_dlls",
            "dlg",
            "dlls",
            "maps",
            "materials",
            "media",
            "models",
            "particles",
            "python",
            "resource",
            "scripts",
            "sound",
            "vdata",
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


class VampireSaveGame(BasicGameSaveGame):
    _filepath: Path

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        self._filepath = filepath
        self.name = None
        self.elapsedTime = None


class VampireTheMasqueradeBloodlinesGame(BasicGame):
    Name = "Vampire - The Masquerade: Bloodlines Support Plugin"
    Author = "John"
    Version = "1.0.0"
    Description = "Adds support for Vampires: The Masquerade - Bloodlines"

    GameName = "Vampire - The Masquerade: Bloodlines"
    GameShortName = "vampirebloodlines"
    GameNexusName = "vampirebloodlines"
    GameNexusId = 437
    GameSteamId = [2600]
    GameGogId = [1207659240]
    GameBinary = "vampire.exe"
    GameDataPath = "vampire"
    GameDocumentsDirectory = "%GAME_PATH%/vampire/cfg"
    GameSavesDirectory = "%GAME_PATH%/vampire/SAVE"
    GameSaveExtension = "sav"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Vampire:-The-Masquerade-%E2%80%90-Bloodlines"
    )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(VampireModDataChecker())
        self._register_feature(BasicLocalSavegames(self))
        return True

    def initializeProfile(self, directory: QDir, settings: mobase.ProfileSetting):
        # Create .cfg files if they don't exist
        for iniFile in self.iniFiles():
            iniPath = Path(self.documentsDirectory().absoluteFilePath(iniFile))
            if not iniPath.exists():
                with open(iniPath, "w") as _:
                    pass

        super().initializeProfile(directory, settings)

    def version(self):
        # Don't forget to import mobase!
        return mobase.VersionInfo(1, 0, 0, mobase.ReleaseType.FINAL)

    def iniFiles(self):
        return ["autoexec.cfg", "user.cfg"]

    def listSaves(self, folder: QDir) -> List[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            VampireSaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"*.{ext}")
        ]
