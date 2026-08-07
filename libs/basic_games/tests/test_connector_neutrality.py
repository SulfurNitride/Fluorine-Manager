"""Regression test for BG3 connector-neutrality (plan step 12).

The BG3 Region B capture is served purely by the real relinks that
``WinePrefix::captureNode`` installs into the prefix.  Both VFS connectors
(FUSE and USVFS) consume the same mapping list from ``BG3Game.mappings()``,
so that list must never advertise a ``%LOCALAPPDATA%`` destination: if it
did, the USVFS launcher (which installs ``request.mappings`` verbatim) would
create a virtual map overlapping the captured Region B tree.  This test pins
down the invariant that every emitted mapping targets the game data dir and
the Region A ``bin``/``Data`` overlays only.

Runnable standalone (``python3 test_connector_neutrality.py``) without any
pytest wiring, mirroring the mock-``mobase`` harness used during plugin
development.
"""

import importlib
import sys
import tempfile
import types
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent
BASIC_GAMES_DIR = TESTS_DIR.parent


class _MockMobase:
    """Minimal stand-in for the mobase module, weak-capsule only."""

    class IPlugin:
        pass

    class IPluginGame(IPlugin):
        pass

    class IPluginFileMapper:
        pass

    class GameFeature:
        pass

    class ProfileDirectories(GameFeature):
        pass

    class LocalSavegames(GameFeature):
        pass

    class ModDataChecker:
        pass

    class ISaveGameInfoWidget:
        pass

    class SaveGameInfo:
        pass

    class ISaveGame:
        pass

    class IFileTree:
        pass

    class FileTreeEntry:
        pass

    class IProfile:
        pass

    class IOrganizer:
        pass

    class ModState:
        ACTIVE = 0x01

    class LoadOrderMechanism:
        PLUGINS_TXT = 0

    class ProfileSetting:
        CONFIGURATION = 0x01

    class PluginSetting:
        pass

    class ExecutableInfo:
        pass

    class ExecutableForcedLoadSetting:
        pass

    class VersionInfo:
        def __init__(self, version):
            self.version = version

    class Mapping:
        def __init__(self, source, dest, is_directory=False, is_create_target=None):
            self.source = source
            self.dest = dest
            self.is_directory = is_directory

    @staticmethod
    def getIconForExecutable(path):
        return None

    @staticmethod
    def getFileVersion(path):
        return "1.0.0"


def _install_mock_mobase() -> dict:
    mobase = types.ModuleType("mobase")
    registry = {}
    for name in dir(_MockMobase):
        if name.startswith("_"):
            continue
        value = getattr(_MockMobase, name)
        if isinstance(value, type) or callable(value):
            setattr(mobase, name, value)
            registry[name] = value
    sys.modules["mobase"] = mobase
    return registry


def _load_plugin():
    registry = _install_mock_mobase()

    basic_games = types.ModuleType("basic_games")
    basic_games.__path__ = [str(BASIC_GAMES_DIR)]
    sys.modules["basic_games"] = basic_games

    importlib.import_module("basic_games.basic_features")
    basic_game = importlib.import_module("basic_games.basic_game")

    games_pkg = types.ModuleType("basic_games.games")
    games_pkg.__path__ = [str(BASIC_GAMES_DIR / "games")]
    sys.modules["basic_games.games"] = games_pkg

    spec = importlib.util.spec_from_file_location(
        "basic_games.games.game_baldursgate3",
        BASIC_GAMES_DIR / "games" / "game_baldursgate3.py",
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)

    BasicGame = basic_game.BasicGame
    BasicGame.steam_games = {}
    BasicGame.gog_games = {}
    BasicGame.origin_games = {}
    BasicGame.epic_games = {}
    BasicGame.eadesktop_games = {}

    return module, registry, basic_game


def _build_game(module, mod_state, install: Path, mods_root: Path):
    organizer = types.SimpleNamespace(
        modsPath=lambda: str(mods_root),
        profilePath=lambda: str(Path(tempfile.mkdtemp())),
        pluginDataPath=lambda: str(Path(tempfile.mkdtemp())),
        modList=lambda: types.SimpleNamespace(
            allModsByProfilePriority=lambda: ["ModA"],
            state=lambda mod: mod_state.ACTIVE,
        ),
        gameFeatures=lambda: None,
    )
    game = module.BG3Game()
    game._organizer = organizer
    game.setGamePath(install)
    return game


def test_mappings_never_target_local_app_data():
    module, registry, _ = _load_plugin()
    mod_state = registry["ModState"]

    install = Path(tempfile.mkdtemp()) / "Baldurs Gate 3"
    (install / "bin").mkdir(parents=True)
    (install / "Data").mkdir()

    mods_root = Path(tempfile.mkdtemp())
    mod_a = mods_root / "ModA"
    (mod_a / "bin").mkdir(parents=True)
    (mod_a / "bin" / "native.dll").write_text("x")
    (mod_a / "Data").mkdir(parents=True)
    (mod_a / "Data" / "foo.pak").write_bytes(b"pak")
    (mod_a / "config.json").write_text("{}")

    game = _build_game(module, mod_state, install, mods_root)
    mappings = game.mappings()

    assert mappings, "expected bin/Data overlay mappings"
    data_dir = str(install).replace("\\", "/") + "/"
    for mapping in mappings:
        dest = str(mapping.dest).replace("\\", "/")
        assert dest.startswith(data_dir), f"mapping target outside data dir: {dest}"
        assert "AppData" not in dest and "Larian Studios" not in dest, dest
        assert "/Local" not in dest, dest
        assert dest.endswith("/bin") or dest.endswith("/Data"), dest


def main():
    test_mappings_never_target_local_app_data()
    print("PASS: mappings() never emits a %LOCALAPPDATA% (Region B) destination")


if __name__ == "__main__":
    main()