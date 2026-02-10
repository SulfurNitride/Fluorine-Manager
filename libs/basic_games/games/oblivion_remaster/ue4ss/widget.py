import json
from functools import cmp_to_key
from json import JSONDecodeError
from pathlib import Path

from PyQt6.QtCore import QDir, QFileInfo, Qt
from PyQt6.QtWidgets import QGridLayout, QWidget

import mobase

from ..constants import DEFAULT_UE4SS_MODS, UE4SSModInfo
from .model import UE4SSListModel
from .view import UE4SSView


class UE4SSTabWidget(QWidget):
    def __init__(self, parent: QWidget | None, organizer: mobase.IOrganizer):
        super().__init__(parent)
        self._organizer = organizer
        self._view = UE4SSView(self)
        self._layout = QGridLayout(self)
        self._layout.addWidget(self._view)
        self._model = UE4SSListModel(self._view, organizer)
        self._view.setModel(self._model)
        self._model.dataChanged.connect(self.write_mod_list)  # type: ignore
        self._view.data_dropped.connect(self.write_mod_list)  # type: ignore
        organizer.onProfileChanged(lambda profile_a, profile_b: self._parse_mod_files())
        organizer.modList().onModInstalled(self.update_mod_files)
        organizer.modList().onModRemoved(lambda mod: self._parse_mod_files())
        organizer.modList().onModStateChanged(self.update_mod_files)
        self._parse_mod_files()

    def get_mod_list(self) -> list[str]:
        mod_list: list[str] = []
        for index in range(self._model.rowCount()):
            mod_list.append(
                self._model.data(
                    self._model.index(index, 0), Qt.ItemDataRole.DisplayRole
                )
            )
        return mod_list

    def update_mod_files(
        self, mods: dict[str, mobase.ModState] | mobase.IModInterface | str
    ):
        mod_list: list[mobase.IModInterface] = []
        if isinstance(mods, dict):
            for mod in mods.keys():
                mod_list.append(self._organizer.modList().getMod(mod))
        elif isinstance(mods, mobase.IModInterface):
            mod_list.append(mods)
        else:
            mod_list.append(self._organizer.modList().getMod(mods))

        for mod in mod_list:
            tree = mod.fileTree()
            ue4ss_files = tree.find("UE4SS")
            if not ue4ss_files:
                ue4ss_files = tree.find(
                    "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods"
                )
            if isinstance(ue4ss_files, mobase.IFileTree):
                for entry in ue4ss_files:
                    if isinstance(entry, mobase.IFileTree):
                        if enabled_txt := entry.find("enabled.txt"):
                            try:
                                Path(mod.absolutePath(), enabled_txt.path("/")).unlink()
                                self._organizer.modDataChanged(mod)
                            except FileNotFoundError:
                                pass

        self._parse_mod_files()

    def _parse_mod_files(self):
        from ...game_oblivion_remaster import OblivionRemasteredGame

        mod_list: set[str] = set()
        for mod in self._organizer.modList().allMods():
            if (
                mobase.ModState(self._organizer.modList().state(mod))
                & mobase.ModState.ACTIVE
            ):
                tree = self._organizer.modList().getMod(mod).fileTree()
                ue4ss_files = tree.find("UE4SS")
                if not ue4ss_files:
                    ue4ss_files = tree.find(
                        "Root/OblivionRemastered/Binaries/Win64/ue4ss/Mods"
                    )
                if isinstance(ue4ss_files, mobase.IFileTree):
                    for entry in ue4ss_files:
                        if isinstance(entry, mobase.IFileTree):
                            if entry.find("scripts/main.lua"):
                                mod_list.add(entry.name())
                            if enabled_txt := entry.find("enabled.txt"):
                                try:
                                    Path(
                                        self._organizer.modList()
                                        .getMod(mod)
                                        .absolutePath(),
                                        enabled_txt.path("/"),
                                    ).unlink()
                                    self._organizer.modDataChanged(
                                        self._organizer.modList().getMod(mod)
                                    )
                                except FileNotFoundError:
                                    pass

        game = self._organizer.managedGame()
        if isinstance(game, OblivionRemasteredGame):
            if game.ue4ssDirectory().exists():
                for dir_info in game.ue4ssDirectory().entryInfoList(
                    QDir.Filter.Dirs | QDir.Filter.NoDotAndDotDot
                ):
                    if QFileInfo(
                        QDir(dir_info.absoluteFilePath()).absoluteFilePath(
                            "scripts/main.lua"
                        )
                    ).exists():
                        mod_list.add(dir_info.fileName())
                    if QFileInfo(
                        QDir(dir_info.absoluteFilePath()).absoluteFilePath(
                            "enabled.txt"
                        )
                    ).exists():
                        Path(dir_info.absoluteFilePath(), "enabled.txt").unlink()

        final_list = sorted(mod_list, key=cmp_to_key(self.sort_mods))
        self._model.setStringList(final_list)

    def write_mod_list(self):
        mod_list: list[UE4SSModInfo] = []
        profile = QDir(self._organizer.profilePath())
        mods_txt = QFileInfo(profile.absoluteFilePath("mods.txt"))
        with open(mods_txt.absoluteFilePath(), "w") as txt_file:
            for i in range(self._model.rowCount()):
                item = self._model.index(i, 0)
                name = self._model.data(item, Qt.ItemDataRole.DisplayRole)
                active = (
                    self._model.data(item, Qt.ItemDataRole.CheckStateRole)
                    == Qt.CheckState.Checked
                )
                mod_list.append({"mod_name": name, "mod_enabled": active})
                txt_file.write(f"{name} : {1 if active else 0}\n")
        mods_json = QFileInfo(profile.absoluteFilePath("mods.json"))
        with open(mods_json.absoluteFilePath(), "w") as json_file:
            json_file.write(json.dumps(mod_list, indent=4))

    def sort_mods(self, mod_a: str, mod_b: str) -> int:
        profile = QDir(self._organizer.profilePath())
        mods_json = QFileInfo(profile.absoluteFilePath("mods.json"))
        mods_list: list[str] = []
        if mods_json.exists() and mods_json.isFile():
            with open(mods_json.absoluteFilePath(), "r") as json_file:
                try:
                    mods = json.load(json_file)
                except JSONDecodeError:
                    mods = DEFAULT_UE4SS_MODS
                for mod in mods:
                    if mod["mod_enabled"]:
                        mods_list.append(mod["mod_name"])
        index_a = -1
        if mod_a in mods_list:
            index_a = mods_list.index(mod_a)
        index_b = -1
        if mod_b in mods_list:
            index_b = mods_list.index(mod_b)
        if index_a != -1 and index_b != -1:
            return index_a - index_b
        if index_a != -1:
            return -1
        if index_b != -1:
            return 1
        if mod_a < mod_b:
            return -1
        return 1
