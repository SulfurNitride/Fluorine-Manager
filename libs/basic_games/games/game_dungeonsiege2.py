from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class DungeonSiegeIIModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def get_resources_and_maps(
        self, tree: mobase.IFileTree
    ) -> tuple[list[mobase.FileTreeEntry], list[mobase.FileTreeEntry]]:
        ress: list[mobase.FileTreeEntry] = []
        maps: list[mobase.FileTreeEntry] = []

        for e in tree:
            if e.isFile():
                if e.suffix().lower() == "ds2res":
                    ress.append(e)
                elif e.suffix().lower() == "ds2map":
                    maps.append(e)

        return ress, maps

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # Check if we have a Resources / Maps folder or .ds2res/.ds2map
        ress, maps = self.get_resources_and_maps(filetree)

        if not ress and not maps:
            if filetree.exists("Resources") or filetree.exists("Maps"):
                return mobase.ModDataChecker.VALID
            else:
                return mobase.ModDataChecker.INVALID

        return mobase.ModDataChecker.FIXABLE

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        ress, maps = self.get_resources_and_maps(filetree)

        if ress:
            rfolder = filetree.addDirectory("Resources")
            for r in ress:
                rfolder.insert(r, mobase.IFileTree.REPLACE)
        if maps:
            rfolder = filetree.addDirectory("Maps")
            for r in maps:
                rfolder.insert(r, mobase.IFileTree.REPLACE)
        return filetree


class DungeonSiegeIIGame(BasicGame):
    Name = "Dungeon Siege II"
    Author = "Holt59"
    Version = "0.1.1"

    GameName = "Dungeon Siege II"
    GameShortName = "dungeonsiegeii"
    GameNexusName = "dungeonsiegeii"
    GameNexusId = 2078
    GameSteamId = [39200]
    GameGogId = [1142020247]
    GameBinary = "DungeonSiege2.exe"
    GameDataPath = ""
    GameSavesDirectory = "%GAME_DOCUMENTS%/Save"
    GameDocumentsDirectory = "%DOCUMENTS%/My Games/Dungeon Siege 2"
    GameSaveExtension = "ds2party"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Dungeon-Siege-II"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(DungeonSiegeIIModDataChecker())
        return True

    def executables(self):
        execs = super().executables()
        execs.append(
            mobase.ExecutableInfo(
                "Dungeon Siege Video Configuration",
                QFileInfo(self.gameDirectory().absoluteFilePath("DS2VideoConfig.exe")),
            )
        )
        return execs

    def iniFiles(self):
        return ["DungeonSiege2.ini"]
