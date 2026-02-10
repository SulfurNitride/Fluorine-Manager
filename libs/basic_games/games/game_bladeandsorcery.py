import json
from collections.abc import Mapping
from pathlib import Path

from PyQt6.QtCore import QDateTime, QDir

import mobase

from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
    format_date,
)
from ..basic_game import BasicGame


class BaSSaveGame(BasicGameSaveGame):
    def __init__(self, filepath: Path):
        super().__init__(filepath)
        with open(self._filepath, "rb") as save:
            save_data = json.load(save)
        self._gameMode = save_data["mode"]["saveData"]["gameModeId"]
        self._gender = (
            "Male"
            if save_data["customization"]["creatureId"] == "PlayerDefaultMale"
            else "Female"
        )
        self._ethnicity = save_data["customization"]["ethnicGroupId"]
        h, m, s = save_data["playTime"].split(":")
        self._elapsed = (float(h), int(m), float(s))
        f_stat = self._filepath.stat()
        self._created = f_stat.st_birthtime
        self._modified = f_stat.st_mtime

    def getName(self) -> str:
        return f"{self.getPlayerSlug()} - {self._gameMode}"

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._created))

    def getModifiedTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(int(self._modified))

    def getPlayerSlug(self) -> str:
        return f"{self._gender} {self._ethnicity}"

    def getElapsed(self) -> str:
        return (
            f"{self._elapsed[0]} hours, "
            f"{self._elapsed[1]} minutes, "
            f"{int(self._elapsed[2])} seconds"
        )

    def getGameMode(self) -> str:
        return self._gameMode


def bas_parse_metadata(p: Path, save: mobase.ISaveGame) -> Mapping[str, str]:
    assert isinstance(save, BaSSaveGame)
    return {
        "Character": save.getPlayerSlug(),
        "Game Mode": save.getGameMode(),
        "Created At": format_date(save.getCreationTime()),
        "Last Saved": format_date(save.getModifiedTime()),
        "Session Duration": save.getElapsed(),
    }


class BaSGame(BasicGame):
    Name = "Blade & Sorcery Plugin"
    Author = "R3z Shark & Silarn & Jonny_Bro"
    Version = "0.5.1"

    GameName = "Blade & Sorcery"
    GameShortName = "bladeandsorcery"
    GameBinary = "BladeAndSorcery.exe"
    GameDataPath = r"BladeAndSorcery_Data\\StreamingAssets\\Mods"
    GameDocumentsDirectory = "%DOCUMENTS%/My Games/BladeAndSorcery"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Saves/Default"
    GameSaveExtension = "chr"
    GameSteamId = 629730
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Blade-&-Sorcery"
    )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        BasicGame.init(self, organizer)
        self._register_feature(
            BasicGameSaveGameInfo(get_metadata=bas_parse_metadata, max_width=400)
        )
        return True

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            BaSSaveGame(path) for path in Path(folder.absolutePath()).glob(f"*.{ext}")
        ]
