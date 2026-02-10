from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class RailworksGame(BasicGame):
    Name = "Train Simulator Classic Support Plugin"
    Author = "Ryan Young"
    Version = "1.1.0"

    GameName = "Train Simulator"
    GameShortName = "railworks"
    GameBinary = "RailWorks.exe"
    GameDataPath = ""
    GameSteamId = "24010"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Train-Simulator-Classic"
    )

    def executables(self):
        game_directory = self.gameDirectory()
        executables: list[tuple[str, str]] = [
            ("32-bit", "RailWorks.exe"),
            ("64-bit", "RailWorks64.exe"),
            ("64-bit, DirectX 12", "RailWorksDX12_64.exe"),
        ]
        return [
            mobase.ExecutableInfo(
                f"Train Simulator ({name})",
                QFileInfo(game_directory.absoluteFilePath(path)),
            )
            for name, path in executables
        ]
