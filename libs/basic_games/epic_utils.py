# -*- encoding: utf-8 -*-
from __future__ import annotations

import itertools
import json
import os
import sys
from collections.abc import Iterable
from pathlib import Path

try:
    import winreg
except ImportError:
    winreg = None

ErrorList = list[tuple[str, Exception]]


def find_epic_games(
    errors: ErrorList | None = None,
) -> Iterable[tuple[str, Path]]:
    if winreg is None:
        return

    try:
        with winreg.OpenKey(
            winreg.HKEY_LOCAL_MACHINE,
            r"Software\Wow6432Node\Epic Games\EpicGamesLauncher",
        ) as key:
            epic_data_path, _ = winreg.QueryValueEx(key, "AppDataPath")
    except FileNotFoundError:
        epic_data_path = r"%ProgramData%\Epic\EpicGamesLauncher\Data"

    manifests_path = Path(os.path.expandvars(epic_data_path)).joinpath("Manifests")
    if manifests_path.exists():
        for manifest_file_path in manifests_path.glob("*.item"):
            try:
                with open(manifest_file_path, encoding="utf-8") as manifest_file:
                    manifest_file_data = json.load(manifest_file)
                yield (
                    manifest_file_data["AppName"],
                    Path(manifest_file_data["InstallLocation"]),
                )
            except (json.JSONDecodeError, KeyError) as e:
                error_message = (
                    f'Unable to parse Epic Games manifest file: "{manifest_file_path}"\n'
                    " Try to run the launcher recreate it."
                )
                print(
                    error_message,
                    e,
                    file=sys.stderr,
                )
                if errors is not None:
                    errors.append((error_message, e))


def find_legendary_games(
    config_path: str | None = None, errors: ErrorList | None = None
) -> Iterable[tuple[str, Path]]:
    # Based on legendary source:
    # https://github.com/derrod/legendary/blob/master/legendary/lfs/lgndry.py
    if config_path := config_path or os.environ.get("XDG_CONFIG_HOME"):
        legendary_config_path = Path(config_path, "legendary")
    else:
        legendary_config_path = Path("~/.config/legendary").expanduser()

    installed_path = legendary_config_path / "installed.json"
    if installed_path.exists():
        try:
            with open(installed_path, encoding="utf-8") as installed_file:
                installed_games = json.load(installed_file)
            for game in installed_games.values():
                yield game["app_name"], Path(game["install_path"])
        except (json.JSONDecodeError, AttributeError, KeyError) as e:
            error_message = (
                f'Unable to parse installed games from Legendary/Heroic launcher: "{installed_path}"\n'
                " Try to run the launcher to recrated the file."
            )
            print(
                error_message,
                e,
                file=sys.stderr,
            )
            if errors is not None:
                errors.append((error_message, e))


def find_heroic_games(errors: ErrorList | None = None):
    return find_legendary_games(
        os.path.expandvars(r"%AppData%\heroic\legendaryConfig"), errors
    )


def find_games(errors: ErrorList | None = None) -> dict[str, Path]:
    return dict(
        itertools.chain(
            find_epic_games(errors=errors),
            find_legendary_games(errors=errors),
            find_heroic_games(errors=errors),
        )
    )


if __name__ == "__main__":
    games = find_games()
    for k, v in games.items():
        print("Found game with id {} at {}.".format(k, v))
