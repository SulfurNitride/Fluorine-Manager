# -*- encoding: utf-8 -*-

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class TDUGame(BasicGame):
    Name = "Test Drive Unlimited Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Test Drive Unlimited"
    GameShortName = "tdu"
    GameNexusName = "testdriveunlimited"
    GameNexusId = 4615
    GameBinary = "TestDriveUnlimited.exe"
    GameDataPath = ""

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Test Drive Unlimited",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ).withArgument("-w -wx -vsync -bigbnks -offline"),
            mobase.ExecutableInfo(
                "Project Paradise Launcher",
                QFileInfo(
                    self.gameDirectory().absoluteFilePath(
                        "TDU - Project Paradise Launcher.exe"
                    )
                ),
            ),
        ]
