from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class StarWarsEmpireAtWarGame(BasicGame):
    Name = "STAR WARS Empire at War"
    Author = "erri120"
    Version = "1.0.0"

    GameName = "STAR WARS™ Empire at War"
    GameShortName = "starwarsempireatwar"
    GameNexusName = "starwarsempireatwar"
    GameNexusId = 453
    GameSteamId = 32470
    GameGogId = 1421404887
    # using StarWarsG.exe instead of sweaw.exe because it has an icon
    GameBinary = "GameData/StarWarsG.exe"
    GameDataPath = "GameData/Data"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Star-Wars:-Empire-At-War"
    )

    def executables(self) -> list[mobase.ExecutableInfo]:
        return [
            mobase.ExecutableInfo(
                "STAR WARS Empire at War",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            )
        ]
