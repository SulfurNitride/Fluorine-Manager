from __future__ import annotations

from ..basic_features import GlobPatterns
from . import game_subnautica  # namespace to not load SubnauticaGame here, too!


class SubnauticaBelowZeroGame(game_subnautica.SubnauticaGame):
    Name = "Subnautica Below Zero Support Plugin"

    GameName = "Subnautica: Below Zero"
    GameShortName = "subnauticabelowzero"
    GameNexusName = "subnauticabelowzero"
    GameThunderstoreName = "subnautica-below-zero"
    GameSteamId = 848450
    GameEpicId = "foxglove"
    GameBinary = "SubnauticaZero.exe"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Subnautica:-Below-Zero"
    )

    _game_extra_save_paths = [
        r"%USERPROFILE%\Appdata\LocalLow\Unknown Worlds"
        r"\Subnautica Below Zero\SubnauticaZero\SavedGames"
    ]

    def _set_mod_data_checker(
        self, extra_patterns: GlobPatterns | None = None, use_qmod: bool | None = None
    ):
        super()._set_mod_data_checker(
            GlobPatterns(unfold=["BepInExPack_BelowZero"]), use_qmod
        )
