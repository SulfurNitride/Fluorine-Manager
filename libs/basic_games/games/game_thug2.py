# -*- encoding: utf-8 -*-

import os

from PyQt6.QtCore import QDir, QFileInfo

import mobase

from ..basic_game import BasicGame


class THPS4Game(BasicGame):
    Name = "Tony Hawk's Underground 2 Support Plugin"
    Author = "uwx"
    Version = "1.0.0"

    GameName = "Tony Hawk's Underground 2"
    GameShortName = "thug2"
    GameBinary = "THUG2.exe"
    GameDataPath = "Data"

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Tony Hawk's Underground 2",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Underground 2 Launcher",
                QFileInfo(self.gameDirectory().absoluteFilePath("../Launcher.exe")),
            ).withWorkingDirectory(
                QDir(QDir.cleanPath(self.gameDirectory().absoluteFilePath("..")))
            ),
            mobase.ExecutableInfo(
                "Tony Hawk's Underground 2 (ClownJob'd)",
                QFileInfo(self.gameDirectory().absoluteFilePath("THUGTWO.exe")),
            ),
            mobase.ExecutableInfo(
                "THUG Pro Launcher",
                QFileInfo(
                    QDir(os.getenv("LOCALAPPDATA")).absoluteFilePath(
                        "THUG Pro/THUGProLauncher.exe"
                    )
                ),
            ),
            mobase.ExecutableInfo(
                "THUG Pro",
                QFileInfo(
                    QDir(os.getenv("LOCALAPPDATA")).absoluteFilePath(
                        "THUG Pro/THUGPro.exe"
                    )
                ),
            ),
        ]
