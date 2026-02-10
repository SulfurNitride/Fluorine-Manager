import filecmp
import json
import re
import shutil
import tempfile
import textwrap
from collections import Counter
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Literal, TypeVar

from PyQt6.QtCore import QDateTime, QDir, Qt, qCritical, qInfo, qWarning
from PyQt6.QtWidgets import (
    QCheckBox,
    QMainWindow,
    QMessageBox,
    QProgressDialog,
    QWidget,
)

import mobase

from ..basic_features import BasicLocalSavegames, BasicModDataChecker, GlobPatterns
from ..basic_features.basic_save_game_info import (
    BasicGameSaveGame,
    BasicGameSaveGameInfo,
    format_date,
)
from ..basic_game import BasicGame


class CyberpunkModDataChecker(BasicModDataChecker):
    def __init__(self):
        super().__init__(
            GlobPatterns(
                delete=[
                    "*.gif",
                    "*.jpg",
                    "*.jpeg",
                    "*.jxl",
                    "*.md",
                    "*.png",
                    "*.txt",
                    "*.webp",
                ],
                move={
                    # archive and ArchiveXL
                    "*.archive": "archive/pc/mod/",
                    "*.xl": "archive/pc/mod/",
                },
                valid=[
                    "archive",
                    # redscript
                    "engine",
                    "r6",
                    "mods",  # RedMod
                    "red4ext",
                    "bin",  # CET etc. gets handled below
                ],
            )
        )


def time_from_seconds(s: int | float) -> str:
    m, s = divmod(int(s), 60)
    h, m = divmod(int(m), 60)
    return f"{h:02}:{m:02}:{s:02}"


def parse_cyberpunk_save_metadata(save_path: Path, save: mobase.ISaveGame):
    metadata_file = save_path / "metadata.9.json"
    try:
        with open(metadata_file) as file:
            meta_data = json.load(file)["Data"]["metadata"]
            name = meta_data["name"]
            if name != (save_name := save.getName()):
                name = f"{save_name}  ({name})"
            return {
                "Name": name,
                "Date": format_date(meta_data["timestampString"], "hh:mm:ss, d.M.yyyy"),
                "Play Time": time_from_seconds(meta_data["playthroughTime"]),
                "Quest": meta_data["trackedQuestEntry"],
                "Level": int(meta_data["level"]),
                "Street Cred": int(meta_data["streetCred"]),
                "Life Path": meta_data["lifePath"],
                "Difficulty": meta_data["difficulty"],
                "Gender": f"{meta_data['bodyGender']} / {meta_data['brainGender']}",
                "Game version": meta_data["buildPatch"],
            }
    except (FileNotFoundError, json.JSONDecodeError):
        return None


class CyberpunkSaveGame(BasicGameSaveGame):
    _name_file = "NamedSave.txt"  # from mod: Named Saves

    def __init__(self, filepath: Path):
        super().__init__(filepath)
        try:  # Custom name from Named Saves
            with open(filepath / self._name_file) as file:
                self._name = file.readline()
        except FileNotFoundError:
            self._name = ""

    def getName(self) -> str:
        return self._name or super().getName()

    def getCreationTime(self) -> QDateTime:
        return QDateTime.fromSecsSinceEpoch(
            int((self._filepath / "sav.dat").stat().st_mtime)
        )


@dataclass
class ModListFile:
    list_path: Path
    mod_search_pattern: str
    reversed_priority: bool = False
    """True: load order priority is reversed compared to MO (first mod has priority)."""


_MOD_TYPE = TypeVar("_MOD_TYPE")


