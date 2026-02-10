from __future__ import annotations

from pathlib import Path

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame


class SilksongModDataChecker(BasicModDataChecker):
    def __init__(self, patterns: GlobPatterns | None = None):
        super().__init__(
            GlobPatterns(
                valid=[
                    # BepInEx files go to root
                    "BepInEx",
                    "doorstop_config.ini",
                    "winhttp.dll",
                    ".doorstop_version",
                ],
                delete=[
                    "*.txt",
                    "*.md",
                    "manifest.json",
                    "icon.png",
                ],
            ).merge(patterns or GlobPatterns()),
        )

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # Check if it contains BepInEx folders/files - if so, keep structure
        if self._has_bepinex_structure(filetree):
            return self.VALID

        # Everything else needs to go to BepInEx/plugins/
        return self.FIXABLE

    def _has_bepinex_structure(self, filetree: mobase.IFileTree) -> bool:
        """Check if the mod has BepInEx folder structure"""
        for entry in filetree:
            name = entry.name().lower()
            if name in [
                "bepinex",
                "doorstop_config.ini",
                "winhttp.dll",
                ".doorstop_version",
            ]:
                return True
        return False

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        # First apply the basic fix (handles delete patterns)
        filetree = super().fix(filetree)

        # If no BepInEx structure, move everything to BepInEx/plugins/
        if not self._has_bepinex_structure(filetree):
            # Move all top-level items to BepInEx/plugins/
            items_to_move = list(filetree)
            for item in items_to_move:
                filetree.move(item, f"BepInEx/plugins/{item.name()}")

        return filetree


class SilksongGame(BasicGame):
    Name = "Hollow Knight: Silksong Support Plugin"
    Author = "Nikirack"
    Version = "1.0.0"

    GameName = "Hollow Knight: Silksong"
    GameShortName = "hollowknightsilksong"  # Match the error message
    GameNexusName = "hollowknightsilksong"
    GameSteamId = 1030300
    GameBinary = r"Hollow Knight Silksong.exe"
    GameDataPath = ""
    GameSavesDirectory = (
        r"%USERPROFILE%\AppData\LocalLow\Team Cherry\Hollow Knight Silksong"
    )
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Hollow-Knight-Silksong"
    )

    _forced_libraries = ["winhttp.dll"]

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(SilksongModDataChecker())
        return True

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        saves: list[mobase.ISaveGame] = []
        save_dir = Path(folder.absolutePath())

        if save_dir.exists():
            # Look for common save file patterns
            for pattern in ["*.save", "user*.dat"]:
                for save_file in save_dir.rglob(pattern):
                    saves.append(BasicGameSaveGame(save_file))

        return saves

    def executables(self) -> list[mobase.ExecutableInfo]:
        return [
            mobase.ExecutableInfo(
                self.gameName(),
                self.gameDirectory().absoluteFilePath(self.binaryName()),
            )
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        return [
            mobase.ExecutableForcedLoadSetting(self.binaryName(), lib).withEnabled(True)
            for lib in self._forced_libraries
        ]
