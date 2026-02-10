# -*- encoding: utf-8 -*-

import configparser
import os
import sys
import xml.etree.ElementTree as et
from configparser import NoOptionError
from pathlib import Path
from typing import Dict


def find_games(errors: list[tuple[str, Exception]] | None = None) -> Dict[str, Path]:
    """
    Find the list of EA Desktop games installed.

    Returns:
        A mapping from EA Desktop content IDs to install locations for available
        EA Desktop games.
    """
    games: Dict[str, Path] = {}

    local_app_data_path = os.path.expandvars("%LocalAppData%")
    ea_desktop_settings_path = Path(local_app_data_path).joinpath(
        "Electronic Arts", "EA Desktop"
    )

    if not ea_desktop_settings_path.exists():
        return games

    try:
        user_ini, *_ = list(ea_desktop_settings_path.glob("user_*.ini"))
    except ValueError:
        return games

    # The INI file in its current form has no section headers.
    # So we wrangle the input to add it all under a fake section.
    with open(user_ini) as f:
        ini_content = "[mod_organizer]\n" + f.read()

    config = configparser.ConfigParser()
    try:
        config.read_string(ini_content)
    except configparser.ParsingError as e:
        error_message = (
            f'Failed to parse EA Desktop games list file "{user_ini}",\n'
            " Try to run the launcher to recreate it."
        )
        print(error_message, e, file=sys.stderr)
        if errors is not None:
            errors.append((error_message, e))
        return games

    try:
        install_path = Path(config.get("mod_organizer", "user.downloadinplacedir"))
    except NoOptionError:
        install_path = Path(os.environ["ProgramW6432"]) / "EA Games"
        config.set("mod_organizer", "user.downloadinplacedir", install_path.__str__())

    if not install_path.exists():
        return games

    for game_dir in install_path.iterdir():
        try:
            installer_file = game_dir.joinpath("__Installer", "installerdata.xml")
            xml_tree = et.parse(installer_file)
            root = xml_tree.getroot()

            # For all manifest files the following XPath expression returns the
            # numeric ID. There are, in some cases, also name IDs but we do not
            # consider these.
            content_id = root.find(".//contentIDs/contentID[1]")

            if content_id is not None and content_id.text:
                game_id = content_id.text
                games[game_id] = game_dir
        except FileNotFoundError:
            pass

    return games


if __name__ == "__main__":
    games = find_games()
    for k, v in games.items():
        print("Found game with id {} at {}.".format(k, v))
