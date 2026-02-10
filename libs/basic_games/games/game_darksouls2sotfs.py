from ..basic_game import BasicGame


class DarkSouls2SotfsGame(BasicGame):
    Name = "DarkSouls2Sotfs"
    Author = "raehik"
    Version = "0.1.0"

    GameName = "Dark Souls II: Scholar of the First Sin"
    GameShortName = "darksouls2sotfs"
    GameNexusName = "darksouls2"
    GameNexusId = 482
    GameSteamId = 335300
    GameBinary = "Game/DarkSoulsII.exe"
    GameDataPath = "Game"
    GameDocumentsDirectory = "%USERPROFILE%/AppData/Roaming/DarkSoulsII"
    GameSavesDirectory = "%USERPROFILE%/AppData/Roaming/DarkSoulsII"
    GameSaveExtension = "sl2"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Dark-Souls-2-Sotfs"
    )
