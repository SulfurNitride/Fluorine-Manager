import json
import os.path
import shutil
import winreg
from enum import IntEnum, auto
from pathlib import Path
from typing import cast

from PyQt6.QtCore import QDir, QFileInfo, QStandardPaths
from PyQt6.QtWidgets import QMainWindow, QTabWidget, QWidget

import mobase

from ..basic_features import BasicGameSaveGameInfo
from ..basic_game import BasicGame
from .oblivion_remaster.constants import DEFAULT_UE4SS_MODS, PLUGIN_NAME, UE4SSModInfo
from .oblivion_remaster.paks.widget import PaksTabWidget
from .oblivion_remaster.ue4ss.widget import UE4SSTabWidget


def getLootPath() -> Path | None:
    """
    Parse the LOOT path using either the modern InnoSetup registry entries (local vs. global installs) or the
    old registry path.
    """
    paths = [
        (
            winreg.HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{BF634210-A0D4-443F-A657-0DCE38040374}_is1",
            "InstallLocation",
        ),
        (
            winreg.HKEY_LOCAL_MACHINE,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{BF634210-A0D4-443F-A657-0DCE38040374}_is1",
            "InstallLocation",
        ),
        (winreg.HKEY_LOCAL_MACHINE, "Software\\LOOT", "Installed Path"),
    ]

    for path in paths:
        try:
            with winreg.OpenKeyEx(
                path[0],
                path[1],
            ) as key:
                value = winreg.QueryValueEx(key, path[2])
                return Path((value[0] + "/LOOT.exe").replace("/", "\\"))
        except FileNotFoundError:
            pass
    return None


class Problems(IntEnum):
    """
    Enums for IPluginDiagnose.
    """

    # The 'dwmapi.dll' is present in the game EXE directory.
    UE4SS_LOADER = auto()
    # A UE4SS mod has a space in the directory name.
    INVALID_UE4SS_MOD_NAME = auto()


