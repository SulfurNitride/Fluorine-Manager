from pathlib import Path
from typing import List, Optional

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class MasterDuelGame(BasicGame, mobase.IPluginFileMapper):
    Name = "Yu-Gi-Oh! Master Duel Support Plugin"
    Author = "The Conceptionist & uwx"
    Version = "1.0.2"
    Description = (
        "Adds support for basic Yu-Gi-Oh! Master Duel mods.\n"
        'Eligible folders for mods are "0000" and "AssetBundle".'
    )

    GameName = "Yu-Gi-Oh! Master Duel"
    GameShortName = "masterduel"
    GameNexusName = "yugiohmasterduel"
    GameNexusId = 4272
    GameSteamId = 1449850
    GameBinary = "masterduel.exe"

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Yu-Gi-Oh! Master Duel",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ).withArgument("-popupwindow"),
        ]

    # dataDirectory returns the specific LocalData folder because
    # this is where most mods will go to
    def dataDirectory(self) -> QDir:
        return QDir(self.userDataDir())

    _userDataDirCached: Optional[str] = None

    # Gets the LocalData/xxxxxxxxx directory. This directory has a different,
    # unique, 8-character hex name for each user.
    def userDataDir(self) -> str:
        if self._userDataDirCached is not None:
            return self._userDataDirCached

        dir = self.gameDirectory()
        dir.cd("LocalData")

        subdirs = dir.entryList(filters=QDir.Filter.Dirs | QDir.Filter.NoDotAndDotDot)
        dir.cd(subdirs[0])

        self._userDataDirCached = dir.absolutePath()
        return self._userDataDirCached

    def mappings(self) -> List[mobase.Mapping]:
        modsPath = Path(self._organizer.modsPath())
        unityMods = self.getUnityDataMods()

        mappings: List[mobase.Mapping] = []

        for modName in unityMods:
            m = mobase.Mapping()
            m.createTarget = False
            m.isDirectory = True
            m.source = modsPath.joinpath(modName, "AssetBundle").as_posix()
            m.destination = self.gameDirectory().filePath(
                Path("masterduel_Data", "StreamingAssets", "AssetBundle").as_posix()
            )
            mappings.append(m)

        return mappings

    def getUnityDataMods(self) -> list[str]:
        modsPath = Path(self._organizer.modsPath())
        allMods = self._organizer.modList().allModsByProfilePriority()

        unityMods: list[str] = []
        for modName in allMods:
            if self._organizer.modList().state(modName) & mobase.ModState.ACTIVE != 0:
                if modsPath.joinpath(modName, "AssetBundle").exists():
                    unityMods.append(modName)

        return unityMods
