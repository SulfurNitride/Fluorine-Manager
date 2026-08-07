"""Automated counterpart of the step 13 manual test checklist.

Covers the automatable checklist items against the real plugin code, using the
same mock-``mobase`` harness as the step 10-12 tests plus a fake prefix tree:

3. Second launch with no changes: ``modsettings.lsx`` mtime preserved.
4. Toggle a ``.pak`` mod (set change) / re-order two (order change): both
   regenerate and produce a ``.previous`` backup.
5. Vulkan runner writes both ``preferences.json`` keys into the captured tree.
8. Connector neutrality: Region B never appears in ``mappings()`` (Region A
   only); the materialized Region B links live under the (fake) prefix.

Items 1, 2 and 6 (capture relink, fresh-prefix seeding, profile switch) are
covered by the C++ suite in ``src/tests/test_capture_node.cpp``; item 7 (the
checkbox) is a UI property verified in the step 12 research.

Also covers step 14: one-time migration of legacy ``<profile>/saves`` content
into the captured save tree (positive move, and no-op for absent/empty source
or an already-populated target).

Runnable standalone (``python3 test_checklist.py``) without pytest wiring.
"""

import importlib
import json
import os
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


def _install_fake_larian_formats():
    """Fake larian-formats-0.8.1 API used by _ordered_pak_ids/_build_modsettings."""
    meta = {
        "Folder": "ModFolder",
        "MD5": "abc",
        "Name": "ModName",
        "PublishHandle": "123",
        "UUID": "11111111-2222-3333-4444-555555555555",
        "Version64": "36028797018963968",
    }
    mod = types.ModuleType("larian_formats")

    def get_metadata_for_file(path):
        # Vary the metadata per file so the generated XML differs by order,
        # not just by set membership.
        stem = str(path).rpartition("/")[2].partition(".")[0]
        m = dict(meta)
        m["Name"] = m["Folder"] = stem
        m["UUID"] = f"00000000-0000-0000-0000-{int(hash(stem)):012d}"
        return m

    mod.get_metadata_for_file = get_metadata_for_file
    mod.is_override = lambda path: False
    sys.modules["larian_formats"] = mod


def _load_plugin():
    registry = _install_mock_mobase()
    _install_fake_larian_formats()

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

    return module, registry


class _Fixture:
    def __init__(self, module, registry):
        self.module = module
        self.mod_state = registry["ModState"]
        self.install = Path(tempfile.mkdtemp()) / "Baldurs Gate 3"
        (self.install / "bin").mkdir(parents=True)
        (self.install / "Data").mkdir()

        self.prefix_user = Path(tempfile.mkdtemp()) / "users" / "steamuser"
        (self.prefix_user / "AppData" / "Local").mkdir(parents=True)
        module._wine_user_profile = lambda: str(self.prefix_user)

        self.mods_root = Path(tempfile.mkdtemp())
        self.enabled = {}
        self.profile_path = Path(tempfile.mkdtemp())

    def add_mod(self, name: str, pak: bool = True) -> "Path":
        mod_dir = self.mods_root / name
        (mod_dir / "Data").mkdir(parents=True)
        (mod_dir / "Data" / "placeholder.txt").write_text("region A")
        if pak:
            (mod_dir / f"{name}.pak").write_bytes(b"pakdata")
        (mod_dir / "config.json").write_text("{}")
        self.enabled[name] = self.mod_state.ACTIVE
        return mod_dir

    def game(self):
        organizer = types.SimpleNamespace(
            modsPath=lambda: str(self.mods_root),
            profilePath=lambda: str(self.profile_path),
            pluginDataPath=lambda: str(self.profile_path),
            modList=lambda: types.SimpleNamespace(
                allModsByProfilePriority=lambda: list(self.enabled),
                state=lambda mod: self.enabled.get(mod, 0),
            ),
            gameFeatures=lambda: None,
        )
        game = self.module.BG3Game()
        game._organizer = organizer
        game.setGamePath(self.install)
        return game

    @property
    def local(self) -> Path:
        return self.prefix_user / "AppData" / "Local"

    @property
    def mods_dir(self) -> Path:
        return (
            self.local
            / "Larian Studios"
            / "Baldur's Gate 3"
            / "Mods"
        )

    @property
    def modsettings(self) -> Path:
        return (
            self.local
            / "Larian Studios"
            / "Baldur's Gate 3"
            / "PlayerProfiles"
            / "Public"
            / "modsettings.lsx"
        )

    @property
    def preferences(self) -> Path:
        return (
            self.local
            / "Larian Studios"
            / "Launcher"
            / "Settings"
            / "preferences.json"
        )

    @property
    def savegames(self) -> Path:
        return (
            self.local
            / "Larian Studios"
            / "Baldur's Gate 3"
            / "PlayerProfiles"
            / "Public"
            / "Savegames"
            / "Story"
        )


def test_vulkan_runner_writes_preferences(item5):
    module, _ = item5
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.preferences.exists(), "preferences.json not written"
    prefs = json.loads(module.preferences.read_text())
    assert prefs["DefaultRenderingBackend"] == 0
    assert prefs["OverrideRenderingBackend"] is True
    assert str(module.preferences.parent).startswith(str(module.local))


def test_modsettings_unchanged_launch_preserves_mtime(item3):
    module, _ = item3
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.modsettings.exists(), "modsettings.lsx not generated"
    first_mtime = module.modsettings.stat().st_mtime_ns
    second_mtime = module.modsettings.stat().st_mtime_ns
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.modsettings.stat().st_mtime_ns == second_mtime
    assert second_mtime == first_mtime, "mtime changed on no-change launch"


