import json
from json import JSONDecodeError
from typing import Any, Iterable

from PyQt6.QtCore import (
    QDir,
    QFileInfo,
    QMimeData,
    QModelIndex,
    QStringListModel,
    Qt,
)
from PyQt6.QtWidgets import QWidget

import mobase

from ..constants import DEFAULT_UE4SS_MODS


class UE4SSListModel(QStringListModel):
    def __init__(self, parent: QWidget | None, organizer: mobase.IOrganizer):
        super().__init__(parent)
        self._checked_items: set[str] = set()
        self._organizer = organizer
        self._init_mod_states()

    def _init_mod_states(self):
        profile = QDir(self._organizer.profilePath())
        mods_json = QFileInfo(profile.absoluteFilePath("mods.json"))
        if mods_json.exists():
            with open(mods_json.absoluteFilePath(), "r") as json_file:
                try:
                    mod_data = json.load(json_file)
                except JSONDecodeError:
                    mod_data = DEFAULT_UE4SS_MODS
                for mod in mod_data:
                    if mod["mod_enabled"]:
                        self._checked_items.add(mod["mod_name"])

    def _set_mod_states(self):
        profile = QDir(self._organizer.profilePath())
        mods_json = QFileInfo(profile.absoluteFilePath("mods.json"))
        mod_list: dict[str, bool] = {}
        if mods_json.exists():
            with open(mods_json.absoluteFilePath(), "r") as json_file:
                try:
                    mod_data = json.load(json_file)
                except JSONDecodeError:
                    mod_data = DEFAULT_UE4SS_MODS
                for mod in mod_data:
                    mod_list[mod["mod_name"]] = mod["mod_enabled"]
            for i in range(self.rowCount()):
                item = self.index(i, 0)
                name = self.data(item, Qt.ItemDataRole.DisplayRole)
                if name in mod_list:
                    self.setData(
                        item,
                        True if mod_list[name] else False,
                        Qt.ItemDataRole.CheckStateRole,
                    )
                else:
                    self.setData(item, True, Qt.ItemDataRole.CheckStateRole)

    def flags(self, index: QModelIndex) -> Qt.ItemFlag:
        flags = super().flags(index)
        if not index.isValid():
            return (
                Qt.ItemFlag.ItemIsSelectable
                | Qt.ItemFlag.ItemIsDragEnabled
                | Qt.ItemFlag.ItemIsDropEnabled
                | Qt.ItemFlag.ItemIsEnabled
            )
        return (
            flags
            | Qt.ItemFlag.ItemIsUserCheckable
            | Qt.ItemFlag.ItemIsDragEnabled & Qt.ItemFlag.ItemIsEditable
        )

    def setData(
        self, index: QModelIndex, value: Any, role: int = Qt.ItemDataRole.EditRole
    ) -> bool:
        if not index.isValid() or role != Qt.ItemDataRole.CheckStateRole:
            return False

        if (
            bool(value)
            and self.data(index, Qt.ItemDataRole.DisplayRole) not in self._checked_items
        ):
            self._checked_items.add(self.data(index, Qt.ItemDataRole.DisplayRole))
        elif (
            not bool(value)
            and self.data(index, Qt.ItemDataRole.DisplayRole) in self._checked_items
        ):
            self._checked_items.remove(self.data(index, Qt.ItemDataRole.DisplayRole))
        self.dataChanged.emit(index, index, [role])
        return True

    def setStringList(self, strings: Iterable[str | None]):
        super().setStringList(strings)
        self._set_mod_states()

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid():
            return None

        if role == Qt.ItemDataRole.CheckStateRole:
            return (
                Qt.CheckState.Checked
                if self.data(index, Qt.ItemDataRole.DisplayRole) in self._checked_items
                else Qt.CheckState.Unchecked
            )

        return super().data(index, role)

    def canDropMimeData(
        self,
        data: QMimeData | None,
        action: Qt.DropAction,
        row: int,
        column: int,
        parent: QModelIndex,
    ) -> bool:
        if action == Qt.DropAction.MoveAction and (row != -1 or column != -1):
            return True
        return False