class ModListFileManager(dict[_MOD_TYPE, ModListFile]):
    """Manages modlist files for specific mod types."""

    def __init__(self, organizer: mobase.IOrganizer, **kwargs: ModListFile) -> None:
        self._organizer = organizer
        super().__init__(**kwargs)

    def update_modlist(
        self, mod_type: _MOD_TYPE, mod_files: list[str] | None = None
    ) -> tuple[Path, list[str], list[str]]:
        """
        Updates the mod list file for `mod_type` with the current load order.
        Removes the file if it is not needed.

        Args:
            mod_type: Which modlist to update.
            mod_files (optional): By default mod files in order of `mod_type` priority
                (respecting `self[mod_type].reversed_priority`).

        Returns:
            `(modlist_path, new_mod_list, old_mod_list)`
        """
        if mod_files is None:
            mod_files = list(self.modfile_names(mod_type))
        modlist_path = self.absolute_modlist_path(mod_type)
        old_modlist = (
            modlist_path.read_text().splitlines() if modlist_path.exists() else []
        )
        if not mod_files or len(mod_files) == 1:
            # No load order required
            if old_modlist:
                qInfo(f"Removing {mod_type} load order {modlist_path}")
                modlist_path.unlink()
            return modlist_path, [], old_modlist
        else:
            qInfo(f'Updating {mod_type} load order "{modlist_path}" with: {mod_files}')
            modlist_path.parent.mkdir(parents=True, exist_ok=True)
            modlist_path.write_text("\n".join(mod_files))
            return modlist_path, mod_files, old_modlist

    def absolute_modlist_path(self, mod_type: _MOD_TYPE) -> Path:
        modlist_path = self[mod_type].list_path
        if not modlist_path.is_absolute():
            existing = self._organizer.findFiles(modlist_path.parent, modlist_path.name)
            overwrite = self._organizer.overwritePath()
            modlist_path = (
                Path(existing[0]) if (existing) else Path(overwrite, modlist_path)
            )
        return modlist_path

    def modfile_names(self, mod_type: _MOD_TYPE) -> Iterable[str]:
        """Get all file names from the `mod_type` in MOs load order
        (reversed with `self[mod_type].reversed_priority = True`).
        """
        yield from (file.name for file in self.modfiles(mod_type))

    def modfiles(self, mod_type: _MOD_TYPE) -> Iterable[Path]:
        """Get all files from the `mod_type` in MOs load order
        (reversed with `self[mod_type].reversed_priority = True`).
        """
        mod_search_pattern = self[mod_type].mod_search_pattern
        for mod_path in self.active_mod_paths(self[mod_type].reversed_priority):
            yield from mod_path.glob(mod_search_pattern)

    def active_mod_paths(self, reverse: bool = False) -> Iterable[Path]:
        """Yield the path to active mods in MOs load order."""
        mods_path = Path(self._organizer.modsPath())
        modlist = self._organizer.modList()
        mods_load_order = modlist.allModsByProfilePriority()
        for mod in reversed(mods_load_order) if reverse else mods_load_order:
            if modlist.state(mod) & mobase.ModState.ACTIVE:
                yield mods_path / mod


