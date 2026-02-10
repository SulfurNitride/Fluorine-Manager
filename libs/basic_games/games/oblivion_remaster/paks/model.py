import itertools
import typing
from enum import IntEnum, auto
from typing import Any, TypeAlias, overload

from PyQt6.QtCore import (
    QAbstractItemModel,
    QByteArray,
    QDataStream,
    QDir,
    QFileInfo,
    QMimeData,
    QModelIndex,
    QObject,
    Qt,
    QVariant,
)
from PyQt6.QtWidgets import QWidget

import mobase

_PakInfo: TypeAlias = tuple[str, str, str, str]


class PaksColumns(IntEnum):
    PRIORITY = auto()
    PAK_NAME = auto()
    SOURCE = auto()


class PaksModel(QAbstractItemModel):
    def __init__(self, parent: QWidget | None, organizer: mobase.IOrganizer):
        super().__init__(parent)
        self.paks: dict[int, _PakInfo] = {}
        self._organizer = organizer
        self._init_mod_states()

    def _init_mod_states(self):
        profile = QDir(self._organizer.profilePath())
        paks_txt = QFileInfo(profile.absoluteFilePath("paks.txt"))
        if paks_txt.exists():
            with open(paks_txt.absoluteFilePath(), "r") as paks_file:
                index = 0
                for line in paks_file:
                    self.paks[index] = (line, "", "", "")
                    index += 1

    def set_paks(self, paks: dict[int, _PakInfo]):
        self.layoutAboutToBeChanged.emit()
        self.paks = paks
        self.layoutChanged.emit()
        self.dataChanged.emit(
            self.index(0, 0),
            self.index(self.rowCount(), self.columnCount()),
            [Qt.ItemDataRole.DisplayRole],
        )

    def flags(self, index: QModelIndex) -> Qt.ItemFlag:
        if not index.isValid():
            return (
                Qt.ItemFlag.ItemIsSelectable
                | Qt.ItemFlag.ItemIsDragEnabled
                | Qt.ItemFlag.ItemIsDropEnabled
                | Qt.ItemFlag.ItemIsEnabled
            )
        return (
            super().flags(index)
            | Qt.ItemFlag.ItemIsDragEnabled
            | Qt.ItemFlag.ItemIsDropEnabled & Qt.ItemFlag.ItemIsEditable
        )

    def columnCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return len(PaksColumns)

    def index(
        self, row: int, column: int, parent: QModelIndex = QModelIndex()
    ) -> QModelIndex:
        if (
            row < 0
            or row >= self.rowCount()
            or column < 0
            or column >= self.columnCount()
        ):
            return QModelIndex()
        return self.createIndex(row, column, row)

    @overload
    def parent(self, child: QModelIndex) -> QModelIndex: ...
    @overload
    def parent(self) -> QObject | None: ...

    def parent(self, child: QModelIndex | None = None) -> QModelIndex | QObject | None:
        if child is None:
            return super().parent()
        return QModelIndex()

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return len(self.paks)

    def setData(
        self, index: QModelIndex, value: Any, role: int = Qt.ItemDataRole.EditRole
    ) -> bool:
        return False

    def headerData(
        self,
        section: int,
        orientation: Qt.Orientation,
        role: int = Qt.ItemDataRole.DisplayRole,
    ) -> typing.Any:
        if (
            orientation != Qt.Orientation.Horizontal
            or role != Qt.ItemDataRole.DisplayRole
        ):
            return QVariant()

        column = PaksColumns(section + 1)
        match column:
            case PaksColumns.PAK_NAME:
                return "Pak Group"
            case PaksColumns.PRIORITY:
                return "Priority"
            case PaksColumns.SOURCE:
                return "Source"

        return QVariant()

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole) -> Any:
        if not index.isValid():
            return None
        if index.column() + 1 == PaksColumns.PAK_NAME:
            if role == Qt.ItemDataRole.DisplayRole:
                return self.paks[index.row()][0]
        elif index.column() + 1 == PaksColumns.PRIORITY:
            if role == Qt.ItemDataRole.DisplayRole:
                return index.row()
        elif index.column() + 1 == PaksColumns.SOURCE:
            if role == Qt.ItemDataRole.DisplayRole:
                return self.paks[index.row()][1]
        return QVariant()

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

    def supportedDropActions(self) -> Qt.DropAction:
        return Qt.DropAction.MoveAction

    def dropMimeData(
        self,
        data: QMimeData | None,
        action: Qt.DropAction,
        row: int,
        column: int,
        parent: QModelIndex,
    ) -> bool:
        if action == Qt.DropAction.IgnoreAction:
            return True

        if data is None:
            return False

        encoded: QByteArray = data.data("application/x-qabstractitemmodeldatalist")
        stream: QDataStream = QDataStream(encoded, QDataStream.OpenModeFlag.ReadOnly)
        source_rows: list[int] = []

        while not stream.atEnd():
            source_row = stream.readInt()
            col = stream.readInt()
            size = stream.readInt()
            item_data = {}
            for _ in range(size):
                role = stream.readInt()
                value = stream.readQVariant()
                item_data[role] = value
            if col == 0:
                source_rows.append(source_row)

        if row == -1:
            row = parent.row()

        if row < 0 or row >= len(self.paks):
            new_priority = len(self.paks)
        else:
            new_priority = row

        before_paks: list[_PakInfo] = []
        moved_paks: list[_PakInfo] = []
        after_paks: list[_PakInfo] = []
        before_paks_p: list[_PakInfo] = []
        moved_paks_p: list[_PakInfo] = []
        after_paks_p: list[_PakInfo] = []
        for row, paks in sorted(self.paks.items()):
            if row < new_priority:
                if row in source_rows:
                    if paks[0].casefold()[-2:] == "_p":
                        moved_paks_p.append(paks)
                    else:
                        moved_paks.append(paks)
                else:
                    if paks[0].casefold()[-2:] == "_p":
                        before_paks_p.append(paks)
                    else:
                        before_paks.append(paks)
            if row >= new_priority:
                if row in source_rows:
                    if paks[0].casefold()[-2:] == "_p":
                        moved_paks_p.append(paks)
                    else:
                        moved_paks.append(paks)
                else:
                    if paks[0].casefold()[-2:] == "_p":
                        after_paks_p.append(paks)
                    else:
                        after_paks.append(paks)

        new_paks = dict(
            enumerate(
                itertools.chain(
                    before_paks,
                    moved_paks,
                    after_paks,
                    before_paks_p,
                    moved_paks_p,
                    after_paks_p,
                )
            )
        )

        index = 8999
        for row, pak in new_paks.items():
            current_dir = QDir(pak[2])
            parent_dir = QDir(pak[2])
            parent_dir.cdUp()
            if current_dir.exists() and parent_dir.dirName().casefold() == "~mods":
                new_paks[row] = (
                    pak[0],
                    pak[1],
                    pak[2],
                    parent_dir.absoluteFilePath(str(index).zfill(4)),
                )
                index -= 1

        self.set_paks(new_paks)
        return False