class OblivionRemasteredGame(
    BasicGame, mobase.IPluginFileMapper, mobase.IPluginDiagnose
):
    Name = PLUGIN_NAME
    Author = "Silarn"
    Version = "0.1.0-b.1"
    Description = "TES IV: Oblivion Remastered; an unholy hybrid of Bethesda and Unreal"

    GameName = "Oblivion Remastered"
    GameShortName = "oblivionremastered"
    GameNexusId = 7587
    GameSteamId = 2623190
    GameBinary = "OblivionRemastered.exe"
    GameDataPath = r"%GAME_PATH%\OblivionRemastered\Content\Dev\ObvData\Data"
    GameDocumentsDirectory = r"%GAME_PATH%\OblivionRemastered\Content\Dev\ObvData"
    UserHome = QStandardPaths.writableLocation(
        QStandardPaths.StandardLocation.HomeLocation
    )
    # Oblivion Remastered does not use the expanded Documents path but instead always uses the
    # base user directory path, even when this disagrees with Windows.
    MyDocumentsDirectory = rf"{UserHome}\Documents\My Games\{GameName}"
    GameSavesDirectory = rf"{MyDocumentsDirectory}\Saved\SaveGames"
    GameSaveExtension = "sav"
    GameSupportURL = (
        r"https://github.com/ModOrganizer2/modorganizer-basic_games/wiki/"
        "Game:-Elder-Scrolls-IV:-Oblivion-Remastered"
    )

    _main_window: QMainWindow
    _ue4ss_tab: UE4SSTabWidget
    _paks_tab: PaksTabWidget

    def __init__(self):
        BasicGame.__init__(self)
        mobase.IPluginFileMapper.__init__(self)
        mobase.IPluginDiagnose.__init__(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        from .oblivion_remaster.game_plugins import OblivionRemasteredGamePlugins
        from .oblivion_remaster.mod_data_checker import OblivionRemasteredModDataChecker
        from .oblivion_remaster.mod_data_content import OblivionRemasteredDataContent
        from .oblivion_remaster.script_extender import OblivionRemasteredScriptExtender

        super().init(organizer)
        self._register_feature(BasicGameSaveGameInfo())
        self._register_feature(OblivionRemasteredGamePlugins(self._organizer))
        self._register_feature(OblivionRemasteredModDataChecker(self._organizer))
        self._register_feature(OblivionRemasteredScriptExtender(self))
        self._register_feature(OblivionRemasteredDataContent())

        organizer.onUserInterfaceInitialized(self.init_tab)
        return True

    def init_tab(self, main_window: QMainWindow):
        """
        Initializes tabs unique to Oblivion Remastered.
        The "UE4SS Mods" tab and "Paks" tab.
        """
        if self._organizer.managedGame() != self:
            return

        self._main_window = main_window
        tab_widget: QTabWidget = main_window.findChild(QTabWidget, "tabWidget")
        if not tab_widget or not tab_widget.findChild(QWidget, "espTab"):
            return

        self._ue4ss_tab = UE4SSTabWidget(main_window, self._organizer)

        # Find the default "Plugins" tab
        plugin_tab = tab_widget.findChild(QWidget, "espTab")
        tab_index = tab_widget.indexOf(plugin_tab) + 1
        # The "Bethesda Plugins Manager" plugin hides the default Plugins tab and inserts itself after.
        # If the default tab is hidden, increment our position by one to account for it.
        if not tab_widget.isTabVisible(tab_widget.indexOf(plugin_tab)):
            tab_index += 1
        tab_widget.insertTab(tab_index, self._ue4ss_tab, "UE4SS Mods")

        self._paks_tab = PaksTabWidget(main_window, self._organizer)

        tab_index += 1
        tab_widget.insertTab(tab_index, self._paks_tab, "Paks")

    def settings(self) -> list[mobase.PluginSetting]:
        return [
            mobase.PluginSetting(
                "ue4ss_use_root_builder", "Use Root Builder paths for UE4SS mods", False
            )
        ]

    def executables(self):
        execs = [
            mobase.ExecutableInfo(
                "Oblivion Remastered",
                QFileInfo(
                    QDir(
                        self.gameDirectory().absoluteFilePath(
                            "OblivionRemastered/Binaries/Win64"
                        )
                    ),
                    "OblivionRemastered-Win64-Shipping.exe",
                ),
            )
        ]
        if extender := self._organizer.gameFeatures().gameFeature(
            mobase.ScriptExtender
        ):
            execs.append(
                mobase.ExecutableInfo("OBSE64", QFileInfo(extender.loaderPath()))  # type: ignore
            )
        if lootPath := getLootPath():
            execs.append(
                mobase.ExecutableInfo("LOOT", QFileInfo(str(lootPath))).withArgument(
                    '--game="Oblivion Remastered"'
                )
            )
        if magicLoaderPath := self.gameDirectory().absoluteFilePath(
            "MagicLoader/MagicLoader.exe"
        ):
            execs.append(
                mobase.ExecutableInfo("MagicLoader", QFileInfo(magicLoaderPath))
            )

        return execs

    def primaryPlugins(self) -> list[str]:
        return [
            "Oblivion.esm",
            "DLCBattlehornCastle.esp",
            "DLCFrostcrag.esp",
            "DLCHorseArmor.esp",
            "DLCMehrunesRazor.esp",
            "DLCOrrery.esp",
            "DLCShiveringIsles.esp",
            "DLCSpellTomes.esp",
            "DLCThievesDen.esp",
            "DLCVileLair.esp",
            "Knights.esp",
            "AltarESPMain.esp",
            "AltarDeluxe.esp",
            "AltarESPLocal.esp",  # Not actually shipped with the game files but present in plugins.txt.
        ]

    def modDataDirectory(self) -> str:
        return "Data"

    def moviesDirectory(self) -> QDir:
        return QDir(
            self.gameDirectory().absolutePath() + "/OblivionRemastered/Content/Movies"
        )

    def paksDirectory(self) -> QDir:
        return QDir(
            self.gameDirectory().absolutePath() + "/OblivionRemastered/Content/Paks"
        )

    def exeDirectory(self) -> QDir:
        return QDir(
            self.gameDirectory().absolutePath() + "/OblivionRemastered/Binaries/Win64"
        )

    def obseDirectory(self) -> QDir:
        return QDir(self.exeDirectory().absolutePath() + "/OBSE")

    def ue4ssDirectory(self) -> QDir:
        return QDir(self.exeDirectory().absolutePath() + "/ue4ss/Mods")

    def loadOrderMechanism(self) -> mobase.LoadOrderMechanism:
        return mobase.LoadOrderMechanism.PLUGINS_TXT

    def sortMechanism(self) -> mobase.SortMechanism:
        return mobase.SortMechanism.LOOT

    def initializeProfile(
        self, directory: QDir, settings: mobase.ProfileSetting
    ) -> None:
        if settings & mobase.ProfileSetting.CONFIGURATION:
            game_ini_file = self.gameDirectory().absoluteFilePath(
                r"OblivionRemastered\Content\Dev\ObvData\Oblivion.ini"
            )
            game_default_ini = self.gameDirectory().absoluteFilePath(
                r"OblivionRemastered\Content\Dev\ObvData\Oblivion_default.ini"
            )
            profile_ini = directory.absoluteFilePath(
                QFileInfo("Oblivion.ini").fileName()
            )
            if not os.path.exists(profile_ini):
                try:
                    shutil.copyfile(
                        game_ini_file,
                        profile_ini,
                    )
                except FileNotFoundError:
                    if os.path.exists(game_ini_file):
                        shutil.copyfile(
                            game_default_ini,
                            profile_ini,
                        )
                    else:
                        Path(profile_ini).touch()
        # Initialize a default UE4SS mods.ini and mods.json with the core mods included
        self.write_default_mods(directory)
        # Bootstrap common mod directories used by the USVFS map
        if (
            self._organizer.managedGame()
            and self._organizer.managedGame().gameName() == self.gameName()
        ):
            if not self.paksDirectory().exists():
                os.makedirs(self.paksDirectory().absolutePath())
            if not self.obseDirectory().exists():
                os.makedirs(self.obseDirectory().absolutePath())
            if not self.ue4ssDirectory().exists():
                os.makedirs(self.ue4ssDirectory().absolutePath())

    def write_default_mods(self, profile: QDir):
        """
        Writer for the default UE4SS 'mods.txt' and 'mods.json' profile files.
        """

        ue4ss_mods_txt = QFileInfo(profile.absoluteFilePath("mods.txt"))
        ue4ss_mods_json = QFileInfo(profile.absoluteFilePath("mods.json"))
        if not ue4ss_mods_txt.exists():
            with open(ue4ss_mods_txt.absoluteFilePath(), "w") as mods_txt:
                for mod in DEFAULT_UE4SS_MODS:
                    mods_txt.write(f"{mod['mod_name']} : 1\n")
        if not ue4ss_mods_json.exists():
            mods_data: list[UE4SSModInfo] = []
            for mod in DEFAULT_UE4SS_MODS:
                mods_data.append({"mod_name": mod["mod_name"], "mod_enabled": True})
            with open(ue4ss_mods_json.absoluteFilePath(), "w") as mods_json:
                mods_json.write(json.dumps(mods_data, indent=4))

    def iniFiles(self) -> list[str]:
        return ["Oblivion.ini"]

    def mappings(self) -> list[mobase.Mapping]:
        mappings: list[mobase.Mapping] = []
        for profile_file in ["plugins.txt", "loadorder.txt"]:
            mappings.append(
                mobase.Mapping(
                    self._organizer.profilePath() + "/" + profile_file,
                    self.dataDirectory().absolutePath() + "/" + profile_file,
                    False,
                )
            )
        for profile_file in ["mods.txt", "mods.json"]:
            mappings.append(
                mobase.Mapping(
                    self._organizer.profilePath() + "/" + profile_file,
                    self.ue4ssDirectory().absolutePath() + "/" + profile_file,
                    False,
                )
            )
        return mappings

    def getModMappings(self) -> dict[str, list[str]]:
        return {
            "Data": [self.dataDirectory().absolutePath()],
            "Paks": [self.paksDirectory().absolutePath()],
            "OBSE": [self.obseDirectory().absolutePath()],
            "Movies": [self.moviesDirectory().absolutePath()],
            "UE4SS": [self.ue4ssDirectory().absolutePath()],
            "GameSettings": [self.exeDirectory().absoluteFilePath("GameSettings")],
        }

    def activeProblems(self) -> list[int]:
        if self._organizer.managedGame() == self:
            problems: set[Problems] = set()
            # The dwmapi.dll loader should not be used with USVFS
            ue4ss_loader = QFileInfo(self.exeDirectory().absoluteFilePath("dwmapi.dll"))
            if ue4ss_loader.exists():
                problems.add(Problems.UE4SS_LOADER)
            # Leverage UE4SS mod tab to find mod names with spaces
            for mod in self._ue4ss_tab.get_mod_list():
                if " " in mod:
                    problems.add(Problems.INVALID_UE4SS_MOD_NAME)
                    break
            return list(problems)
        return []

    def fullDescription(self, key: int) -> str:
        match key:
            case Problems.UE4SS_LOADER:
                return (
                    "The UE4SS loader DLL is present (dwmapi.dll). This will not function out-of-the box with MO2's virtual filesystem.\n\n"
                    + "In order to resolve this, either delete the DLL and use the OBSE UE4SS Loader plugin, or rename "
                    + "the DLL (ex. 'ue4ss_loader.dll') and set it to force load with the game exe.\n\n"
                    + "Do this for any executable which runs the game, such as the OBSE64 loader."
                )
            case Problems.INVALID_UE4SS_MOD_NAME:
                return (
                    "UE4SS mods do not load properly with spaces in the mod name. These are stripped when parsing mods.txt and then"
                    "fail to match up when parsing the mods.json. Simply remove the spaces and they should load correctly."
                )
            case _:
                return ""

    def hasGuidedFix(self, key: int) -> bool:
        match key:
            case Problems.UE4SS_LOADER:
                return True
            case Problems.INVALID_UE4SS_MOD_NAME:
                return True
            case _:
                return False

    def shortDescription(self, key: int) -> str:
        match key:
            case Problems.UE4SS_LOADER:
                return "The UE4SS loader DLL is present (dwmapi.dll)."
            case Problems.INVALID_UE4SS_MOD_NAME:
                return "A UE4SS mod name contains a space."
            case _:
                return ""

    def startGuidedFix(self, key: int) -> None:
        match key:
            case Problems.UE4SS_LOADER:
                os.rename(
                    self.exeDirectory().absoluteFilePath("dwmapi.dll"),
                    self.exeDirectory().absoluteFilePath("ue4ss_loader.dll"),
                )
            case Problems.INVALID_UE4SS_MOD_NAME:
                for mod in self._organizer.modList().allMods():
                    filetree = self._organizer.modList().getMod(mod).fileTree()
                    ue4ss_mod = filetree.find("UE4SS")
                    if not ue4ss_mod:
                        filetree.find(
                            "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods"
                        )
                    if isinstance(ue4ss_mod, mobase.IFileTree):
                        for entry in ue4ss_mod:
                            if isinstance(entry, mobase.IFileTree) and entry.find(
                                "scripts/main.lua"
                            ):
                                if " " in entry.name():
                                    mod_dir = Path(
                                        self._organizer.modList()
                                        .getMod(mod)
                                        .absolutePath()
                                    )
                                    mod_path = mod_dir.joinpath(entry.path("/"))
                                    fixed_path = mod_dir.joinpath(
                                        cast(mobase.IFileTree, entry.parent()).path(
                                            "/"
                                        ),
                                        entry.name().replace(" ", ""),
                                    )
                                    try:
                                        mod_path.rename(fixed_path)
                                        self._organizer.modDataChanged(
                                            self._organizer.modList().getMod(mod)
                                        )
                                        self._ue4ss_tab.update_mod_files(mod)
                                    except FileExistsError:
                                        pass
                                    except FileNotFoundError:
                                        pass
                for entry in self.ue4ssDirectory().entryInfoList(
                    QDir.Filter.Dirs | QDir.Filter.NoDotAndDotDot
                ):
                    entry_dir = QDir(entry.absoluteFilePath())
                    if QFileInfo(
                        entry_dir.absoluteFilePath("scripts/main.lua")
                    ).exists():
                        if " " in entry_dir.dirName():
                            dest = (
                                entry_dir.absoluteFilePath("..")
                                + "/"
                                + entry_dir.dirName().replace(" ", "")
                            )
                            try:
                                os.rename(entry_dir.absolutePath(), dest)
                            except FileExistsError:
                                pass
                            except FileNotFoundError:
                                pass
            case _:
                pass
