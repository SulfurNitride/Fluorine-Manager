from typing import Tuple

import mobase

from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_game import BasicGame


class SilentHill2RemakeModDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                delete=[
                    "*.txt",
                    "*.md",
                    "manifest.json",
                    "icon.png",
                ],
            )
        )
        self.mod_path = ["SHProto", "Content", "Paks", "~mod"]
        self.mod_path_lower = [name.lower() for name in self.mod_path]

    def _find_tree(
        self, filetree: mobase.IFileTree
    ) -> Tuple[str | None, mobase.FileTreeEntry | None]:
        """
        Search the given filetree for a directory name that matches any component
        of self.mod_path (case-insensitive).

        Returns:
            (prefix, entry)
            prefix: The missing part before the match (e.g. 'SHProto/Content/')
            entry:  The IFileTree entry that matched (e.g. the 'Paks' directory)
            (None, None) if nothing matches.
        """
        for entry in filetree:
            if not entry.isDir():
                continue

            name_lower = entry.name().lower()
            for i, component in enumerate(self.mod_path_lower):
                if name_lower == component:
                    # Build the prefix string for everything *before* this match
                    prefix_parts = self.mod_path[:i]
                    prefix = "/".join(prefix_parts) + ("/" if prefix_parts else "")
                    return (prefix, entry)
        # No matches found
        return (None, None)

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # Check for fully valid layout
        has_entry, _ = self._find_tree(filetree)
        if has_entry is None:
            # in this case we check to make sure there's a .pak file
            for entry in filetree:
                if entry.name().lower().endswith(".pak") and entry.isFile():
                    return mobase.ModDataChecker.FIXABLE
        elif has_entry == "":
            return mobase.ModDataChecker.VALID
        else:
            return mobase.ModDataChecker.FIXABLE

        # Otherwise, not recognizable
        return mobase.ModDataChecker.INVALID

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        filetree = super().fix(filetree)
        prefix, item = self._find_tree(filetree)
        if prefix is None:
            foundAPak = False
            # Move all top-level items to BepInEx/plugins/
            items_to_move = list(filetree)
            for cur_item in items_to_move:
                if cur_item.name().lower().endswith(".pak"):
                    foundAPak = True
                filetree.move(cur_item, f"SHProto/Content/Paks/~mod/{cur_item.name()}")
            # foundAPack MUST be true because if 'prefix' returned None then
            # there must be a .pak file or dataLooksValid wouldn't have returned
            # a FIXABLE. This is therefore just a sanity check
            assert foundAPak
            return filetree
        elif prefix == "":
            return filetree
        else:
            # if prefix is not None then item cannot be None
            assert item is not None
            filetree.move(item, f"{prefix}{item.name()}")
            return filetree


class SilentHill2RemakeGame(BasicGame):
    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(SilentHill2RemakeModDataChecker())
        return True

    Name = "Silent Hill 2 Remake Support Plugin"
    Author = "HomerSimpleton Returns"
    Version = "1.0"

    GameName = "Silent Hill 2 Remake"
    GameShortName = "silenthill2"
    GameNexusName = "silenthill2"

    GameBinary = "SHProto/Binaries/Win64/SHProto-Win64-Shipping.exe"
    GameLauncher = "SHProto.exe"
    GameDataPath = "%GAME_PATH%"
    GameSupportURL = "https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/Game:-Silent-Hill-2-Remake"

    GameGogId = [1225972913, 2051029707]
