from PyQt6.QtCore import QFileInfo

import mobase

from ..basic_game import BasicGame


class StarWarsEmpireAtWarGame(BasicGame):
    Name = "STAR WARS Empire at War - Force of Corruption"
    Author = "erri120"
    Version = "1.0.0"

    GameName = "STAR WARS™ Empire at War: Forces of Corruption"
    GameShortName = "starwarsempireatwar"
    GameNexusName = "starwarsempireatwar"
    GameNexusId = 453
    GameSteamId = 32470
    GameGogId = 1421404887
    # using StarWarsG.exe instead of swfoc.exe because it has an icon
    GameBinary = "corruption/StarWarsG.exe"
    GameDataPath = "corruption/Data"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Star-Wars:-Empire-At-War"
    )

    def executables(self) -> list[mobase.ExecutableInfo]:
        return [
            mobase.ExecutableInfo(
                "STAR WARS Empire at War: Forces of Corruption",
                QFileInfo(self.gameDirectory().absoluteFilePath(self.binaryName())),
            )
        ]
