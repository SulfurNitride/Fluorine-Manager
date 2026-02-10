from ..basic_game import BasicGame


class ReadyOrNotGame(BasicGame):
    Name = "Ready or Not Support Plugin"
    Author = "Ra2-IFV"
    Version = "0.0.0.1"

    GameName = "Ready or Not"
    GameShortName = "readyornot"
    GameNexusName = "readyornot"
    GameValidShortNames = ["ron"]
    # GameNexusId = "readyornot"
    GameBinary = "ReadyOrNot/Binaries/Win64/ReadyOrNot-Win64-Shipping.exe"
    GameLauncher = "ReadyOrNot.exe"
    GameDataPath = "ReadyOrNot/Content/Paks"
    GameDocumentsDirectory = "%USERPROFILE%/AppData/Local/ReadyOrNot"
    GameIniFiles = [
        "%GAME_DOCUMENTS%/Saved/Config/Windows/Game.ini",
        "%GAME_DOCUMENTS%/Saved/Config/Windows/GameUserSettings.ini",
    ]
    GameSavesDirectory = "%USERPROFILE%/AppData/Local/ReadyOrNot/Saved/SaveGames"
    GameSaveExtension = "sav"
    GameSteamId = 1144200
