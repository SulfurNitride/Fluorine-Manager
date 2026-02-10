import datetime
import os
import struct
import time
from collections.abc import Mapping
from pathlib import Path
from typing import BinaryIO

from PyQt6.QtCore import QDateTime, QDir, QFile, QFileInfo

import mobase

from ..basic_features import BasicLocalSavegames
from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
    format_date,
)
from ..basic_game import BasicGame


class BlackAndWhite2ModDataChecker(mobase.ModDataChecker):
    _validFolderTree = {
        "<black & white 2>": ["audio", "data", "plugins", "scripts"],
        "audio": ["dialogue", "music", "sfx"],
        "music": [
            "buildingmusic",
            "chant",
            "cutscene",
            "dynamic music",
            "epicspell",
            "townalignment",
        ],
        "sfx": ["atmos", "creature", "game", "script", "spells", "video", "grass"],
        "data": [
            "art",
            "balance",
            "ctr",
            "effects",
            "encryptedshaders",
            "font",
            "handdemo",
            "interface",
            "landscape",
            "light particle effects",
            "lipsync",
            "physics",
            "sfx",
            "shaders",
            "symbols",
            "text",
            "textures",
            "tutorial avi",
            "visualeffects",
            "weathersystem",
            "zones",
        ],
        "art": [
            "binary_anim_libs",
            "binary_animations",
            "features",
            "models",
            "skins",
            "textures",
            "water",
        ],
        "ctr": [
            "badvisor_evil",
            "badvisor_good",
            "bape",
            "bgorilla",
            "bhand",
            "blion",
            "btiger",
            "bwolf",
            "damage",
            "siren",
        ],
        "font": ["asian"],
        "asian": ["korean", "traditional chinese"],
        "landscape": [
            "aztec",
            "bw2",
            "egyptian",
            "generic",
            "greek",
            "japanese",
            "norse",
            "skysettings",
        ],
        "tutorial avi": ["placeholder", "stills"],
        "visualeffects": ["textures"],
        "scripts": ["bw2"],
    }
    _validFileLocation = {
        "<black & white 2>": ["exe", "dll", "ico", "png", "jpeg", "jpg"]
    }
    _mapFile = ["chl", "bmp", "bwe", "ter", "pat", "xml", "wal", "txt"]
    _fileIgnore = ["readme", "read me", "meta.ini", "thumbs.db", "backup", ".png"]

    def fix(self, filetree: mobase.IFileTree):
        toMove: list[tuple[mobase.FileTreeEntry, str]] = []
        for entry in filetree:
            if any([sub in entry.name().casefold() for sub in self._fileIgnore]):
                continue
            elif entry.suffix() == "chl":
                toMove.append((entry, "/Scripts/BW2/"))
            elif entry.suffix() == "bmp":
                toMove.append((entry, "/Data/"))
            elif entry.suffix() == "txt":
                toMove.append((entry, "/Scripts/"))
            else:
                toMove.append((entry, "/Data/landscape/BW2/"))

        for entry, path in toMove:
            filetree.move(entry, path, policy=mobase.IFileTree.MERGE)

        return filetree

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # qInfo("Data validation start")
        root = filetree
        unpackagedMap = False

        for entry in filetree:
            entryName = entry.name().casefold()
            canIgnore = any([sub in entryName for sub in self._fileIgnore])
            if not canIgnore:
                parent = entry.parent()
                if parent is not None:
                    if parent != root:
                        parentName = parent.name().casefold()
                    else:
                        # qInfo(str(entryName))
                        parentName = "<black & white 2>"

                    if not entry.isDir():
                        if parentName in self._validFileLocation.keys():
                            if (
                                entry.suffix()
                                not in self._validFileLocation[parentName]
                            ):
                                if (
                                    entry.suffix() in self._mapFile
                                    or entryName == "map.txt"
                                ):
                                    unpackagedMap = True
                                else:
                                    return mobase.ModDataChecker.INVALID
                    else:
                        unpackagedMap = False
                        if parentName in self._validFolderTree.keys():
                            if entryName not in self._validFolderTree[parentName]:
                                return mobase.ModDataChecker.INVALID

        # qInfo(str(unpackagedMap))
        if unpackagedMap:
            return mobase.ModDataChecker.FIXABLE
        else:
            return mobase.ModDataChecker.VALID