def test_toggle_and_reorder_regenerate_with_backup(item4):
    module, _ = item4
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.modsettings.exists()

    previous = module.modsettings.with_name("modsettings.lsx.previous")
    first = module.modsettings.read_text()

    module.enabled["ModC"] = module.mod_state.ACTIVE
    module.add_mod("ModC")
    module.enabled = {**module.enabled}
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))

    assert previous.exists(), "no .previous backup produced on set change"
    assert module.modsettings.read_text() != first, "modsettings not regenerated"
    regenerated = module.modsettings.read_text()
    assert "cb555efe-2d9e-131f-8195-a89329d218ea" in regenerated

    # Re-order: swap ModA and ModB priority (order change -> regenerate).
    prior_text = module.modsettings.read_text()
    keys = list(module.enabled)
    module.enabled = {k: module.enabled[k] for k in reversed(keys)}
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.modsettings.read_text() != prior_text, "order change not regenerated"


def test_materialized_links_live_under_captured_tree(item8):
    module, _ = item8
    game = module.game()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert module.mods_dir.exists()
    paks = list(module.mods_dir.iterdir())
    assert paks, "no .pak materialized into Region B Mods dir"
    for pak in paks:
        assert pak.name.endswith(".pak")
        target = pak.resolve()
        assert str(target).startswith(str(module.mods_root)), (
            f"materialized link escapes mods root: {target}"
        )

    # Connector neutrality: mappings() must never mention Region B.
    for mapping in game.mappings():
        dest = str(mapping.dest).replace("\\", "/")
        assert dest.startswith(str(module.install).replace("\\", "/") + "/")
        assert "AppData" not in dest and "Larian Studios" not in dest
        assert dest.endswith("/bin") or dest.endswith("/Data")


def test_migrate_legacy_saves(item14):
    module, _ = item14
    game = module.game()
    legacy = module.profile_path / "saves"
    legacy.mkdir()
    (legacy / "Quicksave.lsv").write_bytes(b"save1")
    (legacy / "autosave_tmp.lsv").write_bytes(b"save2")
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    saved = {p.name: p for p in module.savegames.iterdir()}
    assert "Quicksave.lsv" in saved and "autosave_tmp.lsv" in saved
    assert not legacy.exists(), "legacy <profile>/saves not removed after move"

    # Second launch: already migrated -> no-op (idempotent).
    game2 = module.game()
    game2._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert not legacy.exists()
    assert sorted(p.name for p in module.savegames.iterdir()) == sorted(saved)


def test_migrate_legacy_saves_noop_when_nothing_to_move(item14):
    module, _ = item14
    game = module.game()

    # Absent legacy dir.
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert not module.profile_path.exists() or not (
        module.profile_path / "saves"
    ).exists()

    # Empty legacy dir: nothing to move -> no-op, dir left as is.
    legacy = module.profile_path / "saves"
    legacy.mkdir()
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert legacy.is_dir() and not any(legacy.iterdir())


def test_migrate_legacy_saves_skips_populated_target(item14):
    module, _ = item14
    game = module.game()

    # Target already has saves -> legacy content left untouched.
    target = module.savegames
    target.mkdir(parents=True)
    (target / "NewSave.lsv").write_bytes(b"new")
    legacy = module.profile_path / "saves"
    legacy.mkdir()
    (legacy / "Quicksave.lsv").write_bytes(b"save1")
    game._on_about_to_run(str(module.install / "bin" / "bg3.exe"))
    assert (legacy / "Quicksave.lsv").read_bytes() == b"save1"
    assert list(target.iterdir()) == [target / "NewSave.lsv"]


def main():
    module, registry = _load_plugin()
    fx = _Fixture(module, registry)
    fx.add_mod("ModA")
    fx.add_mod("ModB")

    test_vulkan_runner_writes_preferences((fx, registry))
    print("PASS: item 5 - Vulkan runner writes both preferences.json keys")

    fx2 = _Fixture(module, registry)
    fx2.add_mod("ModA")
    fx2.add_mod("ModB")
    test_modsettings_unchanged_launch_preserves_mtime((fx2, registry))
    print("PASS: item 3 - no-change launch preserves modsettings.lsx mtime")

    fx3 = _Fixture(module, registry)
    fx3.add_mod("ModA")
    fx3.add_mod("ModB")
    test_toggle_and_reorder_regenerate_with_backup((fx3, registry))
    print("PASS: item 4 - toggle/re-order regenerate modsettings with .previous backup")

    fx4 = _Fixture(module, registry)
    fx4.add_mod("ModA")
    fx4.add_mod("ModB")
    test_materialized_links_live_under_captured_tree((fx4, registry))
    print("PASS: item 8 - Region B materialized under capture; mappings() Region A only")

    fx5 = _Fixture(module, registry)
    fx5.add_mod("ModA")
    fx5.add_mod("ModB")
    test_migrate_legacy_saves((fx5, registry))
    print("PASS: step 14 - legacy <profile>/saves moved into captured tree")

    fx6 = _Fixture(module, registry)
    fx6.add_mod("ModA")
    test_migrate_legacy_saves_noop_when_nothing_to_move((fx6, registry))
    print("PASS: step 14 - absent/empty legacy saves is a no-op")

    fx7 = _Fixture(module, registry)
    fx7.add_mod("ModA")
    test_migrate_legacy_saves_skips_populated_target((fx7, registry))
    print("PASS: step 14 - populated captured target is left untouched")


if __name__ == "__main__":
    main()