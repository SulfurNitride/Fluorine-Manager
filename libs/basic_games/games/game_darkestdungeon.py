import json
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame, BasicGameSaveGame
from ..steam_utils import find_steam_path


class DarkestDungeonModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()
        self.validDirNames = [
            "activity_log",
            "audio",
            "campaign",
            "colours",
            "curios",
            "cursors",
            "dlc",
            "dungeons",
            "effects",
            "fe_flow",
            "fonts",
            "fx",
            "game_over",
            "heroes",
            "inventory",
            "loading_screen",
            "localization",
            "loot",
            "maps",
            "modes",
            "monsters",
            "overlays",
            "panels",
            "props",
            "raid",
            "raid_result",
            "scripts",
            "scrolls",
            "shaders",
            "shared",
            "trinkets",
            "upgrades",
            "video",
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


class DarkestDungeonSaveGame(BasicGameSaveGame):
    def __init__(self, filepath: Path):
        super().__init__(filepath)
        dataPath = filepath.joinpath("persist.game.json")
        self.name: str = ""
        if self.isBinary(dataPath):
            self.loadBinarySaveFile(dataPath)
        else:
            self.loadJSONSaveFile(dataPath)

    @staticmethod
    def isBinary(dataPath: Path) -> bool:
        with dataPath.open(mode="rb") as fp:
            magic = fp.read(4)
            # magic number in binary save files
            return magic == b"\x01\xb1\x00\x00"

    def loadJSONSaveFile(self, dataPath: Path):
        text = dataPath.read_text()
        content = json.loads(text)
        data = content["data"]
        self.name = str(data["estatename"])

    def loadBinarySaveFile(self, dataPath: Path):
        # see https://github.com/robojumper/DarkestDungeonSaveEditor
        with dataPath.open(mode="rb") as fp:
            # read Header

            # skip to headerLength
            fp.seek(8, 0)
            headerLength = int.from_bytes(fp.read(4), "little")
            if headerLength != 64:
                raise ValueError("Header Length is not 64: " + str(headerLength))
            fp.seek(4, 1)

            # meta1Size = int.from_bytes(fp.read(4), "little")
            fp.seek(4, 1)
            # numMeta1Entries = int.from_bytes(fp.read(4), "little")
            fp.seek(4, 1)

            meta1Offset = int.from_bytes(fp.read(4), "little")
            fp.seek(16, 1)
            numMeta2Entries = int.from_bytes(fp.read(4), "little")
            meta2Offset = int.from_bytes(fp.read(4), "little")
            fp.seek(4, 1)

            # dataLength = int.from_bytes(fp.read(4), "little")
            fp.seek(4, 1)

            dataOffset = int.from_bytes(fp.read(4), "little")

            # read Meta1 Block
            fp.seek(meta1Offset, 0)
            meta1DataLength = meta2Offset - meta1Offset
            if meta1DataLength % 16 != 0:
                raise ValueError(
                    "Meta1 has wrong number of bytes: " + str(meta1DataLength)
                )

            # read Meta2 Block
            fp.seek(meta2Offset, 0)
            meta2DataLength = dataOffset - meta2Offset
            if meta2DataLength % 12 != 0:
                raise ValueError(
                    "Meta2 has wrong number of bytes: " + str(meta2DataLength)
                )
            meta2List: list[tuple[int, int, int]] = []
            for _ in range(numMeta2Entries):
                entryHash = int.from_bytes(fp.read(4), "little")
                offset = int.from_bytes(fp.read(4), "little")
                fieldInfo = int.from_bytes(fp.read(4), "little")
                meta2List.append((entryHash, offset, fieldInfo))

            # read Data
            fp.seek(dataOffset, 0)
            for x in range(numMeta2Entries):
                meta2Entry = meta2List[x]
                fp.seek(dataOffset + meta2Entry[1], 0)
                nameLength = (meta2Entry[2] & 0b11111111100) >> 2
                # null terminated string
                nameBytes = fp.read(nameLength - 1)
                fp.seek(1, 1)
                name = bytes.decode(nameBytes, "utf-8")
                if name != "estatename":
                    continue
                valueLength = int.from_bytes(fp.read(4), "little")
                valueBytes = fp.read(valueLength - 1)
                value = bytes.decode(valueBytes, "utf-8")
                self.name = value
                break

    def getName(self) -> str:
        if self.name == "":
            return super().getName()
        return self.name


class DarkestDungeonGame(BasicGame):
    Name = "DarkestDungeon"
    Author = "erri120"
    Version = "0.2.0"

    GameName = "Darkest Dungeon"
    GameShortName = "darkestdungeon"
    GameNexusName = "darkestdungeon"
    GameNexusId = 804
    GameSteamId = 262060
    GameGogId = 1719198803
    GameBinary = "_windows/win64/Darkest.exe"
    GameDataPath = ""
    GameDocumentsDirectory = "%DOCUMENTS%/Darkest"
    GameSavesDirectory = "%GAME_DOCUMENTS%"
    GameSupportURL = "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Darkest-Dungeon"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(DarkestDungeonModDataChecker())
        return True

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_name = self.gameName()
        game_dir = self.gameDirectory()

        return [
            mobase.ExecutableInfo(
                f"{game_name} Steam x64",
                QFileInfo(game_dir, "_windows/win64/Darkest.exe"),
            ).withWorkingDirectory(game_dir),
            mobase.ExecutableInfo(
                f"{game_name} x64",
                QFileInfo(game_dir, "_windowsnosteam/win64/Darkest.exe"),
            ).withWorkingDirectory(game_dir),
            mobase.ExecutableInfo(
                f"{game_name} Steam x32",
                QFileInfo(game_dir, "_windows/win32/Darkest.exe"),
            ).withWorkingDirectory(game_dir),
            mobase.ExecutableInfo(
                f"{game_name} x32",
                QFileInfo(game_dir, "_windowsnosteam/win32/Darkest.exe"),
            ).withWorkingDirectory(game_dir),
        ]

    @staticmethod
    def getCloudSaveDirectory() -> str | None:
        steamPath = find_steam_path()
        if steamPath is None:
            return None

        userData = steamPath.joinpath("userdata")
        for child in userData.iterdir():
            name = child.name
            try:
                userID = int(name)
            except ValueError:
                userID = -1
            if userID == -1:
                continue
            cloudSaves = child.joinpath("262060", "remote")
            if cloudSaves.exists() and cloudSaves.is_dir():
                return str(cloudSaves)
        return None

    def savesDirectory(self) -> QDir:
        documentsSaves = self.documentsDirectory()

        if not self.is_steam():
            return documentsSaves

        cloudSaves = self.getCloudSaveDirectory()
        if cloudSaves is None:
            return documentsSaves

        return QDir(cloudSaves)

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        profiles: list[Path] = []
        for path in Path(folder.absolutePath()).glob("profile_*"):
            # profile_9 is only for the Multiplayer DLC "The Butcher's Circus"
            # and contains different files than other profiles
            if path.name == "profile_9":
                continue
            profiles.append(path)

        return [DarkestDungeonSaveGame(path) for path in profiles]
