# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class THPS4Game(BasicGame):
    Name = "Tony Hawk's Underground Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Tony Hawk's Underground"
    GameShortName = "thug"
    GameBinary = "THUG.exe"
    GameDataPath = "Data"

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Tony Hawk's Underground",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Underground Launcher",
                QFileInfo(self.gameDirectory().absoluteFilePath("../Launcher.exe")),
            ).withWorkingDirectory(
                QDir(QDir.cleanPath(self.gameDirectory().absoluteFilePath("..")))
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Underground (ClownJob'd)",
                QFileInfo(self.gameDirectory().absoluteFilePath("THUGONE.exe")),
            ),
        ]
