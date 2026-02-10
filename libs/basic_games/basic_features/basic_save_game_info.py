# -*- encoding: utf-8 -*-

import sys
from collections.abc import Mapping
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Self, Sequence

from PyQt6.QtCore import QDateTime, QLocale, Qt
from PyQt6.QtGui import QImage, QPixmap
from PyQt6.QtWidgets import QFormLayout, QLabel, QSizePolicy, QVBoxLayout, QWidget

import mobase


def format_date(date_time: QDateTime | datetime | str, format_str: str | None = None):
    """Default format for date and time in the `BasicGameSaveGameInfoWidget`.

    Args:
        date_time: either a `QDateTime`/`datetime` or a string together with
            a `format_str`.
        format_str (optional): date/time format string (see `QDateTime.fromString`).

    Returns:
        Date and time in short locale format.
    """
    if isinstance(date_time, str):
        date_time = QDateTime.fromString(date_time, format_str)
    return QLocale.system().toString(date_time, QLocale.FormatType.ShortFormat)


class BasicGameSaveGame(mobase.ISaveGame):
    def __init__(self, filepath: Path):
        super().__init__()
        self._filepath = filepath

    def getFilepath(self) -> str:
        return self._filepath.as_posix()

    def getName(self) -> str:
        return self._filepath.name

    def getCreationTime(self):
        return QDateTime.fromSecsSinceEpoch(int(self._filepath.stat().st_mtime))

    def getSaveGroupIdentifier(self) -> str:
        return ""

    def allFiles(self) -> list[str]:
        return [self.getFilepath()]


def get_filedate_metadata(p: Path, save: mobase.ISaveGame) -> Mapping[str, str]:
    """Returns saves file date as the metadata for `BasicGameSaveGameInfoWidget`."""
    return {"File Date:": format_date(save.getCreationTime())}


class BasicGameSaveGameInfoWidget(mobase.ISaveGameInfoWidget):
    """Save game info widget to display metadata and a preview."""

    def __init__(
        self,
        parent: QWidget | None,
        get_preview: (
            Callable[[Path], QPixmap | QImage | Path | str | None] | None
        ) = lambda p: None,
        get_metadata: (
            Callable[[Path, mobase.ISaveGame], Mapping[str, Any] | None] | None
        ) = get_filedate_metadata,
        max_width: int = 320,
    ):
        """
        Args:
            parent: parent widget
            get_preview (optional): `callback(savegame_path)` returning the
                saves preview image or the path to it.
            get_metadata (optional): `callback(savegame_path, ISaveGame)` returning
                the saves metadata. By default the saves file date is shown.
            max_width (optional): The maximum widget and (scaled) preview width.
                Defaults to 320.
        """
        super().__init__(parent)

        def _no_preview(p: Path) -> None:
            return None

        self._get_preview = get_preview or _no_preview
        self._get_metadata = get_metadata or get_filedate_metadata
        self._max_width = max_width or 320

        layout = QVBoxLayout()

        # Metadata form
        self._metadata_widget = QWidget()
        self._metadata_widget.setMaximumWidth(self._max_width)
        self._metadata_layout = form_layout = QFormLayout(self._metadata_widget)
        form_layout.setContentsMargins(0, 0, 0, 0)
        form_layout.setVerticalSpacing(2)
        layout.addWidget(self._metadata_widget)
        self._metadata_widget.hide()  # Backwards compatibility (no metadata)

        # Preview (pixmap)
        self._label = QLabel()
        layout.addWidget(self._label)
        self.setLayout(layout)

        self.setWindowFlags(
            Qt.WindowType.ToolTip | Qt.WindowType.BypassGraphicsProxyWidget
        )

    def setSave(self, save: mobase.ISaveGame):
        save_path = Path(save.getFilepath())

        # Clear previous
        self.hide()
        self._label.clear()
        while self._metadata_layout.count():
            layoutItem = self._metadata_layout.takeAt(0)
            if layoutItem is not None and (w := layoutItem.widget()):
                w.deleteLater()

        # Retrieve the pixmap and metadata:
        preview = self._get_preview(save_path)
        pixmap = None

        # Set the preview pixmap if the preview file exits
        if preview is not None:
            if isinstance(preview, str):
                preview = Path(preview)
            if isinstance(preview, Path):
                if preview.exists():
                    pixmap = QPixmap(str(preview))
                else:
                    print(
                        f"Failed to retrieve the preview, file not found: {preview}",
                        file=sys.stderr,
                    )
            elif isinstance(preview, QImage):
                pixmap = QPixmap.fromImage(preview)
            else:
                pixmap = preview
        if pixmap and not pixmap.isNull():
            # Scale the pixmap and show it:
            pixmap = pixmap.scaledToWidth(self._max_width)
            self._label.setPixmap(pixmap)
            self._label.show()
        else:
            self._label.hide()
            pixmap = None

        # Add metadata, file date by default.
        metadata = self._get_metadata(save_path, save)
        if metadata:
            for key, value in metadata.items():
                self._metadata_layout.addRow(*self._new_form_row(key, str(value)))
            self._metadata_widget.show()
            self._metadata_widget.setLayout(self._metadata_layout)
            self._metadata_widget.adjustSize()
        else:
            self._metadata_widget.hide()

        if metadata or pixmap:
            self.adjustSize()
            self.show()

    def _new_form_row(self, label: str = "", field: str = ""):
        qLabel = QLabel(text=label)
        qLabel.setAlignment(Qt.AlignmentFlag.AlignTop)
        qLabel.setStyleSheet("font: italic")
        qLabel.setSizePolicy(QSizePolicy.Policy.Minimum, QSizePolicy.Policy.Minimum)
        qField = QLabel(text=field)
        qField.setWordWrap(True)
        qField.setAlignment(Qt.AlignmentFlag.AlignTop)
        qField.setStyleSheet("font: bold")
        qField.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Preferred)
        return qLabel, qField

    def set_maximum_width(self, width: int):
        self._max_width = width
        self._metadata_widget.setMaximumWidth(width)


class BasicGameSaveGameInfo(mobase.SaveGameInfo):
    _get_widget: Callable[[QWidget | None], mobase.ISaveGameInfoWidget | None] | None

    def __init__(
        self,
        get_preview: (
            Callable[[Path], QPixmap | QImage | Path | str | None] | None
        ) = None,
        get_metadata: (
            Callable[[Path, mobase.ISaveGame], Mapping[str, Any] | None] | None
        ) = None,
        max_width: int = 0,
    ):
        """Args from: `BasicGameSaveGameInfoWidget`."""
        super().__init__()
        self._get_widget = lambda parent: BasicGameSaveGameInfoWidget(
            parent, get_preview, get_metadata, max_width
        )

    @classmethod
    def with_widget(
        cls,
        widget: type[mobase.ISaveGameInfoWidget] | None,
    ) -> Self:
        """

        Args:
            widget: a custom `ISaveGameInfoWidget` instead of the default
                `BasicGameSaveGameInfoWidget`.
        """
        self = cls()
        self._get_widget = lambda parent: widget(parent) if widget else None
        return self

    def getMissingAssets(self, save: mobase.ISaveGame) -> dict[str, Sequence[str]]:
        return {}

    def getSaveGameWidget(
        self, parent: QWidget | None = None
    ) -> mobase.ISaveGameInfoWidget | None:
        if self._get_widget:
            return self._get_widget(parent)
        else:
            return None
