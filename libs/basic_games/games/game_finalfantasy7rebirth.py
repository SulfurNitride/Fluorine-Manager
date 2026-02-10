import math
import random
from pathlib import Path
from typing import Iterable, List

import mobase

from ..basic_features.basic_mod_data_checker import BasicModDataChecker, GlobPatterns
from ..basic_game import BasicGame


class FinalFantasy7RebirthDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                move={
                    "*.pak": "End/Content/Paks/~Mods/",
                    "*.ucas": "End/Content/Paks/~Mods/",
                    "*.utoc": "End/Content/Paks/~Mods/",
                    "*.dll": "End/Binaries/Win64/",
                },
                valid=[
                    "End/Content/Paks/~Mods",
                    "End/Binaries/Win64",
                    "End",
                    "/",
                    "_ROOT",
                    "_ROOT/End/Binaries/Win64",
                    "_ROOT/End/Content/Paks/~Mods",
                ],
            )
        )


class FinalFantasy7RebirthFileManager:
    _mods_extensions = [".pak", ".ucas", ".utoc"]
    _file_extensions = [".dll"]

    def __init__(
        self,
        mod_path: Path,
        file_path: Path,
        organizer: mobase.IOrganizer,
        load_enabled: mobase.MoVariant,
    ) -> None:
        self._mod_path = mod_path
        self._file_path = file_path
        self._organizer = organizer
        self._load_enabled = load_enabled

    def _active_mods(self) -> Iterable[Path]:
        mods_path = Path(self._organizer.modsPath())
        mod_list = self._organizer.modList()
        mod_load_order = mod_list.allModsByProfilePriority()
        for mod in mod_load_order:
            if mod_list.state(mod) & mobase.ModState.ACTIVE:
                yield mods_path / mod

    def _recursive(self, child: Path, mod_prefix: str) -> Iterable[mobase.Mapping]:
        if child.suffix.lower() in self._file_extensions:
            dest_mod_path = self._file_path / child.name
            yield mobase.Mapping(str(child), str(dest_mod_path), child.is_dir(), True)

        if child.suffix.lower() in self._mods_extensions:
            child_name = (
                child.with_stem(mod_prefix + child.stem).name
                if self._load_enabled
                else child.name
            )
            dest_mod_path = self._mod_path / child_name
            yield mobase.Mapping(str(child), str(dest_mod_path), child.is_dir(), True)

        if child.is_dir():
            for inner in child.iterdir():
                yield from self._recursive(inner, mod_prefix)

    def mod_mapping(self) -> Iterable[mobase.Mapping]:
        priority_digits = math.floor(math.log10(random.randrange(1, 999))) + 1
        for priority, mod_path in enumerate(self._active_mods()):
            mod_prefix = str(priority).zfill(priority_digits) + "_"
            for child in mod_path.iterdir():
                yield from self._recursive(child, mod_prefix)


class FinalFantasy7RebirthGame(BasicGame, mobase.IPluginFileMapper):
    Name = "Final Fasntasy 7 Rebirth Support Plugin"
    Author = "diegofesanto, TheUnlocked"
    Version = "0.0.1"

    GameName = "Final Fantasy VII Rebirth"
    GameShortName = "finalfantasy7rebirth"
    GameNexuName = "finalfantasy7rebirth"
    GameSteamId = 2909400
    GameBinary = "End/Binaries/Win64/ff7rebirth_.exe"
    GameDataPath = "_ROOT"
    GameSaveExtension = "sav"

    _forced_libraries = ["xinput1_3.dll"]

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)

    def init(self, organizer: mobase.IOrganizer):
        super().init(organizer)

        self._register_feature(FinalFantasy7RebirthDataChecker())

        return True

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "enforce_mod_load_order",
                ("Force mod load order by list priority"),
                True,
            )
        ]

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_name = self.gameName()
        game_dir = self.gameDirectory()
        bin_path = game_dir.absoluteFilePath(self.binaryName())
        return [
            # Default
            mobase.ExecutableInfo(
                f"{game_name}",
                bin_path,
            )
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        exe = Path(self.binaryName()).name
        return [
            mobase.ExecutableForcedLoadSetting(exe, lib).withEnabled(True)
            for lib in self._forced_libraries
        ]

    def _get_settings(self, key: str) -> mobase.MoVariant:
        return self._organizer.pluginSetting(self.name(), key)

    def mappings(self) -> List[mobase.Mapping]:
        mod_path = Path(self.gameDirectory().absolutePath()) / "End/Content/Paks/~Mods"
        file_path = Path(self.gameDirectory().absolutePath()) / "End/Binaries/Win64"
        load_enabled = self._get_settings("enforce_mod_load_order")
        file_manager = FinalFantasy7RebirthFileManager(
            mod_path, file_path, self._organizer, load_enabled
        )

        return [
            mobase.Mapping(
                self._organizer.overwritePath(),
                str(mod_path),
                True,
                True,
            )
        ] + list(file_manager.mod_mapping())
