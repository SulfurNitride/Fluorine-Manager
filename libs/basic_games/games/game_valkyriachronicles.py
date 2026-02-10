from ..basic_game import BasicGame


class ValkyriaChroniclesGame(BasicGame):
    Name = "Valkyria Chronicles Support Plugin"
    Author = "Ketsuban"
    Version = "1.0.0"

    GameName = "Valkyria Chronicles"
    GameShortName = "vc1"
    GameBinary = "Valkyria.exe"
    GameLauncher = "Launcher.exe"
    GameDataPath = "%GAME_PATH%"
    GameSavesDirectory = "%GAME_PATH%/savedata"
    GameSteamId = 294860
