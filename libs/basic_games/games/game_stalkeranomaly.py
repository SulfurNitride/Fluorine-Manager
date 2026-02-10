from enum import IntEnum
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo, Qt
from PyQt6.QtWidgets import QLabel, QVBoxLayout, QWidget

import mobase

from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
)
from ..basic_game import BasicGame
from .stalkeranomaly import XRSave


class StalkerAnomalyModDataChecker(mobase.ModDataChecker):
    _valid_folders: list[str] = [
        "appdata",
        "bin",
        "db",
        "gamedata",
    ]

    def hasValidFolders(self, tree: mobase.IFileTree) -> bool:
        for e in tree:
            if e.isDir():
                if e.name().lower() in self._valid_folders:
                    return True

        return False

    def findLostData(self, tree: mobase.IFileTree) -> list[mobase.FileTreeEntry]:
        lost_db: list[mobase.FileTreeEntry] = []

        for e in tree:
            if e.isFile():
                if e.suffix().lower().startswith("db"):
                    lost_db.append(e)

        return lost_db

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        if self.hasValidFolders(filetree):
            return mobase.ModDataChecker.VALID

        if self.findLostData(filetree):
            return mobase.ModDataChecker.FIXABLE

        return mobase.ModDataChecker.INVALID

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        lost_db = self.findLostData(filetree)
        if lost_db:
            rfolder = filetree.addDirectory("db").addDirectory("mods")
            for r in lost_db:
                rfolder.insert(r, mobase.IFileTree.REPLACE)

        return filetree


class Content(IntEnum):
    INTERFACE = 0
    TEXTURE = 1
    MESH = 2
    SCRIPT = 3
    SOUND = 4
    MCM = 5
    CONFIG = 6


class StalkerAnomalyModDataContent(mobase.ModDataContent):
    content: list[int] = []

    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [
            mobase.ModDataContent.Content(
                Content.INTERFACE, "Interface", ":/MO/gui/content/interface"
            ),
            mobase.ModDataContent.Content(
                Content.TEXTURE, "Textures", ":/MO/gui/content/texture"
            ),
            mobase.ModDataContent.Content(
                Content.MESH, "Meshes", ":/MO/gui/content/mesh"
            ),
            mobase.ModDataContent.Content(
                Content.SCRIPT, "Scripts", ":/MO/gui/content/script"
            ),
            mobase.ModDataContent.Content(
                Content.SOUND, "Sounds", ":/MO/gui/content/sound"
            ),
            mobase.ModDataContent.Content(Content.MCM, "MCM", ":/MO/gui/content/menu"),
            mobase.ModDataContent.Content(
                Content.CONFIG, "Configs", ":/MO/gui/content/inifile"
            ),
        ]

    def walkContent(
        self, path: str, entry: mobase.FileTreeEntry
    ) -> mobase.IFileTree.WalkReturn:
        name = entry.name().lower()
        if entry.isFile():
            ext = entry.suffix().lower()
            if ext in ["dds", "thm"]:
                self.content.append(Content.TEXTURE)
                if path.startswith("gamedata/textures/ui"):
                    self.content.append(Content.INTERFACE)
            elif ext in ["omf", "ogf"]:
                self.content.append(Content.MESH)
            elif ext in ["script"]:
                self.content.append(Content.SCRIPT)
                if "_mcm" in name:
                    self.content.append(Content.MCM)
            elif ext in ["ogg"]:
                self.content.append(Content.SOUND)
            elif ext in ["ltx", "xml"]:
                self.content.append(Content.CONFIG)
                if path.startswith("gamedata/configs/ui"):
                    self.content.append(Content.INTERFACE)

        return mobase.IFileTree.WalkReturn.CONTINUE

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        self.content = []
        filetree.walk(self.walkContent, "/")
        return self.content


class StalkerAnomalySaveGame(BasicGameSaveGame):
    _filepath: Path

    xr_save: XRSave

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        self._filepath = filepath
        self.xr_save = XRSave(self._filepath)

    def getName(self) -> str:
        xr_save = self.xr_save
        player = xr_save.player
        if player:
            name = player.character_name_str
            time = xr_save.time_fmt
            return f"{name}, {xr_save.save_fmt} [{time}]"
        return ""

    def allFiles(self) -> list[str]:
        filepath = str(self._filepath)
        paths = [filepath]
        scoc = filepath.replace(".scop", ".scoc")
        if Path(scoc).exists():
            paths.append(scoc)
        dds = filepath.replace(".scop", ".dds")
        if Path(dds).exists():
            paths.append(dds)
        return paths


