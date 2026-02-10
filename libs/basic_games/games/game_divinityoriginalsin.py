import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_game import BasicGame


class DivinityOriginalSinGame(BasicGame):
    Name = "Divinity: Original Sin (Classic) Support Plugin"
    Author = "LostDragonist"
    Version = "1.0.0"

    GameName = "Divinity: Original Sin (Classic)"
    GameShortName = "divinityoriginalsin"
    GameNexusName = "divinityoriginalsin"
    GameValidShortNames = ["divinityoriginalsin"]
    GameNexusId = 573
    GameSteamId = [230230]
    GameBinary = "Shipping/EoCApp.exe"
    GameDataPath = "Data"
    GameSaveExtension = "lsv"  # Not confirmed
    GameDocumentsDirectory = (
        "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin"
    )
    GameSavesDirectory = (
        "%USERPROFILE%/Documents/Larian Studios/Divinity Original Sin/PlayerProfiles"
    )
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Divinity:-Original-Sin"
    )

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(
            BasicGameSaveGameInfo(
                lambda s: s.with_suffix(".png")  # Not confirmed
            )
        )
        return True