class BlackAndWhite2SaveGame(BasicGameSaveGame):
    _saveInfLayout = {
        "start": [0x00000000, 0x00000004],
        "name": [0x00000004, 0x0000002C],
        "empty": [0x0000002C, 0x00000104],
        "land": [0x00000104, 0x00000108],
        "date": [0x00000108, 0x00000110],
        "empty1": [0x00000110, 0x00000114],
        "elapsed": [0x00000114, 0x00000118],
        "empty2": [0x00000108, 0x0000011C],
    }

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        self._filepath = Path(filepath)
        self.name: str = ""
        self.land: int = -1
        self.elapsed: int = 0
        self.lastsave: int = 0
        with open(self._filepath.joinpath("SaveGame.inf"), "rb") as info:
            # Name embedded in "SaveGame.inf" with UTF-16 encoding
            self.name = self.readInf(info, "name").decode("utf-16")
            # Land number embedded in "SaveGame.inf" as an int written in binary
            self.land = int.from_bytes(self.readInf(info, "land"), "little")
            # Getting elapsed time in second
            self.elapsed = int.from_bytes(self.readInf(info, "elapsed"), "little")
            # Getting date in 100th of nanosecond need to convert NT time
            # to UNIX time and offset localtime
            self.lastsave = int(
                (
                    struct.unpack("q", self.readInf(info, "date"))[0] / 10000
                    - 11644473600000
                )
                - (time.localtime().tm_gmtoff * 1000)
            )
            info.close()

    def readInf(self, inf: BinaryIO, key: str):
        inf.seek(self._saveInfLayout[key][0])
        return inf.read(self._saveInfLayout[key][1] - self._saveInfLayout[key][0])

    def allFiles(self) -> list[str]:
        files = [str(file) for file in self._filepath.glob("./*")]
        files.append(str(self._filepath))
        return files

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromMSecsSinceEpoch(self.lastsave)

    def getElapsed(self) -> str:
        return str(datetime.timedelta(seconds=self.elapsed))

    def getName(self) -> str:
        return self.name

    def getLand(self) -> str:
        return str(self.land)

    def getSaveGroupIdentifier(self):
        return self._filepath.parent.parent.name


def getMetadata(savepath: Path, save: mobase.ISaveGame) -> Mapping[str, str]:
    assert isinstance(save, BlackAndWhite2SaveGame)
    return {
        "Name": save.getName(),
        "Profile": save.getSaveGroupIdentifier(),
        "Land": save.getLand(),
        "Saved at": format_date(save.getCreationTime()),
        "Elapsed time": save.getElapsed(),
    }


PSTART_MENU = (
    str(os.getenv("ProgramData")) + "\\Microsoft\\Windows\\Start Menu\\Programs"
)


class BlackAndWhite2Game(BasicGame):
    Name = "Black & White 2 Support Plugin"
    Author = "Ilyu"
    Version = "1.0.1"

    GameName = "Black & White 2"
    GameShortName = "BW2"
    GameNexusName = "blackandwhite2"
    GameDataPath = "%GAME_PATH%"
    GameBinary = "white.exe"
    GameDocumentsDirectory = "%DOCUMENTS%/Black & White 2"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Profiles"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Black-&-White-2"
    )

    _program_link = PSTART_MENU + "\\Black & White 2\\Black & White® 2.lnk"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        BasicGame.init(self, organizer)

        self._register_feature(BlackAndWhite2ModDataChecker())
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(
            BasicGameSaveGameInfo(get_metadata=getMetadata, max_width=400)
        )
        return True

    def detectGame(self):
        super().detectGame()

        program_path = Path(self._program_link)
        if program_path.exists():
            installation_path = Path(QFileInfo(self._program_link).symLinkTarget())
            if installation_path.exists():
                self.setGamePath(installation_path.parent)

        return

    def executables(self) -> list[mobase.ExecutableInfo]:
        execs = super().executables()

        """
        A bat file to load modded executables from VFS.
        """
        workaroundPath = self._gamePath + "/" + self.GameBinary[:-4] + ".bat"

        try:
            workaround = open(workaroundPath, "rt")
        except FileNotFoundError:
            with open(workaroundPath, "wt") as workaround:
                workaround.write('start "" "' + self.GameBinary + '"')
        workaround.close()

        execs.append(
            mobase.ExecutableInfo(
                self.GameShortName + " Modded Exec", QFileInfo(workaroundPath)
            )
        )

        return execs

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        profiles: list[Path] = []
        for path in Path(folder.absolutePath()).glob("*/Saved Games/*"):
            if (
                path.name == "Autosave"
                or path.name == "Pictures"
                or "_invalid" in path.name
            ):
                continue
            if path.is_dir():
                saveFolder = QDir(str(path))
                if not saveFolder.exists("SaveGame.inf"):
                    savePath = saveFolder.absolutePath()
                    QFile.rename(savePath, savePath + "_invalid")
                    continue
            else:
                continue

            profiles.append(path)

        return [BlackAndWhite2SaveGame(path) for path in profiles]


class BOTGGame(BlackAndWhite2Game):
    Name = "Black & White 2 Battle of the Gods Support Plugin"

    GameName = "Black & White 2 Battle of the Gods"
    GameShortName = "BOTG"
    GameBinary = "BattleOfTheGods.exe"
    GameDocumentsDirectory = "%DOCUMENTS%/Black & White 2 - Battle of the Gods"
    GameSavesDirectory = "%GAME_DOCUMENTS%/Profiles"

    _program_link = (
        PSTART_MENU + "\\Black & White 2 Battle of the Gods"
        "\\Black & White® 2 Battle of the Gods.lnk"
    )
