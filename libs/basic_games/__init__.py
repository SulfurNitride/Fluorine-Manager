# pyright: reportUnboundVariable=false

import glob
import importlib
import os
import pathlib
import site
import sys
import typing

from PyQt6.QtCore import qWarning

from mobase import IPlugin

from .basic_game import BasicGame
from .basic_game_ini import BasicIniGame

site.addsitedir(os.path.join(os.path.dirname(__file__), "lib"))


BasicGame.setup()


def createPlugins():
    # List of game class from python:
    game_plugins: typing.List[IPlugin] = []

    # We are going to list all game plugins:
    curpath = os.path.abspath(os.path.dirname(__file__))
    escaped_games_path = glob.escape(os.path.join(curpath, "games"))

    # List all the .ini files:
    for file in glob.glob(os.path.join(escaped_games_path, "*.ini")):
        game_plugins.append(BasicIniGame(file))

    # List all the python plugins:
    for file in glob.glob(os.path.join(escaped_games_path, "*.py")):
        module_p = os.path.relpath(file, os.path.join(curpath, "games"))
        if module_p == "__init__.py":
            continue

        if module_p == "game_stalkeranomaly.py":
            try:
                import lzokay  # type: ignore # noqa: F401
            except Exception:
                # Optional dependency not available on this host.
                continue

        # Import the module:
        try:
            module = importlib.import_module(".games." + module_p[:-3], __package__)
        except ImportError as e:
            print("Failed to import module {}: {}".format(module_p, e), file=sys.stderr)
            continue
        except Exception as e:
            print("Failed to import module {}: {}".format(module_p, e), file=sys.stderr)
            continue

        # Lookup game plugins:
        for name in dir(module):
            if hasattr(module, name):
                obj = getattr(module, name)
                if (
                    isinstance(obj, type)
                    and issubclass(obj, BasicGame)
                    and obj is not BasicGame
                ):
                    try:
                        game_plugins.append(obj())
                    except Exception as e:
                        print(
                            "Failed to instantiate {}: {}".format(name, e),
                            file=sys.stderr,
                        )
    for path in pathlib.Path(escaped_games_path).rglob("plugins/__init__.py"):
        module_path = "." + os.path.relpath(path.parent, curpath).replace(os.sep, ".")
        try:
            module = importlib.import_module(module_path, __package__)
            if hasattr(module, "createPlugins") and callable(module.createPlugins):
                try:
                    plugins: typing.Any = module.createPlugins()
                    for item in plugins:
                        if isinstance(item, IPlugin):
                            game_plugins.append(item)
                except TypeError:
                    pass
            if hasattr(module, "createPlugin") and callable(module.createPlugin):
                plugin = module.createPlugin()
                if isinstance(plugin, IPlugin):
                    game_plugins.append(plugin)
        except ImportError as e:
            qWarning(f"Error importing module {module_path}: {e}")
        except Exception as e:
            qWarning(f"Error calling function createPlugin(s) in {module_path}: {e}")

    return game_plugins
