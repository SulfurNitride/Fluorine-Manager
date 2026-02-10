# -*- encoding: utf-8 -*-

# Heavily influenced by https://github.com/erri120/GameFinder

import os
import threading
import time
from collections.abc import Sequence
from pathlib import Path
from urllib import parse

import psutil


class OriginWatcher:
    """
    This is a class to control killing Origin when needed. This is used in
    order to hook and unhook Origin to get around the Origin DRM. Support
    for launching Origin is not included as it's intended for the game's
    DRM to launch Origin as needed.
    """

    def __init__(self, executables: Sequence[str] = []):
        self.executables = list(map(lambda s: s.lower(), executables))

    def spawn_origin_watcher(self) -> bool:
        self.kill_origin()
        self.worker_alive = True
        self.worker = threading.Thread(target=self._workerFunc)
        self.worker.start()
        return True

    def stop_origin_watcher(self) -> None:
        self.worker_alive = False
        self.worker.join(10.0)

    def kill_origin(self) -> None:
        """
        Kills the Origin application
        """
        for proc in psutil.process_iter():
            if proc.name().lower() == "origin.exe":
                proc.kill()

    def _workerFunc(self) -> None:
        gameAliveCount = 300  # Large number to allow Origin and the game to launch
        while self.worker_alive:
            gameAlive = False
            # See if the game is still alive
            for proc in psutil.process_iter():
                if proc.name().lower() in self.executables:
                    gameAlive = True
                    break
            if gameAlive:
                # Game is alive, sleep and keep monitoring at faster pace
                gameAliveCount = 5
            else:
                gameAliveCount -= 1
                if gameAliveCount <= 0:
                    self.kill_origin()
                    self.worker_alive = False
            time.sleep(1)


def find_games() -> dict[str, Path]:
    """
    Find the list of Origin games installed.

    Returns:
        A mapping from Origin manifest IDs to install locations for available
        Origin games.
    """
    games: dict[str, Path] = {}

    program_data_path = os.path.expandvars("%PROGRAMDATA%")
    local_content_path = Path(program_data_path).joinpath("Origin", "LocalContent")
    for manifest in local_content_path.glob("**/*.mfst"):
        # Skip any manifest file with '@steam'
        if "@steam" in manifest.name.lower():
            continue

        # Read the file and look for &id= and &dipinstallpath=
        with open(manifest, "r") as f:
            manifest_query = f.read()
        url = parse.urlparse(manifest_query)
        query = parse.parse_qs(url.query)
        if "id" not in query:
            # If id is not present, we have no clue what to do.
            continue
        if "dipinstallpath" not in query:
            # We could query the Origin server for the install location but... no?
            continue

        for id_ in query["id"]:
            for path_ in query["dipinstallpath"]:
                games[id_] = Path(path_)

    return games


if __name__ == "__main__":
    games = find_games()
    for k, v in games.items():
        print("Found game with id {} at {}.".format(k, v))
