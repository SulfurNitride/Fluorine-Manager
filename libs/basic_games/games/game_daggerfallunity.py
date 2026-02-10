import mobase

from ..basic_game import BasicGame


class DaggerfallUnityModDataChecker(mobase.ModDataChecker):
    def __init__(self):
        super().__init__()
        self.validDirNames = [
            "biogs",
            "docs",
            "factions",
            "fonts",
            "mods",
            "questpacks",
            "quests",
            "sound",
            "soundfonts",
            "spellicons",
            "tables",
            "text",
            "textures",
            "worlddata",
            "aa",
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


class DaggerfallUnityGame(BasicGame):
    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(DaggerfallUnityModDataChecker())
        return True

    Name = "Daggerfall Unity Support Plugin"
    Author = "HomerSimpleton"
    Version = "1.0.0"

    GameName = "Daggerfall Unity"
    GameShortName = "daggerfallunity"
    GameBinary = "DaggerfallUnity.exe"
    GameLauncher = "DaggerfallUnity.exe"
    GameDataPath = "%GAME_PATH%/DaggerfallUnity_Data/StreamingAssets"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Daggerfall-Unity"
    )
