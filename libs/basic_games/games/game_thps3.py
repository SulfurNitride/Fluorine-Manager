# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class THPS3Game(BasicGame):
    Name = "Tony Hawk's Pro Skater 3 Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Tony Hawk's Pro Skater 3"
    GameShortName = "thps3"
    GameBinary = "Skate3.exe"
    GameDataPath = "Data"

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 3",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 3 Setup",
                QFileInfo(self.gameDirectory().absoluteFilePath("THPS3Setup.exe")),
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 3 (PARTYMOD)",
                QFileInfo(self.gameDirectory().absoluteFilePath("THPS3.exe")),
            ),
            mobase.ExecutableInfo(
                "PARTYMOD Configurator",
                QFileInfo(self.gameDirectory().absoluteFilePath("partyconfig.exe")),
            ),
        ]