class Cyberpunk2077Game(BasicGame):
    Name = "Cyberpunk 2077 Support Plugin"
    Author = "6788, Zash"
    Version = "3.0.1"

    GameName = "Cyberpunk 2077"
    GameShortName = "cyberpunk2077"
    GameBinary = "bin/x64/Cyberpunk2077.exe"
    GameLauncher = "REDprelauncher.exe"
    GameDataPath = "%GAME_PATH%"
    GameDocumentsDirectory = "%USERPROFILE%/AppData/Local/CD Projekt Red/Cyberpunk 2077"
    GameSavesDirectory = "%USERPROFILE%/Saved Games/CD Projekt Red/Cyberpunk 2077"
    GameSaveExtension = "dat"
    GameSteamId = 1091500
    GameGogId = 1423049311
    GameEpicId = "77f2b98e2cef40c8a7437518bf420e47"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Cyberpunk-2077"
    )

    # CET and RED4ext, relative to Cyberpunk2077.exe
    _forced_libraries = ["version.dll", "winmm.dll"]
    _crashreporter_path = "bin/x64/CrashReporter/CrashReporter.exe"

    _redmod_binary = Path("tools/redmod/bin/redMod.exe")
    _redmod_log = Path("tools/redmod/bin/REDmodLog.txt")
    _redmod_deploy_path = Path("r6/cache/modded/")
    _redmod_deploy_args = "deploy -reportProgress"
    """Deploy arguments for `redmod.exe`, -modlist=... is added."""

    _parentWidget: QWidget
    """Set with `_organizer.onUserInterfaceInitialized()`"""

    def init(self, organizer: mobase.IOrganizer) -> bool:
        super().init(organizer)
        self._register_feature(BasicLocalSavegames(self))
        self._register_feature(
            BasicGameSaveGameInfo(
                lambda p: Path(p or "", "screenshot.png"),
                parse_cyberpunk_save_metadata,
            )
        )
        self._register_feature(CyberpunkModDataChecker())

        self._modlist_files = ModListFileManager[Literal["archive", "redmod"]](
            organizer,
            archive=ModListFile(
                Path("archive/pc/mod/modlist.txt"),
                "archive/pc/mod/*.archive",
                reversed_priority=bool(self._get_setting("reverse_archive_load_order")),
            ),
            redmod=ModListFile(
                Path(self._redmod_deploy_path, "MO_REDmod_load_order.txt"),
                "mods/*/",
                reversed_priority=bool(self._get_setting("reverse_redmod_load_order")),
            ),
        )
        organizer.onAboutToRun(self._onAboutToRun)
        organizer.onFinishedRun(self._onFinishedRun)
        organizer.onPluginSettingChanged(self._on_settings_changed)
        organizer.modList().onModInstalled(self._check_disable_crashreporter)
        organizer.onUserInterfaceInitialized(self._on_user_interface_initialized)
        return True

    def _on_settings_changed(
        self,
        plugin_name: str,
        setting: str,
        old: mobase.MoVariant,
        new: mobase.MoVariant,
    ):
        if self.name() != plugin_name:
            return
        match setting:
            case "reverse_archive_load_order":
                self._modlist_files["archive"].reversed_priority = bool(new)
            case "reverse_remod_load_order":
                self._modlist_files["redmod"].reversed_priority = bool(new)
            case "show_rootbuilder_conversion":
                if new and (dialog := self._get_rootbuilder_conversion_dialog()):
                    dialog.open()  # type: ignore
            case _:
                return

    def _on_user_interface_initialized(self, window: QMainWindow):
        self._parentWidget = window
        if not self.isActive():
            return
        if dialog := self._get_rootbuilder_conversion_dialog(window):
            dialog.open()  # type: ignore
        else:
            self._check_disable_crashreporter()

    def _check_disable_crashreporter(
        self, mod: mobase.IModInterface | None | Any = None
    ):
        """
        Disable Crashreporter with CET installed in VFS, since it crashes
        when trying to resolve `version.dll`.
        """
        if not self.isActive() or not self._get_setting("disable_crashreporter"):
            return
        cet_mod_name = self._find_cet_mod_name(
            mod if isinstance(mod, mobase.IModInterface) else None
        )
        if (
            cet_mod_name
            and (cr_origin := self._organizer.getFileOrigins(self._crashreporter_path))
            and cr_origin[0] == "data"
        ):
            self._create_dummy_crashreporter_mod(
                self._organizer.modList().priority(cet_mod_name) + 1
            )

    def _find_cet_mod_name(self, mod: mobase.IModInterface | None = None) -> str:
        """
        Find the mod containing `version.dll`.

        Args:
            mod (optional): check the mods filetree instead. Defaults to None.

        Returns:
            The mods name if `version.dll` is found, else ''.
        """
        cet_mod_name = ""
        if mod:
            if mod.fileTree().find("bin/x64/version.dll"):
                cet_mod_name = mod.name()
        elif dll_origins := self._organizer.getFileOrigins("bin/x64/version.dll"):
            cet_mod_name = dll_origins[0]
        return cet_mod_name if cet_mod_name != "data" else ""

    def _create_dummy_crashreporter_mod(self, priority: int = 0):
        """
        Disables CrashReporter by creating an empty dummy file to replace
        `CrashReporter.exe`.
        """
        mod_name = "disable CrashReporter (MO CET fix)"
        modlist = self._organizer.modList()
        if modlist.getMod(mod_name):
            return
        qInfo(f"CET VFS fix: creating mod {mod_name}")
        new_mod = self._organizer.createMod(mobase.GuessedString(mod_name))
        if not new_mod:
            return
        mod_name = new_mod.name()
        new_mod.setGameName(self.gameShortName())
        new_mod.setUrl(f"{self.getSupportURL()}#crashreporter")
        crashReporter = Path(new_mod.absolutePath(), self._crashreporter_path)
        crashReporter.parent.mkdir(parents=True)
        crashReporter.touch()

        def callback():
            modlist.setActive(mod_name, True)
            modlist.setPriority(mod_name, priority)

        self._organizer.onNextRefresh(callback, False)
        self._organizer.refresh()
        self._set_setting("disable_crashreporter", False)

    def iniFiles(self):
        return ["UserSettings.json"]

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        ext = self._mappings.savegameExtension.get()
        return [
            CyberpunkSaveGame(path.parent)
            for path in Path(folder.absolutePath()).glob(f"**/*.{ext}")
        ]

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "skipStartScreen",
                (
                    'Skips the "Breaching..." start screen on game launch'
                    " (can also skip loading of GOG rewards)"
                ),
                False,
            ),
            mobase.PluginSetting(
                "enforce_archive_load_order",
                (
                    "Enforce the current load order via"
                    " <code>archive/pc/mod/modlist.txt</code>"
                ),
                False,
            ),
            mobase.PluginSetting(
                "reverse_archive_load_order",
                (
                    "Reverse MOs load order in"
                    " <code>archive/pc/mod/modlist.txt</code>"
                    " (first loaded mod wins = last one / highest prio in MO)"
                ),
                False,
            ),
            mobase.PluginSetting(
                "enforce_redmod_load_order",
                "Enforce the current load order on redmod deployment",
                True,
            ),
            mobase.PluginSetting(
                "reverse_redmod_load_order",
                (
                    "Reverse MOs load order on redmod deployment"
                    " (first loaded mod wins = last one / highest prio in MO)"
                ),
                False,
            ),
            mobase.PluginSetting(
                "auto_deploy_redmod",
                "Deploy redmod before game launch if necessary",
                True,
            ),
            mobase.PluginSetting(
                "clear_cache_after_game_update",
                (
                    'Clears "overwrite/r6/cache/*" if the original game files changed'
                    " (after update)"
                ),
                True,
            ),
            mobase.PluginSetting(
                "disable_crashreporter",
                (
                    "Creates a dummy mod/file to disable CrashReporter.exe, "
                    "which is not compatible with VFS version.dll, see wiki"
                ),
                True,
            ),
            mobase.PluginSetting(
                "crash_message",
                ("Show a crash message as replacement of disabled CrashReporter"),
                True,
            ),
            mobase.PluginSetting(
                "show_rootbuilder_conversion",
                (
                    "Shows a dialog to convert legacy RootBuilder mods to native MO mods,"
                    " using force load libraries"
                ),
                True,
            ),
        ]

    def _get_setting(self, key: str) -> mobase.MoVariant:
        return self._organizer.pluginSetting(self.name(), key)

    def _set_setting(self, key: str, value: mobase.MoVariant):
        self._organizer.setPluginSetting(self.name(), key, value)

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_name = self.gameName()
        game_dir = self.gameDirectory()
        bin_path = game_dir.absoluteFilePath(self.binaryName())
        skip_start_screen = (
            "-skipStartScreen" if self._get_setting("skipStartScreen") else ""
        )
        return [
            # Default, runs REDmod deploy if necessary
            mobase.ExecutableInfo(
                f"{game_name} (REDmod)",
                bin_path,
            ).withArgument(f"--launcher-skip -modded {skip_start_screen}"),
            # Start game without REDmod
            mobase.ExecutableInfo(
                f"{game_name}",
                bin_path,
            ).withArgument(f"--launcher-skip {skip_start_screen}"),
            # Deploy REDmods only
            mobase.ExecutableInfo(
                "REDmod",
                self._get_redmod_binary(),
            ).withArgument("deploy -reportProgress -force %modlist%"),
            # Launcher
            mobase.ExecutableInfo(
                "REDprelauncher",
                game_dir.absoluteFilePath(self.getLauncherName()),
            ).withArgument(f"{skip_start_screen}"),
        ]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        exe = Path(self.binaryName()).name
        return [
            mobase.ExecutableForcedLoadSetting(exe, lib).withEnabled(True)
            for lib in self._forced_libraries
        ]

    def _get_redmod_binary(self) -> Path:
        """Absolute path to redmod binary"""
        return Path(self.gameDirectory().absolutePath(), self._redmod_binary)

    def _onAboutToRun(self, app_path_str: str, wd: QDir, args: str) -> bool:
        if not self.isActive():
            return True
        app_path = Path(app_path_str)
        if app_path == self._get_redmod_binary():
            if m := re.search(r"%modlist%", args, re.I):
                # Manual deployment: replace %modlist% variable
                (
                    modlist_path,
                    modlist,
                    _,
                ) = self._modlist_files.update_modlist("redmod")
                modlist_param = f'-modlist="{modlist_path}"' if modlist else ""
                args = f"{args[: m.start()]}{modlist_param}{args[m.end() :]}"
                qInfo(f"Manual modlist deployment: replacing {m[0]}, new args = {args}")
                self._check_redmod_result(
                    self._organizer.waitForApplication(
                        self._organizer.startApplication(app_path_str, [args], wd),
                        False,
                    )
                )
                return False  # redmod with new args started
            return True  # No recursive redmod call
        if (
            self._get_setting("auto_deploy_redmod")
            and app_path == Path(self.gameDirectory().absolutePath(), self.binaryName())
            and "-modded" in args
            and not self._check_redmod_result(self._deploy_redmod())
        ):
            qWarning("Aborting game launch.")
            return False  # Auto deploy failed
        self._map_cache_files()
        if self._get_setting("enforce_archive_load_order"):
            self._modlist_files.update_modlist("archive")
        return True

    def _onFinishedRun(self, path: str, exit_code: int) -> None:
        if not self._get_setting("crash_message"):
            return
        if path.endswith(self.binaryName()) and exit_code > 0:
            crash_message = QMessageBox(
                QMessageBox.Icon.Critical,
                "Cyberpunk Crashed",
                textwrap.dedent(
                    f"""
                    Cyberpunk crashed. Tips:
                    - disable mods (create backup of modlist or use new profile)
                    - clear overwrite or delete at least overwrite/r6/cache (to keep mod settings)
                    - check log files of CET/redscript/RED4ext (in overwrite)
                    - read [FAQ & Troubleshooting]({self.GameSupportURL}#faq--troubleshooting)
                    """
                ),
                QMessageBox.StandardButton.Ok,
                self._parentWidget,
            )
            crash_message.setTextFormat(Qt.TextFormat.MarkdownText)
            hide_cb = QCheckBox("&Do not show again*", crash_message)
            hide_cb.setToolTip(f"Settings/Plugins/{self.name()}/crash_message")
            crash_message.setCheckBox(hide_cb)
            crash_message.open(  # type: ignore
                lambda: hide_cb.isChecked()
                and self._set_setting("crash_message", False)
            )

    def _check_redmod_result(self, result: tuple[bool, int]) -> bool:
        if result == (True, 0):
            return True
        if result[1] < 0:
            qWarning(f"REDmod deployment aborted (exit code {result[1]}).")
        else:
            qCritical(
                f"REDmod deployment failed with exit code {result[1]} !"
                f" Check {Path('GAME_FOLDER/', self._redmod_log)}"
            )
        return False

    def _deploy_redmod(self) -> tuple[bool, int]:
        """Deploys redmod. Clears deployed files if no redmods are active.
        Recreates deployed files to force load order when necessary.

        Returns:
            (success?, exit code)
        """
        # Add REDmod load order if none is specified
        redmod_list = list(self._modlist_files.modfile_names("redmod"))
        if not redmod_list:
            qInfo("Cleaning up redmod deployed files")
            self._clean_deployed_redmod()
            return True, 0
        args = self._redmod_deploy_args
        if self._get_setting("enforce_redmod_load_order"):
            modlist_path, _, old_redmods = self._modlist_files.update_modlist(
                "redmod", redmod_list
            )
            if (
                Counter(redmod_list) == Counter(old_redmods)
                and not redmod_list == old_redmods
            ):
                # Only load order changed: recreate redmod deploys
                # Fix for redmod not detecting change of load order.
                # Faster than -force https://github.com/E1337Kat/cyberpunk2077_ext_redux/issues/297  # noqa: E501
                qInfo("Redmod order changed, recreate deployed files")
                self._clean_deployed_redmod(modlist_path)
            qInfo(f"Deploying redmod with modlist: {modlist_path}")
            args += f' -modlist="{modlist_path}"'
        else:
            qInfo("Deploying redmod")
        redmod_binary = self._get_redmod_binary()
        return self._organizer.waitForApplication(
            self._organizer.startApplication(
                redmod_binary, [args], redmod_binary.parent
            ),
            False,
        )

    def _clean_deployed_redmod(self, modlist_path: Path | None = None):
        """Delete all files from `_redmod_deploy_path` except for `modlist_path`."""
        for file in self._organizer.findFiles(self._redmod_deploy_path, "*"):
            file_path = Path(file)
            if modlist_path is None or file_path != modlist_path:
                file_path.unlink()

    def _map_cache_files(self):
        """
        Copy cache files (`final.redscript` etc.) to overwrite to catch
        overwritten game files.
        """
        data_path = Path(self.dataDirectory().absolutePath())
        overwrite_path = Path(self._organizer.overwritePath())
        data_cache_path = data_path / "r6/cache"
        overwrite_cache_path = overwrite_path / "r6/cache"
        cache_files = (
            file.relative_to(data_path) for file in data_cache_path.glob("*")
        )
        if self._get_setting("clear_cache_after_game_update") and any(
            self._is_cache_file_updated(file, data_path) for file in cache_files
        ):
            qInfo('Updated game files detected, clearing "overwrite/r6/cache/*"')
            try:
                shutil.rmtree(overwrite_cache_path)
            except FileNotFoundError:
                pass
            qInfo('Updating "r6/cache" in overwrite')
            shutil.copytree(data_cache_path, overwrite_cache_path, dirs_exist_ok=True)
        else:
            for file in self._unmapped_cache_files(data_path):
                qInfo(f'Copying "{file}" to overwrite (to catch file overwrites)')
                dst = overwrite_path / file
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(data_path / file, dst)

    def _is_cache_file_updated(self, file: Path, data_path: Path) -> bool:
        """Checks if a cache file is updated (in game dir).

        Args:
            file: Relative to data dir.
        """
        game_file = data_path.absolute() / file
        mapped_files = self._organizer.findFiles(file.parent, file.name)
        return bool(
            mapped_files
            and (mapped_file := mapped_files[0])
            and not (
                game_file.samefile(mapped_file)
                or filecmp.cmp(game_file, mapped_file)
                or (  # different backup file
                    (
                        backup_files := self._organizer.findFiles(
                            file.parent, f"{file.name}.bk"
                        )
                    )
                    and filecmp.cmp(game_file, backup_files[0])
                )
            )
        )

    def _unmapped_cache_files(self, data_path: Path) -> Iterable[Path]:
        """Yields unmapped cache files relative to `data_path`."""
        for file in self._organizer.findFiles("r6/cache", "*"):
            try:
                yield Path(file).absolute().relative_to(data_path)
            except ValueError:
                continue

    def _get_rootbuilder_conversion_dialog(
        self, parent_widget: QWidget | None = None
    ) -> QMessageBox | None:
        """
        Dialog to convert any mods with `root` folder, if applicable.
        CET and RED4ext work with forced load libraries since ~ Cyberpunk v2.12,
        making RootBuilder unnecessary.
        """
        setting = "show_rootbuilder_conversion"
        if not self.isActive() or not self._get_setting(setting):
            return None
        if not (
            (root_folder := self._organizer.virtualFileTree().find("root"))
            and root_folder.isDir()
        ):
            return None
        parent_widget = parent_widget or self._parentWidget
        message_box = QMessageBox(
            QMessageBox.Icon.Question,
            "RootBuilder obsolete",
            textwrap.dedent(
                """
                Mod Organizer now supports Cyberpunk Engine Tweaks (CET) and
                RED4ext native via forced load libraries, making RootBuilder
                unnecessary.

                Do you want to convert all mods with a `root` folder now?
                <br/>This usually only affects CET, RED4ext and overwrite.

                You can disable RootBuilder afterwards.
                """
            ),
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
            parent_widget,
        )
        message_box.setTextFormat(Qt.TextFormat.MarkdownText)
        checkbox = QCheckBox("&Do not show again*", parent_widget)
        checkbox.setChecked(True)
        checkbox.setToolTip(f"Settings/Plugins/{self.name()}/{setting}")
        message_box.setCheckBox(checkbox)

        def accept_callback():
            if unfolded_mods := unfold_root_folders(self._organizer, parent_widget):
                n_mods = len(unfolded_mods)
                info = QMessageBox(
                    QMessageBox.Icon.Information,
                    "Root mods converted",
                    (
                        f"{n_mods} mod{'s' if n_mods > 1 else ''} converted."
                        "You can disable RootBuilder in the settings now."
                    ),
                    parent=parent_widget,
                )
                info.setDetailedText(f"Converted mods:\n{'\n'.join(unfolded_mods)}")
                info.open()  # type: ignore

        message_box.accepted.connect(accept_callback)  # type: ignore

        def finished_callback():
            if checkbox.isChecked():
                self._set_setting(setting, False)
            self._check_disable_crashreporter()

        message_box.finished.connect(finished_callback)  # type: ignore
        return message_box


