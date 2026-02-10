from typing import Iterable

from PyQt6.QtCore import QModelIndex, Qt, pyqtSignal
from PyQt6.QtGui import QDropEvent
from PyQt6.QtWidgets import QAbstractItemView, QListView, QWidget


class UE4SSView(QListView):
    data_dropped = pyqtSignal()

    def __init__(self, parent: QWidget | None):
        super().__init__(parent)
        self.setAcceptDrops(True)
        self.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(True)
        self.setDragDropMode(QAbstractItemView.DragDropMode.InternalMove)
        self.setDefaultDropAction(Qt.DropAction.MoveAction)
        if (viewport := self.viewport()) is not None:
            viewport.setAcceptDrops(True)

    def dropEvent(self, e: QDropEvent | None):
        super().dropEvent(e)
        self.data_dropped.emit()

    def dataChanged(
        self, topLeft: QModelIndex, bottomRight: QModelIndex, roles: Iterable[int] = ()
    ):
        super().dataChanged(topLeft, bottomRight, roles)
        self.repaint()
