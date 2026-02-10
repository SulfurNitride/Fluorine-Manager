# Code adapted from EzioTheDeadPoet / erri120:
#     https://github.com/ModOrganizer2/modorganizer-basic_games/pull/5

import winreg
from pathlib import Path


def find_games() -> dict[str, Path]:
    # List the game IDs from the registry:
    game_ids: list[str] = []
    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE, r"Software\Wow6432Node\GOG.com\Games"
        ) as key:
            nkeys = winreg.QueryInfoKey(key)[0]
            for ik in range(nkeys):
                game_key = winreg.EnumKey(key, ik)
                if game_key.isdigit():
                    game_ids.append(game_key)
    except FileNotFoundError:
        return {}

    # For each game, query the path:
    games: dict[str, Path] = {}
    for game_id in game_ids:
        try:
            with winreg.OpenKey(
                winreg.HKEY_LOCAL_MACHINE,
                f"Software\\Wow6432Node\\GOG.com\\Games\\{game_id}",
            ) as key:
                games[game_id] = Path(winreg.QueryValueEx(key, "path")[0])
        except FileNotFoundError:
            pass

    return games