class StalkerAnomalySaveGameInfoWidget(mobase.ISaveGameInfoWidget):
    def __init__(self, parent: QWidget | None):
        super().__init__(parent)
        layout = QVBoxLayout()
        self._labelSave = self.newLabel(layout)
        self._labelName = self.newLabel(layout)
        self._labelFaction = self.newLabel(layout)
        self._labelHealth = self.newLabel(layout)
        self._labelMoney = self.newLabel(layout)
        self._labelRank = self.newLabel(layout)
        self._labelRep = self.newLabel(layout)
        self.setLayout(layout)
        palette = self.palette()
        palette.setColor(self.backgroundRole(), Qt.GlobalColor.black)
        self.setAutoFillBackground(True)
        self.setPalette(palette)
        self.setWindowFlags(
            Qt.WindowType.ToolTip | Qt.WindowType.BypassGraphicsProxyWidget
        )

    def newLabel(self, layout: QVBoxLayout) -> QLabel:
        label = QLabel()
        label.setAlignment(Qt.AlignmentFlag.AlignLeft)
        palette = label.palette()
        palette.setColor(label.foregroundRole(), Qt.GlobalColor.white)
        label.setPalette(palette)
        layout.addWidget(label)
        layout.addStretch()
        return label

    def setSave(self, save: mobase.ISaveGame):
        self.resize(240, 32)
        if not isinstance(save, StalkerAnomalySaveGame):
            return
        xr_save = save.xr_save
        player = xr_save.player
        if player:
            self._labelSave.setText(f"Save: {xr_save.save_fmt}")
            self._labelName.setText(f"Name: {player.character_name_str}")
            self._labelFaction.setText(f"Faction: {xr_save.getFaction()}")
            self._labelHealth.setText(f"Health: {player.health:.2f}%")
            self._labelMoney.setText(f"Money: {player.money} RU")
            self._labelRank.setText(f"Rank: {xr_save.getRank()} ({player.rank})")
            self._labelRep.setText(
                f"Reputation: {xr_save.getReputation()} ({player.reputation})"
            )


class StalkerAnomalySaveGameInfo(BasicGameSaveGameInfo):
    def getSaveGameWidget(self, parent: QWidget | None = None):
        return StalkerAnomalySaveGameInfoWidget(parent)


class StalkerAnomalyGame(BasicGame, mobase.IPluginFileMapper):
    Name = "STALKER Anomaly"
    Author = "Qudix"
    Version = "0.5.0"
    Description = "Adds support for STALKER Anomaly"

    GameName = "STALKER Anomaly"
    GameShortName = "stalkeranomaly"
    GameNexusName = "stalkeranomaly"
    GameNexusId = 3743
    GameBinary = "AnomalyLauncher.exe"
    GameDataPath = ""
    GameDocumentsDirectory = "%GAME_PATH%/appdata"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-S.T.A.L.K.E.R.-Anomaly"
    )

    GameSaveExtension = "scop"
    GameSavesDirectory = "%GAME_DOCUMENTS%/savedgames"

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer):
        BasicGame.init(self, organizer)
        self._register_feature(StalkerAnomalyModDataChecker())
        self._register_feature(StalkerAnomalyModDataContent())
        self._register_feature(StalkerAnomalySaveGameInfo())
        organizer.onAboutToRun(lambda _str: self.aboutToRun(_str))
        return True

    def aboutToRun(self, _str: str) -> bool:
        gamedir = self.gameDirectory()
        if gamedir.exists():
            # For mappings
            gamedir.mkdir("appdata")
            # The game will crash if this file exists in the
            # virtual tree rather than the game dir
            dbg_path = Path(self._gamePath, "gamedata/configs/cache_dbg.ltx")
            if not dbg_path.exists():
                dbg_path.parent.mkdir(parents=True, exist_ok=True)
                with open(dbg_path, "w", encoding="utf-8"):
                    pass
        return True

    def executables(self) -> list[mobase.ExecutableInfo]:
        info = [
            ["Anomaly Launcher", "AnomalyLauncher.exe"],
            ["Anomaly (DX11-AVX)", "bin/AnomalyDX11AVX.exe"],
            ["Anomaly (DX11)", "bin/AnomalyDX11.exe"],
            ["Anomaly (DX10-AVX)", "bin/AnomalyDX10AVX.exe"],
            ["Anomaly (DX10)", "bin/AnomalyDX10.exe"],
            ["Anomaly (DX9-AVX)", "bin/AnomalyDX9AVX.exe"],
            ["Anomaly (DX9)", "bin/AnomalyDX9.exe"],
            ["Anomaly (DX8-AVX)", "bin/AnomalyDX8AVX.exe"],
            ["Anomaly (DX8)", "bin/AnomalyDX8.exe"],
        ]
        gamedir = self.gameDirectory()
        return [
            mobase.ExecutableInfo(inf[0], QFileInfo(gamedir, inf[1])) for inf in info
        ]

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            StalkerAnomalySaveGame(path)
            for path in Path(folder.absolutePath()).glob(f"*.{ext}")
        ]

    def mappings(self) -> list[mobase.Mapping]:
        appdata = self.gameDirectory().filePath("appdata")
        m = mobase.Mapping()
        m.createTarget = True
        m.isDirectory = True
        m.source = appdata
        m.destination = appdata
        return [m]
