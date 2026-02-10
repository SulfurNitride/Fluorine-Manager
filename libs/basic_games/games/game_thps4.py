# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class THPS4Game(BasicGame):
    Name = "Tony Hawk's Pro Skater 4 Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Tony Hawk's Pro Skater 4"
    GameShortName = "thps4"
    GameBinary = "Skate4.exe"
    GameDataPath = "Data"

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 4",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 4 Setup",
                QFileInfo(self.gameDirectory().absoluteFilePath("../Start.exe")),
            ).withWorkingDirectory(
                QDir(QDir.cleanPath(self.gameDirectory().absoluteFilePath("..")))
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Pro Skater 4 (PARTYMOD)",
                QFileInfo(self.gameDirectory().absoluteFilePath("THPS4.exe")),
            ),
            mobase.ExecutableInfo(
                "PARTYMOD Configurator",
                QFileInfo(self.gameDirectory().absoluteFilePath("partyconfig.exe")),
            ),
        ]
