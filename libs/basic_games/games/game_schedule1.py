import json
from pathlib import Path

from PyQt6.QtCore import QDir

import mobase

from ..basic_features import BasicLocalSavegames, BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
)
from ..basic_game import BasicGame


def parse_schedule1_save_metadata(save_path: Path, save: mobase.ISaveGame):
    metadata_file = save_path / "Game.json"
    try:
        with open(metadata_file) as file:
            meta_data = json.load(file)
            name = meta_data["OrganisationName"]
            if name != (save_name := save.getName()):
                name = f"{save_name}  ({name})"
            return {
                "Name": name,
                "Game version": meta_data["GameVersion"],
            }
    except (FileNotFoundError, json.JSONDecodeError):
        return None


class Schedule1SaveGame(BasicGameSaveGame):
    def getName(self) -> str:
        metadata_file = self._filepath / "Game.json"
        try:
            with open(metadata_file) as file:
                meta_data = json.load(file)
                return meta_data["OrganisationName"]
        except (FileNotFoundError, json.JSONDecodeError):
            return (
                f"[{self.getSaveGroupIdentifier().rstrip('s')}] {self._filepath.stem}"
            )

    def getSaveGroupIdentifier(self) -> str:
        return self._filepath.parent.name


class Schedule1Game(BasicGame):
    Name = "Schedule I Support Plugin"
    Author = "shellbj"
    Version = "1.0.0"

    GameName = "Schedule I"
    GameShortName = "scheduleI"
    GameNexusName = "schedule1"
    GameNexusId = 7381
    GameSteamId = 3164500

    GameBinary = "Schedule I.exe"
    GameValidShortNames = ["schedule1", "scheduleI"]
    GameDataPath = ""
    GameSavesDirectory = r"%USERPROFILE%/AppData/LocalLow/TVGS/Schedule I/Saves"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Schedule-I"
    )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(
            BasicModDataChecker(
                GlobPatterns(
                    unfold=[
                        # Fixes bad packaging for select mods
                        "ModManager&PhoneApp",
                        "AutoClearCompletedDeals",
                    ],
                    valid=[
                        "meta.ini",  # Included in installed mod folder.
                        "Mods",
                        "Plugins",
                        "UserData",  # might not be needed
                    ],
                    delete=[
                        "*.md",
                        "icon.png",
                        "fomod",  # not sure this is needed either
                    ],
                    move={
                        "*.dll": "Mods/",
                        # If these even exisit outside of mod packs
                        "*.ini": "UserData/",
                        "*.cfg": "UserData/",
                        "*.json": "UserData/",
                    },
                )
            )
        )
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(
            BasicGameSaveGameInfo(
                None,  # no snapshot to add to the widget
                parse_schedule1_save_metadata,
            )
        )
        return True

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        return [
            Schedule1SaveGame(path)
            for path in Path(folder.absolutePath()).glob("*/SaveGame_[1-5]")
        ]
