# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class TDU2Game(BasicGame):
    Name = "Test Drive Unlimited 2 Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Test Drive Unlimited 2"
    GameShortName = "tdu2"
    GameNexusName = "testdriveunlimited2"
    GameNexusId = 2353
    GameSteamId = 9930
    GameBinary = "UpLauncher.exe"
    GameDataPath = ""

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Test Drive Unlimited 2",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "TDU2 Universal Launcher",
                QFileInfo(
                    self.gameDirectory().absoluteFilePath("TDU2_Universal_Launcher.exe")
                ),
            ),
        ]
