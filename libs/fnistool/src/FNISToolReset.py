import os
import sys

from FNISTool import FNISTool
from PyQt6.QtCore import QFileInfo, QCoreApplication
from PyQt6.QtGui import QFileSystemModel, QIcon
from PyQt6.QtWidgets import QMessageBox

import mobase

class FNISToolReset(mobase.IPluginTool):
    def __init__(self):
        super(FNISToolReset, self).__init__()
        self.__organizer = None
        self.__parentWidget = None

    def init(self, organizer):
        self.__organizer = organizer
        return True

    def name(self):
        return "FNIS Integration Tool Reset"

    def localizedName(self):
        return self.tr("FNIS Integration Tool Reset")

    def author(self):
        return "LostDragonist"

    def description(self):
        return self.tr("Provides an easier way to reset the FNIS integration tool settings when needed.")

    def version(self):
        return mobase.VersionInfo(1, 0, 0, 0)

    def master(self):
        return "FNIS Integration Tool"

    def settings(self):
        return []

    def displayName(self):
        return self.tr("FNIS/Reset FNIS Settings")

    def tooltip(self):
        return self.description()

    def icon(self):
        fnisPath = self.__organizer.pluginSetting(self.__mainToolName(), "fnis-path")
        if os.path.exists(fnisPath):
            # We can't directly grab the icon from an executable, but this seems like the simplest alternative.
            fin = QFileInfo(fnisPath)
            model = QFileSystemModel()
            model.setRootPath(fin.path())
            return model.fileIcon(model.index(fin.filePath()))
        else:
            # Fall back to where the user might have put an icon manually.
            return QIcon("plugins/FNIS.ico")

    def setParentWidget(self, widget):
        self.__parentWidget = widget

    def display(self):
        result = QMessageBox.question(self.__parentWidget, self.tr("Reset settings?"), self.tr("Would you like to reset the options that pop up when you first ran \"{}\"?").format(self.__mainToolName()))
        if result == QMessageBox.StandardButton.Yes:
            self.__organizer.setPluginSetting(self.__mainToolName(), "initialised", False)

    def tr(self, str):
        return QCoreApplication.translate("FNISToolReset", str)

    @staticmethod
    def __mainToolName():
        return FNISTool().name()

def createPlugin():
    return FNISToolReset()
