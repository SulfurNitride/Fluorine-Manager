# -*- encoding: utf-8 -*-

import configparser
import os

from .basic_game import BasicGame


class BasicIniGame(BasicGame):
    def __init__(self, path: str):
        # Set the _fromName to get more "correct" errors:
        self._fromName = os.path.basename(path)

        # Read the file:
        config = configparser.ConfigParser()
        config.optionxform = str  # type: ignore
        config.read(path)

        # Just fill the class with values:
        for k, v in config["DEFAULT"].items():
            setattr(self, k, v)

        super().__init__()
