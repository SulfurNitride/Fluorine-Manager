from enum import IntEnum, auto

import mobase

from . import bg3_utils


class Content(IntEnum):
    PAK = auto()
    WORKSPACE = auto()
    NATIVE = auto()
    LOOSE_FILES = auto()
    SE_FILES = auto()


class BG3DataContent(mobase.ModDataContent):
    BG3_CONTENTS: list[tuple[Content, str, str, bool] | tuple[Content, str, str]] = [
        (Content.WORKSPACE, "Mod workspace", ":/MO/gui/content/script"),
        (Content.PAK, "Pak", ":/MO/gui/content/bsa"),
        (Content.LOOSE_FILES, "Loose file override mod", ":/MO/gui/content/texture"),
        (Content.SE_FILES, "Script Extender Files", ":/MO/gui/content/inifile"),
        (Content.NATIVE, "Native DLL mod", ":/MO/gui/content/plugin"),
    ]

    def getAllContents(self) -> list[mobase.ModDataContent.Content]:
        return [
            mobase.ModDataContent.Content(id, name, icon, *filter_only)
            for id, name, icon, *filter_only in self.BG3_CONTENTS
        ]

    def getContentsFor(self, filetree: mobase.IFileTree) -> list[int]:
        contents: set[int] = set()
        for entry in filetree:
            if isinstance(entry, mobase.IFileTree):
                match entry.name():
                    case "Script Extender":
                        contents.add(Content.SE_FILES)
                    case "Data":
                        contents.add(Content.LOOSE_FILES)
                    case "Mods":
                        for e in entry:
                            if e.name().endswith(".pak"):
                                contents.add(Content.PAK)
                                break
                    case "bin":
                        contents.add(Content.NATIVE)
                    case _:
                        for e in entry:
                            if e.name() in bg3_utils.loose_file_folders:
                                contents.add(Content.WORKSPACE)
                                break
            elif entry.name().endswith(".pak"):
                contents.add(Content.PAK)
        return list(contents)
