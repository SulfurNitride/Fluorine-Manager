import os
import re

from PyQt6.QtCore import QDir

from ..basic_game import BasicGame


class MSFS2020Game(BasicGame):
    Name = "Microsoft Flight Simulator 2020 Support Plugin"
    Author = "Deorder"
    Version = "0.0.1"

    GameName = "Microsoft Flight Simulator 2020"
    GameShortName = "msfs2020"
    GameBinary = r"FlightSimulator.exe"
    GameSteamId = [1250410]
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Microsoft-Flight-Simulator-(2020)"
    )

    def dataDirectory(self) -> QDir:
        # Find and use package path specified in Asobo engine options
        AppDataPath = os.path.expandvars(r"%APPDATA%\Microsoft Flight Simulator")
        UserCfgPath = os.path.join(AppDataPath, "UserCfg.opt")
        InstalledPackagesPathPattern = re.compile(
            r'InstalledPackagesPath\s*=\s*"(.*)"', re.IGNORECASE
        )
        with open(UserCfgPath, newline="") as f:
            for _, line in enumerate(f):
                for match in re.finditer(InstalledPackagesPathPattern, line):
                    return QDir(os.path.join(match.group(), "Community"))
        return QDir(os.path.join(AppDataPath, "Packages", "Community"))