def unfold_root_folders(
    organizer: mobase.IOrganizer, parent_widget: QWidget | None = None
) -> list[str]:
    """Unfolds (RootBuilders) root folders of all mods (excluding backups)."""
    mods = organizer.modList().allMods()
    progress = None
    unfolded_mods: list[str] = []
    if parent_widget:
        progress = QProgressDialog(
            "Merging/unfolding root folders...",
            "Abort",
            0,
            len(mods),
            parent_widget,
        )
        progress.setWindowModality(Qt.WindowModality.WindowModal)
    for i, mod_name in enumerate(mods):
        if progress:
            if progress.wasCanceled():
                break
            progress.setValue(i)
        if mod_name == "data":
            continue
        mod = organizer.modList().getMod(mod_name)
        if mod.isBackup() or mod.isSeparator() or mod.isForeign():
            continue
        root_folder = mod.fileTree().find("root")
        if root_folder is None or not root_folder.isDir():
            continue
        qInfo(f"Merging root folder of {mod_name}")
        mod_path = Path(mod.absolutePath())
        root_folder_path = mod_path / "root"
        unfold_folder(root_folder_path)
        unfolded_mods.append(mod_name)
    if progress:
        progress.setValue(len(mods))
    organizer.refresh()
    return unfolded_mods


def unfold_folder(src_path: Path):
    """
    Unfolds a folder (`parent/src/* -> parent/*`), overwriting existing files/folders.
    Preserves a subfolder with same name (`parent/src/src -> parent/src`).
    """
    parent = src_path.parent
    if (src_path / src_path.name).exists():
        # Contains a file/folder with same name
        with tempfile.TemporaryDirectory(dir=parent) as temp_folder:
            src_path = src_path.rename(parent / temp_folder / src_path.name)
            shutil.copytree(src_path, parent, symlinks=True, dirs_exist_ok=True)
    else:
        shutil.copytree(src_path, parent, symlinks=True, dirs_exist_ok=True)
        shutil.rmtree(src_path)
