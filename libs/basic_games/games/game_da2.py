import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_game import BasicGame


class DA2Game(BasicGame):
    Name = "Dragon Age 2 Support Plugin"
    Author = "Patchier"

    GameName = "Dragon Age 2"
    GameShortName = "dragonage2"
    GameBinary = r"bin_ship\DragonAge2.exe"
    GameDataPath = r"%DOCUMENTS%\BioWare\Dragon Age 2\packages\core\override"
    GameSavesDirectory = r"%DOCUMENTS%\BioWare\Dragon Age 2\Characters"
    GameSaveExtension = "das"
    GameSteamId = 1238040
    GameOriginManifestIds = ["OFB-EAST:59474", "DR:201797000"]
    GameOriginWatcherExecutables = ("DragonAge2.exe",)
    GameEaDesktopId = [70784, 1002980]
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Dragon-Age-II"
    )

    def version(self):
        # Don't forget to import mobase!
        return mobase.VersionInfo(1, 0, 1, mobase.ReleaseType.FINAL)

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)
        self._register_feature(
            BasicGameSaveGameInfo(lambda s: s.parent.joinpath("screen.dds"))
        )
        return True
