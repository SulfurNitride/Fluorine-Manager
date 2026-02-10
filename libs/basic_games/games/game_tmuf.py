# -*- encoding: utf-8 -*-

from __future__ import annotations

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class TmufGame(BasicGame):
    Name = "Trackmania United Forever Support Plugin"
    Author = "uwx"
    Version = "1.0.0"
    Description = "Adds support for Trackmania United Forever game folder mods."

    GameName = "Trackmania United Forever"
    GameShortName = "tmuf"
    GameNexusName = "trackmaniaunited"
    GameNexusId = 1500
    GameSteamId = 7200
    GameBinary = "TmForeverLauncher.exe"
    GameDataPath = "GameData"

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Trackmania United Forever",
                QFileInfo(
                    self.gameDirectory(),
                    self.binaryName(),
                ),
            ),
        ]
