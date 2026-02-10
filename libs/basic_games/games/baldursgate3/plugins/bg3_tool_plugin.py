from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon, QPixmap

import mobase


class BG3ToolPlugin(mobase.IPluginTool, mobase.IPlugin):
    desc = sub_name = ""
    icon_bytes: bytes

    def __init__(self):
        mobase.IPluginTool.__init__(self)
        mobase.IPlugin.__init__(self)
        self._pluginName = self._displayName = "BG3 Tools"
        self._pluginVersion = mobase.VersionInfo(1, 0, 0)
        pixmap = QPixmap()
        pixmap.loadFromData(self.icon_bytes, "SVG")
        self.qicon = QIcon(pixmap)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def version(self):
        return self._pluginVersion

    def author(self):
        return "daescha"

    def name(self):
        return f"{self._pluginName}: {self.sub_name}"

    def displayName(self):
        return f"{self._displayName}/{self.sub_name}"

    def tooltip(self):
        return self.description()

    def enabledByDefault(self):
        return self._organizer.managedGame().name() == "Baldur's Gate 3 Plugin"

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def icon(self) -> QIcon:
        return self.qicon

    def description(self) -> str:
        return QCoreApplication.translate(self._pluginName, self.desc)
