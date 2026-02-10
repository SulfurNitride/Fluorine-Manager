from typing import List, Optional

import mobase

from ..basic_game import BasicGame


class ZeusAndPoseidonModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        folders: List[mobase.IFileTree] = []
        files: List[mobase.FileTreeEntry] = []

        for entry in filetree:
            if isinstance(entry, mobase.IFileTree):
                folders.append(entry)
            else:
                files.append(entry)

        if len(folders) != 1:
            return mobase.ModDataChecker.INVALID

        folder = folders[0]
        pakfile = folder.name() + ".pak"
        if folder.exists(pakfile):
            if filetree.exists(pakfile):
                return mobase.ModDataChecker.VALID
            else:
                return mobase.ModDataChecker.FIXABLE

        return mobase.ModDataChecker.INVALID

    def fix(self, filetree: mobase.IFileTree) -> Optional[mobase.IFileTree]:
        first_entry = filetree[0]
        if not isinstance(first_entry, mobase.IFileTree):
            return None
        entry = first_entry.find(filetree[0].name() + ".pak")
        if entry is None:
            return None
        filetree.copy(entry, "", mobase.IFileTree.InsertPolicy.FAIL_IF_EXISTS)
        return filetree


class ZeusAndPoseidonGame(BasicGame):
    Name = "Zeus and Poseidon Support Plugin"
    Author = "Holt59"
    Version = "1.0.0a"

    GameName = "Zeus and Poseidon"
    GameShortName = "zeusandposeidon"  # No Nexus support
    GameSteamId = 566050
    GameGogId = 1207659039
    GameBinary = "Zeus.exe"
    GameDataPath = "Adventures"
    GameDocumentsDirectory = "%GAME_PATH%"
    GameSavesDirectory = "%GAME_PATH%/Save"
    GameSaveExtension = "sav"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Zeus%EF%BC%8BPoseidon"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(ZeusAndPoseidonModDataChecker())
        return True
