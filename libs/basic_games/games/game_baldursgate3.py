import datetime
import difflib
import os
import shutil
from functools import cached_property
from pathlib import Path
from typing import Any

from PyQt6.QtCore import QLoggingCategory, qDebug, qInfo

import mobase

from ..basic_features import BasicGameSaveGameInfo, BasicLocalSavegames
from ..basic_game import BasicGame
from .baldursgate3 import bg3_file_mapper


class BG3Game(BasicGame, bg3_file_mapper.BG3FileMapper):
    Name = "Baldur's Gate 3 Plugin"
    Author = "daescha"
    Version = "0.1.0"
    GameName = "Baldur's Gate 3"
    GameShortName = "baldursgate3"
    GameNexusName = "baldursgate3"
    GameValidShortNames = ["bg3"]
    GameLauncher = "Launcher/LariLauncher.exe"
    GameBinary = "bin/bg3.exe"
    GameDataPath = ""
    GameDocumentsDirectory = (
        "%USERPROFILE%/AppData/Local/Larian Studios/Baldur's Gate 3"
    )
    GameSavesDirectory = "%GAME_DOCUMENTS%/PlayerProfiles/Public/Savegames/Story"
    GameSaveExtension = "lsv"
    GameNexusId = 3474
    GameSteamId = 1086940
    GameGogId = 1456460669

    def __init__(self):
        BasicGame.__init__(self)
        from .baldursgate3 import bg3_utils

        self.utils = bg3_utils.BG3Utils(self.name())
        bg3_file_mapper.BG3FileMapper.__init__(
            self, self.utils, self.documentsDirectory
        )

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self.utils.init(organizer)
        from .baldursgate3 import bg3_data_checker, bg3_data_content

        self._register_feature(bg3_data_checker.BG3ModDataChecker())
        self._register_feature(bg3_data_content.BG3DataContent())
        self._register_feature(BasicGameSaveGameInfo(lambda s: s.with_suffix(".webp")))
        self._register_feature(BasicLocalSavegames(self))
        organizer.onAboutToRun(self.utils.construct_modsettings_xml)
        organizer.onFinishedRun(self._on_finished_run)
        organizer.onUserInterfaceInitialized(self.utils.on_user_interface_initialized)
        organizer.modList().onModInstalled(self.utils.on_mod_installed)
        organizer.onPluginSettingChanged(self.utils.on_settings_changed)
        return True

    def settings(self):
        base_settings = super().settings()
        custom_settings = [
            mobase.PluginSetting(
                "force_load_dlls",
                "Force load all dlls detected in active mods. Removes the need for 'Native Mod Loader' and similar mods.",
                True,
            ),
            mobase.PluginSetting(
                "log_diff",
                "Log a diff of the modsettings.xml file before and after the game runs to check for differences.",
                False,
            ),
            mobase.PluginSetting(
                "delete_levelcache_folders_older_than_x_days",
                "Maximum number of days a file in overwrite/LevelCache is allowed to exist before being deleted "
                "after the executable finishes. Set to negative to disable.",
                3,
            ),
            mobase.PluginSetting(
                "autobuild_paks",
                "Autobuild folders likely to be PAK folders with every run of an executable.",
                True,
            ),
            mobase.PluginSetting(
                "remove_extracted_metadata",
                "Remove extracted meta.lsx files when they are no longer needed.",
                True,
            ),
            mobase.PluginSetting(
                "extract_full_package",
                "Extract the full pak when parsing metadata, instead of just meta.lsx.",
                False,
            ),
            mobase.PluginSetting(
                "convert_yamls_to_json",
                "Convert YAMLs to JSONs when executable runs. Allows one to configure ScriptExtender and related mods with YAML files.",
                False,
            ),
        ]
        for setting in custom_settings:
            setting.description = self.utils.tr(setting.description)
            base_settings.append(setting)
        return base_settings

    def executables(self) -> list[mobase.ExecutableInfo]:
        return [
            mobase.ExecutableInfo(
                f"{self.gameName()}: DX11",
                self.gameDirectory().absoluteFilePath("bin/bg3_dx11.exe"),
            ),
            mobase.ExecutableInfo(
                f"{self.gameName()}: Vulkan",
                self.gameDirectory().absoluteFilePath(self.binaryName()),
            ),
            mobase.ExecutableInfo(
                "Larian Launcher",
                self.gameDirectory().absoluteFilePath(self.getLauncherName()),
            ),
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        try:
            efls = super().executableForcedLoads()
        except AttributeError:
            efls = []
        if self.utils.force_load_dlls:
            qInfo("detecting dlls in enabled mods")
            libs: set[str] = set()
            tree: mobase.IFileTree | mobase.FileTreeEntry | None = (
                self._organizer.virtualFileTree().find("bin")
            )
            if type(tree) is not mobase.IFileTree:
                return efls

            def find_dlls(
                _: Any, entry: mobase.FileTreeEntry
            ) -> mobase.IFileTree.WalkReturn:
                relpath = entry.pathFrom(tree)
                if (
                    relpath
                    and entry.hasSuffix("dll")
                    and relpath not in self._base_dlls
                ):
                    libs.add(relpath)
                return mobase.IFileTree.WalkReturn.CONTINUE

            tree.walk(find_dlls)
            exes = self.executables()
            qDebug(f"dlls to force load: {libs}")
            efls = efls + [
                mobase.ExecutableForcedLoadSetting(
                    exe.binary().fileName(), lib
                ).withEnabled(True)
                for lib in libs
                for exe in exes
            ]
        return efls

    def loadOrderMechanism(self) -> mobase.LoadOrderMechanism:
        return mobase.LoadOrderMechanism.PLUGINS_TXT

    @cached_property
    def _base_dlls(self) -> set[str]:
        base_bin = Path(self.gameDirectory().absoluteFilePath("bin"))
        return {str(f.relative_to(base_bin)) for f in base_bin.glob("*.dll")}

    def _on_finished_run(self, exec_path: str, exit_code: int):
        if "bin/bg3" not in exec_path:
            return
        cat = QLoggingCategory.defaultCategory()
        self.utils.log_dir.mkdir(parents=True, exist_ok=True)
        if (
            cat is not None
            and cat.isDebugEnabled()
            and self.utils.log_diff
            and self.utils.modsettings_backup.exists()
            and self.utils.modsettings_path.exists()
        ):
            for x in difflib.unified_diff(
                self.utils.modsettings_backup.open().readlines(),
                self.utils.modsettings_path.open().readlines(),
                fromfile=str(self.utils.modsettings_backup),
                tofile=str(self.utils.modsettings_path),
                lineterm="",
            ):
                qDebug(x)
        moved: dict[str, str] = {}
        for path in self.utils.overwrite_path.rglob("*.log"):
            try:
                moved[str(path.relative_to(Path.home()))] = str(
                    (self.utils.log_dir / path.name).relative_to(Path.home())
                )
                path.replace(self.utils.log_dir / path.name)
            except PermissionError as e:
                qDebug(str(e))
        for path in self.utils.overwrite_path.rglob("*log.txt"):
            dest = self.utils.log_dir / path.name
            if path.name == "log.txt":
                dest = self.utils.log_dir / f"{path.parent.name}-{path.name}"
            try:
                moved[str(path.relative_to(Path.home()))] = str(
                    dest.relative_to(Path.home())
                )
                path.replace(dest)
            except PermissionError as e:
                qDebug(str(e))
        if cat is not None and cat.isDebugEnabled() and len(moved) > 0:
            qDebug(f"moved log files to logs dir: {moved}")
        days = self.utils.get_setting("delete_levelcache_folders_older_than_x_days")
        if type(days) is int and days >= 0:
            cutoff_time = datetime.datetime.now() - datetime.timedelta(days=days)
            qDebug(f"cleaning folders in overwrite/LevelCache older than {cutoff_time}")
            removed: set[Path] = set()
            for path in self.utils.overwrite_path.glob("LevelCache/*"):
                if (
                    datetime.datetime.fromtimestamp(os.path.getmtime(path))
                    < cutoff_time
                ):
                    shutil.rmtree(path, ignore_errors=True)
                    removed.add(path)
            if cat is not None and cat.isDebugEnabled() and len(removed) > 0:
                qDebug(
                    f"cleaned the following folders due to them being older than {cutoff_time}: {removed}"
                )
        for fdir in {self.utils.overwrite_path, self.doc_path}:
            removed: set[Path] = set()
            for folder in sorted(list(fdir.walk(top_down=False)))[:-1]:
                try:
                    folder[0].rmdir()
                    removed.add(folder[0].relative_to(Path.home()))
                except OSError:
                    pass
            if cat is not None and cat.isDebugEnabled() and len(removed) > 0:
                qDebug(
                    f"cleaned empty dirs from {fdir.relative_to(Path.home())} {removed}"
                )
