from __future__ import annotations

from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class ControlGame(BasicGame):
    Name = "Control Support Plugin"
    Author = "Zash"
    Version = "1.0.0"

    GameName = "Control"
    GameShortName = "control"
    GameNexusId = 2936
    GameSteamId = 870780
    GameGogId = 2049187585
    GameEpicId = "calluna"
    GameBinary = "Control.exe"
    GameDataPath = ""

    def executables(self):
        return [
            mobase.ExecutableInfo(
                "Control (Launcher)",
                QFileInfo(
                    self.gameDirectory(),
                    self.binaryName(),
                ),
            ),
            mobase.ExecutableInfo(
                "Control DX11",
                QFileInfo(
                    self.gameDirectory(),
                    "Control_DX11.exe",
                ),
            ),
            mobase.ExecutableInfo(
                "Control DX12",
                QFileInfo(
                    self.gameDirectory(),
                    "Control_DX12.exe",
                ),
            ),
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        try:
            efls = super().executableForcedLoads()
        except AttributeError:
            efls = []

        libraries = ["iphlpapi.dll", "xinput1_4.dll"]
        efls.extend(
            mobase.ExecutableForcedLoadSetting(
                exe.binary().fileName(), lib
            ).withEnabled(True)
            for lib in libraries
            for exe in self.executables()
        )
        return efls
