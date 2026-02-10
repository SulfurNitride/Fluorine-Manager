# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class NFSHSGame(BasicGame):
    Name = "Need for Speed: High Stakes Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Need for Speed: High Stakes"
    GameShortName = "nfshs"
    GameNexusName = "needforspeedhighstakes"
    GameNexusId = 6032
    GameBinary = "nfshs.exe"
    GameDataPath = ""

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Need for Speed: High Stakes",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            )
        ]
