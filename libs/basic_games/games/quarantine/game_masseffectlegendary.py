from ...basic_game import BasicGame


class MassEffectLegendaryGame(BasicGame):
    Name = "Mass Effect Legendary Edition Support Plugin"
    Author = "LostDragonist"
    Version = "1.0.0"

    GameName = "Mass Effect: Legendary Edition"
    GameShortName = "masseffectlegendaryedition"
    GameBinary = "Game/Launcher/MassEffectLauncher.exe"
    GameLauncher = "MassEffectLauncher.exe"
    GameDataPath = "%GAME_PATH%"
    GameDocumentsDirectory = (
        "%USERPROFILE%/Documents/BioWare/Mass Effect Legendary Edition/Save"
    )
    GameSaveExtension = "pcsav"
    GameSteamId = 1328670
    GameOriginWatcherExecutables = (
        "masseffectlauncher.exe",
        "masseffect1.exe",
        "masseffect2.exe",
        "masseffect3.exe",
    )
